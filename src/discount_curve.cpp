#include "rates/discount_curve.hpp"
#include <algorithm>
#include <utility>

namespace rates {
DiscountCurve::DiscountCurve(Date valuation, const std::vector<CurveNode> &pillars) : valuation_(valuation) {
    if (pillars.empty())
        throw std::invalid_argument("Curve requires at least one future pillar");
    nodes_.push_back({valuation, DiscountFactor{1}});
    times_.push_back(0);
    log_discounts_.push_back(0);
    for (const auto &node : pillars) {
        if (node.date <= nodes_.back().date)
            throw std::invalid_argument(
                "Curve pillars must be strictly ordered after valuation; duplicates forbidden");
        if (!std::isfinite(node.discount.value) || node.discount.value <= 0)
            throw std::invalid_argument("Curve discount factors must be finite and strictly positive");
        nodes_.push_back(node);
        times_.push_back(year_fraction(valuation, node.date, DayCount::Actual365Fixed).value);
        log_discounts_.push_back(std::log(node.discount.value));
    }
}
DiscountFactor DiscountCurve::discount(Date d) const {
    return discount(year_fraction(valuation_, d, DayCount::Actual365Fixed));
}
void DiscountCurve::replace_last_discount(DiscountFactor discount) {
    if (!std::isfinite(discount.value) || discount.value <= 0)
        throw std::invalid_argument("Curve discount factors must be finite and strictly positive");
    nodes_.back().discount = discount;
    log_discounts_.back() = std::log(discount.value);
}
DiscountFactor DiscountCurve::discount(YearFraction t) const {
    if (!std::isfinite(t.value) || t.value < 0 || t.value > times_.back())
        throw std::out_of_range("Discount lookup outside calibrated curve; extrapolation disabled");
    const auto found = std::lower_bound(times_.begin(), times_.end(), t.value);
    const auto j = static_cast<std::size_t>(found - times_.begin());
    if (*found == t.value)
        return nodes_[j].discount;
    const double w = (t.value - times_[j - 1]) / (times_[j] - times_[j - 1]);
    return DiscountFactor{std::exp(std::lerp(log_discounts_[j - 1], log_discounts_[j], w))};
}
ZeroRate DiscountCurve::zero(Date d) const {
    const auto t = year_fraction(valuation_, d, DayCount::Actual365Fixed);
    return ZeroRate{discount_to_rate(discount(d), t, Compounding::Continuous)};
}
SimpleRate DiscountCurve::forward(Date a, Date b, DayCount dc) const {
    const double tau = year_fraction(a, b, dc).value;
    if (tau <= 0)
        throw std::invalid_argument("Forward interval must have positive accrual");
    return SimpleRate{std::expm1(std::log(discount(a).value) - std::log(discount(b).value)) / tau};
}
std::vector<CurveDiagnostic> DiscountCurve::diagnostics() const {
    std::vector<CurveDiagnostic> result;
    for (std::size_t i = 1; i < nodes_.size(); ++i)
        result.push_back({nodes_[i].date, times_[i], nodes_[i].discount.value, -log_discounts_[i] / times_[i],
                          -(log_discounts_[i] - log_discounts_[i - 1]) / (times_[i] - times_[i - 1])});
    return result;
}
DiscountCurve DiscountCurve::shift_zero_node(std::size_t index, BasisPoints shift) const {
    if (index >= nodes_.size() - 1)
        throw std::out_of_range("Zero-node index excludes time-zero anchor");
    std::vector<CurveNode> pillars(nodes_.begin() + 1, nodes_.end());
    pillars[index].discount =
        DiscountFactor{std::exp(log_discounts_[index + 1] - shift.value * 1e-4 * times_[index + 1])};
    return DiscountCurve{valuation_, pillars};
}
} // namespace rates
