#include "rates/curve_bootstrapper.hpp"
#include <algorithm>
#include <numeric>
#include <set>
#include <utility>

namespace rates {
namespace {
void validate_settings(const SolverSettings &s) {
    if (!std::isfinite(s.quote_tolerance) || s.quote_tolerance <= 0 ||
        !std::isfinite(s.log_discount_tolerance) || s.log_discount_tolerance <= 0 || s.max_iterations <= 0)
        throw std::invalid_argument("Solver tolerances and iteration limit must be positive and finite");
}
double checked_evaluate(const std::function<double(double)> &f, double x) {
    const double y = f(x);
    if (!std::isfinite(y))
        throw std::runtime_error("Root solver encountered non-finite objective");
    return y;
}
bool same_sign(double a, double b) {
    return std::signbit(a) == std::signbit(b);
}
} // namespace
RootResult bisect(const std::function<double(double)> &f, double lo, double hi, const SolverSettings &s) {
    validate_settings(s);
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo >= hi)
        throw std::invalid_argument("Invalid root bracket");
    double flo = checked_evaluate(f, lo);
    const double fhi = checked_evaluate(f, hi);
    if (std::abs(flo) <= s.quote_tolerance)
        return {lo, 0, flo};
    if (std::abs(fhi) <= s.quote_tolerance)
        return {hi, 0, fhi};
    if (same_sign(flo, fhi))
        throw std::runtime_error("Root is not bracketed");
    for (int i = 1; i <= s.max_iterations; ++i) {
        const double mid = std::midpoint(lo, hi);
        const double fm = checked_evaluate(f, mid);
        if (std::abs(fm) <= s.quote_tolerance)
            return {mid, i, fm};
        if (hi - lo <= s.log_discount_tolerance)
            throw std::runtime_error("Root interval exhausted before quote residual tolerance was met");
        if (same_sign(flo, fm)) {
            lo = mid;
            flo = fm;
        } else
            hi = mid;
    }
    throw std::runtime_error("Root solver exceeded iteration limit");
}
CurveBootstrapper::CurveBootstrapper(Conventions c, SolverSettings s)
    : conventions_(std::move(c)), settings_(s) {
    conventions_.validate();
    validate_settings(settings_);
}
BootstrapResult CurveBootstrapper::build(Date valuation, std::vector<MarketQuote> quotes) const {
    if (quotes.empty())
        throw std::invalid_argument("Cannot calibrate an empty quote set");
    const auto &c = conventions_;
    if (c.calendar.adjust(valuation, c.business_day) != valuation)
        throw std::invalid_argument("Valuation date must satisfy business-day convention");
    const auto pillar = [&](const MarketQuote &q) {
        return q.instrument == Instrument::ContinuousZero ? q.maturity
                                                          : c.calendar.adjust(q.maturity, c.business_day);
    };
    std::sort(quotes.begin(), quotes.end(),
              [&](const auto &a, const auto &b) { return pillar(a) < pillar(b); });
    std::vector<CurveNode> nodes;
    std::vector<CalibrationRow> rows;
    std::set<std::string> labels;
    for (const auto &q : quotes) {
        if (q.label.empty() || !labels.insert(q.label).second)
            throw std::invalid_argument("Quote labels must be nonempty and unique");
        if (!std::isfinite(q.rate.value))
            throw std::invalid_argument("Quote must be finite: " + q.label);
        const Date end = pillar(q);
        if (q.maturity <= valuation || end <= valuation || (!nodes.empty() && end <= nodes.back().date))
            throw std::invalid_argument("Invalid or duplicate adjusted maturity: " + q.label);
        int iterations = 0;
        double df = 0;
        if (q.instrument == Instrument::Deposit) {
            df = rate_to_discount(q.rate.value, year_fraction(valuation, end, c.deposit_day_count),
                                  Compounding::Simple)
                     .value;
        } else if (q.instrument == Instrument::ContinuousZero) {
            df = rate_to_discount(q.rate.value, year_fraction(valuation, end, DayCount::Actual365Fixed),
                                  Compounding::Continuous)
                     .value;
        } else if (q.instrument == Instrument::Swap) {
            // All coupons, including those between the last known and new pillar,
            // use precisely the same log-linear curve as subsequent pricing.
            const PreparedSwap swap{Swap{valuation, q.maturity, 1.0, q.rate, Side::Payer, c}};
            const auto objective = [&](double log_df) {
                auto candidate = nodes;
                candidate.push_back({end, DiscountFactor{std::exp(log_df)}});
                return price(DiscountCurve{valuation, candidate}, swap).par_rate - q.rate.value;
            };
            double width = 0.25;
            while (width < 50 && same_sign(objective(-width), objective(width)))
                width = std::min(50.0, width * 2);
            try {
                const auto root = bisect(objective, -width, width, settings_);
                df = std::exp(root.root);
                iterations = root.iterations;
            } catch (const std::exception &error) {
                throw std::runtime_error("Calibration failed for " + q.label + ": " + error.what());
            }
        } else
            throw std::invalid_argument("Unsupported market instrument");
        nodes.push_back({end, DiscountFactor{df}});
        rows.push_back({q, end, 0, 0, iterations, false});
    }
    DiscountCurve curve{valuation, nodes};
    for (auto &row : rows) {
        if (row.quote.instrument == Instrument::Deposit)
            row.model_quote = discount_to_rate(curve.discount(row.pillar),
                                               year_fraction(valuation, row.pillar, c.deposit_day_count),
                                               Compounding::Simple);
        else if (row.quote.instrument == Instrument::ContinuousZero)
            row.model_quote = curve.zero(row.pillar).value;
        else
            row.model_quote =
                price(curve, Swap{valuation, row.quote.maturity, 1.0, row.quote.rate, Side::Payer, c})
                    .par_rate;
        row.residual = row.model_quote - row.quote.rate.value;
        row.converged = std::abs(row.residual) <= settings_.quote_tolerance;
        if (!row.converged)
            throw std::runtime_error("Final curve failed repricing tolerance: " + row.quote.label);
    }
    return {std::move(curve), std::move(rows)};
}
} // namespace rates
