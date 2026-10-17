#pragma once
#include "rates/date.hpp"
#include <cmath>
#include <stdexcept>

namespace rates {
// Strong scalar types. Rates use decimal units; BasisPoints{1} = 0.0001.
template <class Tag> struct Quantity {
    double value;
    explicit Quantity(double v) : value(v) {
        if (!std::isfinite(v))
            throw std::invalid_argument("Quantity must be finite");
    }
};
using DiscountFactor = Quantity<struct DiscountFactorTag>;
using ZeroRate = Quantity<struct ZeroRateTag>;
using SimpleRate = Quantity<struct SimpleRateTag>;
using YearFraction = Quantity<struct YearFractionTag>;
using BasisPoints = Quantity<struct BasisPointsTag>;
enum class DayCount : unsigned char { Actual360, Actual365Fixed, ThirtyE360 };
enum class Compounding : unsigned char { Simple, Continuous };
YearFraction year_fraction(Date start, Date end, DayCount convention);
DiscountFactor rate_to_discount(double decimal_rate, YearFraction time, Compounding compounding);
double discount_to_rate(DiscountFactor discount, YearFraction time, Compounding compounding);

struct Conventions {
    int fixed_months = 6;
    int floating_months = 3;
    DayCount fixed_day_count = DayCount::ThirtyE360;
    DayCount floating_day_count = DayCount::Actual360;
    DayCount deposit_day_count = DayCount::Actual360;
    BusinessDay business_day = BusinessDay::ModifiedFollowing;
    bool end_of_month = true;
    Calendar calendar{};
    void validate() const;
};
} // namespace rates
