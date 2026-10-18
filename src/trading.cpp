#include "rates/trading.hpp"
#include <algorithm>
#include <cmath>

namespace rates::trading {
namespace {
constexpr double max_shares = 1e12;
double whole_shares(double value) {
    if (!std::isfinite(value) || value < 0 || value > max_shares)
        throw std::overflow_error("Share quantity exceeds supported bounds");
    return std::floor(value);
}
void validate(const SimulationConfig &c) {
    if (!std::isfinite(c.initial_cash) || c.initial_cash <= 0 || c.lookback < 2 ||
        !std::isfinite(c.half_spread_bps) || c.half_spread_bps < 0 || c.half_spread_bps >= 10000 ||
        !std::isfinite(c.fee_bps) || c.fee_bps < 0 || c.fee_bps >= 10000 || c.latency_seconds < 0 ||
        c.max_price_age_seconds < 0 || c.order_ttl_seconds <= c.latency_seconds ||
        c.max_execution_delay_seconds < 0)
        throw std::invalid_argument("Invalid trading simulation configuration");
}
} // namespace
PriceTape::PriceTape(std::vector<PriceEvent> events) : events_(std::move(events)) {
    if (events_.empty())
        throw std::invalid_argument("Empty price tape");
    for (std::size_t i = 0; i < events_.size(); ++i) {
        const auto &e = events_[i];
        if (!std::isfinite(e.price) || e.price <= 0 || e.available_at < e.event_time ||
            (e.kind != PriceKind::Open && e.kind != PriceKind::Close) ||
            (i > 0 && e.available_at < events_[i - 1].available_at))
            throw std::invalid_argument("Invalid price or event/availability ordering");
        auto &versions = versions_[{e.event_time, e.kind}];
        if (e.revision != versions.size() ||
            (!versions.empty() && e.available_at <= events_[versions.back()].available_at))
            throw std::invalid_argument("Revisions must start at zero and increase without gaps");
        versions.push_back(i);
    }
}
std::vector<PriceEvent> PriceTape::closes_before(Timestamp decision, std::size_t count) const {
    std::vector<PriceEvent> result;
    for (auto it = versions_.rbegin(); it != versions_.rend() && result.size() < count; ++it) {
        if (it->first.second != PriceKind::Close || it->first.first >= decision)
            continue;
        const auto &versions = it->second;
        const auto known =
            std::lower_bound(versions.begin(), versions.end(), decision,
                             [&](std::size_t i, Timestamp time) { return events_[i].available_at < time; });
        if (known != versions.begin())
            result.push_back(events_[*std::prev(known)]);
    }
    std::reverse(result.begin(), result.end());
    return result;
}
SimulationResult simulate(const PriceTape &tape, const std::vector<Timestamp> &decisions,
                          const SimulationConfig &c) {
    validate(c);
    if (decisions.empty() || !std::is_sorted(decisions.begin(), decisions.end()) ||
        std::adjacent_find(decisions.begin(), decisions.end()) != decisions.end())
        throw std::invalid_argument("Decision times must be nonempty and strictly increasing");
    SimulationResult result;
    double cash = c.initial_cash;
    double shares = 0;
    double peak = c.initial_cash;
    double previous_equity = c.initial_cash;
    const double spread = c.half_spread_bps / 10000;
    const double fee_rate = c.fee_bps / 10000;
    std::optional<std::size_t> pending;
    std::size_t cursor = 0;
    for (const auto decision : decisions) {
        // Equal timestamps remain unknown until after this decision. Late old
        // observations and revisions cannot become new executable prices.
        while (cursor < tape.events().size() && tape.events()[cursor].available_at < decision) {
            const auto &event = tape.events()[cursor++];
            if (!pending)
                continue;
            auto &order = result.orders[*pending];
            if (event.available_at - order.decision_time > c.order_ttl_seconds) {
                order.status = "expired";
                pending.reset();
                continue;
            }
            if (event.kind != PriceKind::Open || event.revision != 0 ||
                event.event_time - order.decision_time <= c.latency_seconds ||
                event.available_at - event.event_time > c.max_execution_delay_seconds)
                continue;
            const bool buy = order.requested_shares > 0;
            const double execution = event.price * (buy ? 1 + spread : 1 - spread);
            if (!std::isfinite(execution) || execution <= 0)
                throw std::overflow_error("Execution price exceeds supported bounds");
            double quantity = std::abs(order.requested_shares);
            if (buy)
                quantity = std::min(quantity, whole_shares(cash / (execution * (1 + fee_rate))));
            else
                quantity = std::min(quantity, shares);
            if (quantity == 0) {
                order.status = "unaffordable";
                pending.reset();
                continue;
            }
            const double signed_quantity = buy ? quantity : -quantity;
            const double notional = quantity * execution;
            const double fee = notional * fee_rate;
            const double spread_cost = quantity * event.price * spread;
            cash -= signed_quantity * execution + fee;
            shares += signed_quantity;
            if (!std::isfinite(cash) || cash < -1e-8)
                throw std::overflow_error("Invalid cash after execution");
            cash = std::max(0.0, cash); // remove only sub-cent floating-point residue
            order.filled_shares = signed_quantity;
            order.status = signed_quantity == order.requested_shares ? "filled" : "partial_cancelled";
            result.fills.push_back({*pending, event.event_time, event.available_at, signed_quantity,
                                    event.price, execution, fee, spread_cost});
            result.total_fees += fee;
            result.total_spread_cost += spread_cost;
            result.gross_traded_notional += notional;
            pending.reset();
        }
        const auto window = tape.closes_before(decision, c.lookback);
        if (window.empty())
            throw MarketDataError(MarketDataFailure::Missing, "No close available before decision");
        const auto &mark = window.back();
        // A newly published revision of an old close does not reset freshness.
        if (decision - mark.event_time > c.max_price_age_seconds)
            throw MarketDataError(MarketDataFailure::Stale, "Latest known close is stale");
        const double exposure = shares * mark.price;
        const double equity = cash + exposure;
        if (!std::isfinite(equity) || equity <= 0 || !std::isfinite(result.gross_traded_notional))
            throw std::overflow_error("Nonfinite or nonpositive portfolio value");
        peak = std::max(peak, equity);
        const double drawdown = 1 - equity / peak;
        result.max_drawdown = std::max(result.max_drawdown, drawdown);
        const double period_return = equity / previous_equity - 1;
        if (!std::isfinite(period_return))
            throw std::overflow_error("Period return exceeds supported bounds");
        result.equity.push_back({decision, mark.event_time, mark.available_at, mark.price, cash, shares,
                                 exposure, equity, equity - c.initial_cash, drawdown, period_return});
        previous_equity = equity;
        if (pending) {
            auto &order = result.orders[*pending];
            order.status = decision - order.decision_time > c.order_ttl_seconds ? "expired" : "replaced";
            pending.reset();
        }
        if (window.size() < c.lookback)
            continue;
        double mean = 0;
        for (const auto &point : window)
            mean += point.price / static_cast<double>(window.size());
        // Entry/exit only: do not continually rebalance a long position.
        double quantity = 0;
        if (mark.price > mean && shares == 0)
            quantity = whole_shares(cash / (mark.price * (1 + spread) * (1 + fee_rate)));
        else if (mark.price <= mean && shares > 0)
            quantity = -shares;
        if (quantity != 0) {
            result.orders.push_back({decision, mark.event_time, mark.available_at, quantity});
            pending = result.orders.size() - 1;
        }
    }
    if (pending)
        result.orders[*pending].status = "unfilled_end";
    result.turnover = result.gross_traded_notional / c.initial_cash;
    // Returns between observation points only; initial cash is not an extra period.
    if (result.equity.size() > 2) {
        double mean = 0;
        const auto n = static_cast<double>(result.equity.size() - 1);
        for (std::size_t i = 1; i < result.equity.size(); ++i)
            mean += result.equity[i].period_return / n;
        double variance = 0;
        for (std::size_t i = 1; i < result.equity.size(); ++i)
            variance += std::pow(result.equity[i].period_return - mean, 2) / (n - 1);
        result.period_volatility = std::sqrt(variance);
    }
    if (!std::isfinite(result.turnover) || !std::isfinite(result.period_volatility) ||
        !std::isfinite(result.total_fees) || !std::isfinite(result.total_spread_cost))
        throw std::overflow_error("Simulation statistics exceed supported bounds");
    return result;
}
} // namespace rates::trading
