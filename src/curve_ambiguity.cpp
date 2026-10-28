#include "rates/curve_ambiguity.hpp"
#include <algorithm>
#include <map>
#include <numeric>
#include <set>

namespace rates {
namespace {
using CashAmounts = std::map<Date, double>;
CashAmounts floating_amounts(const PreparedSwap &trade, const std::optional<DiscountCurve> &projection) {
    CashAmounts amounts;
    for (const auto &period : trade.floating_periods()) {
        if (projection) {
            amounts[period.payment] +=
                projection->forward(period.start, period.end, trade.trade().conventions.floating_day_count)
                    .value *
                period.accrual.value;
        } else {
            // With same-curve projection and no payment lag, each coupon PV is D(start)-D(end).
            if (period.payment != period.end)
                throw std::invalid_argument("Curve ambiguity requires unlagged single-curve coupons");
            amounts[period.start] += 1;
            amounts[period.end] -= 1;
        }
    }
    return amounts;
}
void add_fixed(CashAmounts &amounts, const PreparedSwap &trade, double coupon) {
    for (const auto &period : trade.fixed_periods())
        amounts[period.payment] += coupon * period.accrual.value;
}
struct SparseConstraint {
    std::string label;
    CashAmounts amounts;
    double upper;
};
} // namespace
CurveAmbiguityProblem curve_ambiguity_problem(Date valuation, const std::vector<QuoteBand> &quotes,
                                              const std::vector<Swap> &portfolio, ForwardBounds forwards,
                                              const Conventions &conventions,
                                              const std::optional<DiscountCurve> &projection) {
    conventions.validate();
    if (quotes.empty() || portfolio.empty())
        throw std::invalid_argument("Curve ambiguity requires quotes and a target portfolio");
    if (!std::isfinite(forwards.lower) || !std::isfinite(forwards.upper) || forwards.lower > forwards.upper)
        throw std::invalid_argument("Invalid forward bounds");
    if (projection && projection->valuation_date() != valuation)
        throw std::invalid_argument("Projection valuation date mismatch");
    std::set<Date> grid{valuation};
    std::set<std::string> labels;
    std::vector<SparseConstraint> sparse;
    Date last_quote = valuation;
    for (const auto &band : quotes) {
        const auto &q = band.quote;
        if (q.label.empty() || !labels.insert(q.label).second)
            throw std::invalid_argument("Quote labels must be nonempty and unique");
        if (!std::isfinite(band.bid) || !std::isfinite(band.ask) || band.bid > band.ask)
            throw std::invalid_argument("Invalid quote interval: " + q.label);
        const auto end = quote_pillar(q, conventions);
        if (q.maturity <= valuation || end <= valuation)
            throw std::invalid_argument("Invalid quote maturity: " + q.label);
        last_quote = std::max(last_quote, end);
        grid.insert(end);
        if (q.instrument == Instrument::Swap) {
            const PreparedSwap trade{Swap{valuation, q.maturity, 1, SimpleRate{0}, Side::Payer, conventions}};
            auto ask = floating_amounts(trade, std::nullopt);
            add_fixed(ask, trade, -band.ask);
            auto bid = floating_amounts(trade, std::nullopt);
            for (auto &[date, amount] : bid) {
                static_cast<void>(date);
                amount = -amount;
            }
            add_fixed(bid, trade, band.bid);
            sparse.push_back({q.label + ":ask", std::move(ask), 0});
            sparse.push_back({q.label + ":bid", std::move(bid), 0});
        } else if (q.instrument == Instrument::Deposit || q.instrument == Instrument::ContinuousZero) {
            const auto dc = q.instrument == Instrument::Deposit ? conventions.deposit_day_count
                                                                : DayCount::Actual365Fixed;
            const auto comp =
                q.instrument == Instrument::Deposit ? Compounding::Simple : Compounding::Continuous;
            const auto time = year_fraction(valuation, end, dc);
            const auto lower = rate_to_discount(band.ask, time, comp).value;
            const auto upper = rate_to_discount(band.bid, time, comp).value;
            sparse.push_back({q.label + ":ask", {{end, -1}}, -lower});
            sparse.push_back({q.label + ":bid", {{end, 1}}, upper});
        } else {
            throw std::invalid_argument("Unsupported ambiguity quote: " + q.label);
        }
    }
    CashAmounts objective;
    for (const auto &trade : portfolio) {
        trade.validate(valuation);
        const PreparedSwap prepared{trade};
        auto amounts = floating_amounts(prepared, projection);
        add_fixed(amounts, prepared, -trade.fixed_rate.value);
        const double scale = trade.notional * (trade.side == Side::Payer ? 1 : -1);
        for (const auto &[date, amount] : amounts)
            objective[date] += scale * amount;
        // Preserve all dates, including cancelling floating coupons, for witness repricing.
        for (const auto &period : prepared.floating_periods()) {
            grid.insert(period.start);
            grid.insert(period.end);
            grid.insert(period.payment);
        }
    }
    const auto insert_dates = [&](const CashAmounts &amounts) {
        for (const auto &[date, amount] : amounts) {
            if (!std::isfinite(amount))
                throw std::overflow_error("Non-finite ambiguity cash flow");
            grid.insert(date);
        }
    };
    insert_dates(objective);
    for (const auto &row : sparse)
        insert_dates(row.amounts);
    if (*grid.rbegin() > last_quote)
        throw std::invalid_argument("Ambiguity target extends beyond the last quote");
    CurveAmbiguityProblem result{valuation, {grid.begin(), grid.end()}, {}, {}, forwards};
    const auto dense = [&](const CashAmounts &amounts) {
        std::vector<double> values(result.dates.size(), 0);
        for (const auto &[date, amount] : amounts) {
            const auto i =
                std::lower_bound(result.dates.begin(), result.dates.end(), date) - result.dates.begin();
            values[static_cast<std::size_t>(i)] = amount;
        }
        return values;
    };
    result.objective = dense(objective);
    for (const auto &row : sparse)
        result.constraints.push_back({row.label, dense(row.amounts), row.upper});
    // Ratio constraints are linear even when negative forwards are allowed.
    for (std::size_t i = 1; i < result.dates.size(); ++i) {
        const auto previous = result.dates[i - 1], next = result.dates[i];
        const auto dt = year_fraction(previous, next, DayCount::Actual365Fixed);
        const double lower = rate_to_discount(forwards.upper, dt, Compounding::Continuous).value;
        const double upper = rate_to_discount(forwards.lower, dt, Compounding::Continuous).value;
        result.constraints.push_back(
            {"forward:" + next.str() + ":upper", dense({{previous, lower}, {next, -1}}), 0});
        result.constraints.push_back(
            {"forward:" + next.str() + ":lower", dense({{previous, -upper}, {next, 1}}), 0});
    }
    return result;
}
DiscountCurve CurveAmbiguityProblem::witness(const std::vector<double> &discounts, double tolerance) const {
    if (!std::isfinite(tolerance) || tolerance < 0 || discounts.size() != dates.size() || dates.empty())
        throw std::invalid_argument("Invalid ambiguity witness dimensions or tolerance");
    if (std::abs(discounts.front() - 1) > tolerance)
        throw std::invalid_argument("Ambiguity witness must have D(valuation)=1");
    std::vector<CurveNode> nodes;
    for (std::size_t i = 0; i < dates.size(); ++i) {
        if (!std::isfinite(discounts[i]) || discounts[i] <= 0)
            throw std::invalid_argument("Ambiguity witness discounts must be finite and positive");
        if (i > 0)
            nodes.push_back({dates[i], DiscountFactor{discounts[i]}});
    }
    for (const auto &row : constraints) {
        if (row.coefficients.size() != discounts.size())
            throw std::invalid_argument("Malformed ambiguity constraint");
        const double value =
            std::inner_product(row.coefficients.begin(), row.coefficients.end(), discounts.begin(), 0.0);
        if (!std::isfinite(value) || value - row.upper > tolerance)
            throw std::invalid_argument("Ambiguity witness violates " + row.label);
    }
    return {valuation, nodes};
}
} // namespace rates
