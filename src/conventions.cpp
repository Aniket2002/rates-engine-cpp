#include "rates/conventions.hpp"
#include <algorithm>

namespace rates {
YearFraction year_fraction(Date a, Date b, DayCount dc) {
    if (b < a)
        throw std::invalid_argument("Day count requires end >= start");
    switch (dc) {
    case DayCount::Actual360:
        return YearFraction{(b - a) / 360.0};
    case DayCount::Actual365Fixed:
        return YearFraction{(b - a) / 365.0};
    case DayCount::ThirtyE360:
        return YearFraction{
            (360 * (b.year() - a.year()) + 30 * (static_cast<int>(b.month()) - static_cast<int>(a.month())) +
             static_cast<int>(std::min(b.day(), 30U)) - static_cast<int>(std::min(a.day(), 30U))) /
            360.0};
    }
    throw std::invalid_argument("Invalid day-count convention");
}
DiscountFactor rate_to_discount(double r, YearFraction t, Compounding c) {
    if (!std::isfinite(r) || !std::isfinite(t.value) || t.value <= 0)
        throw std::invalid_argument("Conversion requires finite rate and positive time");
    double df = 0;
    if (c == Compounding::Simple)
        df = 1.0 / (1 + r * t.value);
    else if (c == Compounding::Continuous)
        df = std::exp(-r * t.value);
    else
        throw std::invalid_argument("Invalid compounding convention");
    if (!std::isfinite(df) || df <= 0)
        throw std::invalid_argument("Rate implies invalid discount factor");
    return DiscountFactor{df};
}
double discount_to_rate(DiscountFactor df, YearFraction t, Compounding c) {
    if (!std::isfinite(df.value) || df.value <= 0 || !std::isfinite(t.value) || t.value <= 0)
        throw std::invalid_argument("Conversion requires positive discount and time");
    double r = 0;
    if (c == Compounding::Simple)
        r = std::expm1(-std::log(df.value)) / t.value;
    else if (c == Compounding::Continuous)
        r = -std::log(df.value) / t.value;
    else
        throw std::invalid_argument("Invalid compounding convention");
    if (!std::isfinite(r))
        throw std::overflow_error("Rate conversion overflow");
    return r;
}
void Conventions::validate() const {
    if (fixed_months <= 0 || fixed_months > 120 || floating_months <= 0 || floating_months > 120)
        throw std::invalid_argument("Payment interval must be 1..120 calendar months");
    const Date a{2025, 1, 2};
    (void)year_fraction(a, a.add_days(1), fixed_day_count);
    (void)year_fraction(a, a.add_days(1), floating_day_count);
    (void)year_fraction(a, a.add_days(1), deposit_day_count);
    (void)calendar.adjust(a, business_day);
}
} // namespace rates
