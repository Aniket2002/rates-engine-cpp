#include "rates/risk_engine.hpp"
#include <algorithm>
#include <utility>

namespace rates {
namespace {
void check_bump(BasisPoints bump) {
    if (!std::isfinite(bump.value) || bump.value <= 0)
        throw std::invalid_argument("Risk bump must be finite and positive in basis points");
}
DiscountCurve shocked_curve(const CurveBootstrapper &b, Date v, const std::vector<MarketQuote> &quotes,
                            double shift, std::size_t bucket) {
    try {
        auto shocked = quotes;
        for (std::size_t i = 0; i < shocked.size(); ++i) {
            if (bucket == quotes.size() || bucket == i) {
                const double rate = shocked[i].rate.value + shift;
                if (rate == shocked[i].rate.value)
                    throw std::invalid_argument("Quote bump is below floating-point resolution");
                shocked[i].rate = SimpleRate{rate};
            }
        }
        return b.build(v, std::move(shocked)).curve;
    } catch (const std::exception &e) {
        throw std::runtime_error("Risk shock failed (bucket " +
                                 (bucket == quotes.size() ? std::string{"parallel"} : quotes[bucket].label) +
                                 ", decimal shift " + std::to_string(shift) + "): " + e.what());
    }
}
CurveShock quote_pair(const CurveBootstrapper &b, Date v, const std::vector<MarketQuote> &quotes,
                      BasisPoints bump, std::size_t bucket) {
    const bool parallel = bucket == quotes.size();
    return {parallel ? "parallel" : quotes[bucket].label,
            parallel ? v : quote_pillar(quotes[bucket], b.conventions()),
            shocked_curve(b, v, quotes, -bump.value * 1e-4, bucket),
            shocked_curve(b, v, quotes, bump.value * 1e-4, bucket)};
}
CurveShock zero_pair(const DiscountCurve &curve, std::size_t index, BasisPoints bump) {
    const auto date = curve.nodes()[index + 1].date;
    return {date.str(), date, curve.shift_zero_node(index, BasisPoints{-bump.value}),
            curve.shift_zero_node(index, bump)};
}
double sensitivity(const CurveShock &shock, const PreparedSwap &trade, double bump_bps) {
    return (price(shock.down, trade).pv - price(shock.up, trade).pv) / (2 * bump_bps);
}
std::vector<RiskBucket> buckets(const std::vector<CurveShock> &shocks, const PreparedSwap &trade,
                                double bump_bps) {
    std::vector<RiskBucket> result;
    result.reserve(shocks.size());
    for (const auto &shock : shocks)
        result.push_back({shock.label, shock.maturity, sensitivity(shock, trade, bump_bps)});
    return result;
}
void check_trades(const std::vector<Swap> &trades) {
    if (trades.empty())
        throw std::invalid_argument("Portfolio must contain at least one trade");
}
} // namespace
RiskScenarios::RiskScenarios(DiscountCurve base, CurveShock parallel, std::vector<CurveShock> quotes,
                             std::vector<CurveShock> zeros, double bump_bps, double tolerance,
                             std::size_t calibrations)
    : base_(std::move(base)), parallel_(std::move(parallel)), quotes_(std::move(quotes)),
      zeros_(std::move(zeros)), bump_bps_(bump_bps), tolerance_(tolerance), calibrations_(calibrations) {}
RiskReport RiskScenarios::report(const Swap &trade, std::string id) const {
    return report(PreparedSwap{trade}, std::move(id));
}
RiskReport RiskScenarios::report(const PreparedSwap &prepared, std::string id) const {
    const auto &trade = prepared.trade();
    return {base_.valuation_date(),
            std::move(id),
            trade,
            price(base_, prepared),
            sensitivity(parallel_, prepared, bump_bps_),
            buckets(quotes_, prepared, bump_bps_),
            buckets(zeros_, prepared, bump_bps_),
            bump_bps_,
            tolerance_};
}
std::vector<RiskReport> RiskScenarios::portfolio(const std::vector<Swap> &trades,
                                                 const std::string &id) const {
    check_trades(trades);
    std::vector<RiskReport> reports;
    reports.reserve(trades.size());
    for (const auto &trade : trades)
        reports.push_back(report(trade, id));
    return reports;
}
RiskEngine::RiskEngine(CurveBootstrapper b) : bootstrapper_(std::move(b)) {}
double RiskEngine::parallel_quote_dv01(Date v, const std::vector<MarketQuote> &q, const Swap &t,
                                       BasisPoints bump) const {
    check_bump(bump);
    return sensitivity(quote_pair(bootstrapper_, v, q, bump, q.size()), PreparedSwap{t}, bump.value);
}
std::vector<RiskBucket> RiskEngine::quote_risk(Date v, const std::vector<MarketQuote> &q, const Swap &t,
                                               BasisPoints bump) const {
    check_bump(bump);
    if (q.empty())
        throw std::invalid_argument("Quote risk requires a nonempty calibration quote set");
    const PreparedSwap prepared{t};
    std::vector<RiskBucket> result;
    result.reserve(q.size());
    for (std::size_t i = 0; i < q.size(); ++i) {
        const auto shock = quote_pair(bootstrapper_, v, q, bump, i);
        result.push_back({shock.label, shock.maturity, sensitivity(shock, prepared, bump.value)});
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.maturity < b.maturity; });
    return result;
}
std::vector<RiskBucket> RiskEngine::zero_risk(const DiscountCurve &curve, const Swap &trade,
                                              BasisPoints bump) const {
    check_bump(bump);
    const PreparedSwap prepared{trade};
    std::vector<RiskBucket> result;
    for (std::size_t i = 0; i + 1 < curve.nodes().size(); ++i) {
        const auto shock = zero_pair(curve, i, bump);
        result.push_back({shock.label, shock.maturity, sensitivity(shock, prepared, bump.value)});
    }
    return result;
}
RiskScenarios RiskEngine::prepare_scenarios(Date v, const std::vector<MarketQuote> &q,
                                            BasisPoints bump) const {
    check_bump(bump);
    auto calibrated = bootstrapper_.build(v, q);
    std::size_t calibrations = 1;
    auto parallel = quote_pair(bootstrapper_, v, q, bump, q.size());
    calibrations += 2;
    std::vector<CurveShock> quote_shocks;
    quote_shocks.reserve(q.size());
    for (std::size_t i = 0; i < q.size(); ++i) {
        quote_shocks.push_back(quote_pair(bootstrapper_, v, q, bump, i));
        calibrations += 2;
    }
    std::sort(quote_shocks.begin(), quote_shocks.end(),
              [](const auto &a, const auto &b) { return a.maturity < b.maturity; });
    std::vector<CurveShock> zero_shocks;
    zero_shocks.reserve(calibrated.curve.nodes().size() - 1);
    for (std::size_t i = 0; i + 1 < calibrated.curve.nodes().size(); ++i)
        zero_shocks.push_back(zero_pair(calibrated.curve, i, bump));
    return RiskScenarios{std::move(calibrated.curve),
                         std::move(parallel),
                         std::move(quote_shocks),
                         std::move(zero_shocks),
                         bump.value,
                         bootstrapper_.settings().quote_tolerance,
                         calibrations};
}
RiskReport RiskEngine::report(Date v, const std::vector<MarketQuote> &q, const Swap &trade, std::string id,
                              BasisPoints bump) const {
    return prepare_scenarios(v, q, bump).report(trade, std::move(id));
}
std::vector<RiskReport> RiskEngine::portfolio_report(Date v, const std::vector<MarketQuote> &q,
                                                     const std::vector<Swap> &trades, const std::string &id,
                                                     BasisPoints bump) const {
    check_trades(trades);
    return prepare_scenarios(v, q, bump).portfolio(trades, id);
}
std::vector<double> RiskEngine::portfolio_parallel_dv01(Date v, const std::vector<MarketQuote> &q,
                                                        const std::vector<Swap> &trades,
                                                        BasisPoints bump) const {
    check_trades(trades);
    check_bump(bump);
    const auto shock = quote_pair(bootstrapper_, v, q, bump, q.size());
    std::vector<double> result;
    result.reserve(trades.size());
    for (const auto &trade : trades)
        result.push_back(sensitivity(shock, PreparedSwap{trade}, bump.value));
    return result;
}
} // namespace rates
