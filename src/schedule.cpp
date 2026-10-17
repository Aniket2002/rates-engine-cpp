#include "rates/schedule.hpp"
#include "rates/swap.hpp"

namespace rates {
std::vector<AccrualPeriod> make_schedule(Date effective, Date maturity, int months, DayCount dc,
                                         const Calendar &calendar, BusinessDay adjustment, bool eom) {
    if (maturity <= effective)
        throw std::invalid_argument("Maturity must follow effective date");
    if (months <= 0 || months > 120)
        throw std::invalid_argument("Payment interval must be 1..120 calendar months");
    if (calendar.adjust(effective, adjustment) != effective)
        throw std::invalid_argument("Effective date must already satisfy business-day convention");
    std::vector<AccrualPeriod> periods;
    Date previous = effective;
    for (int offset = months;; offset += months) {
        // Avoid computing a tenor date beyond the supported date domain for a final stub.
        const int maturity_months = 12 * (maturity.year() - effective.year()) +
                                    static_cast<int>(maturity.month()) - static_cast<int>(effective.month());
        Date raw = offset > maturity_months ? maturity : effective.add_months(offset, eom);
        if (raw > maturity)
            raw = maturity;
        const Date end = calendar.adjust(raw, adjustment);
        if (end <= previous)
            throw std::invalid_argument(
                "Business-day adjustment produced duplicate or reversed accrual dates");
        const auto accrual = year_fraction(previous, end, dc);
        if (accrual.value <= 0)
            throw std::invalid_argument("Accrual fraction must be positive");
        periods.push_back({previous, end, end, accrual});
        if (raw == maturity)
            break;
        previous = end;
    }
    return periods;
}
void Swap::validate(Date valuation) const {
    if (!std::isfinite(notional) || notional <= 0)
        throw std::invalid_argument("Swap notional must be finite and positive");
    if (!std::isfinite(fixed_rate.value))
        throw std::invalid_argument("Swap coupon must be finite");
    if (effective < valuation)
        throw std::invalid_argument(
            "Historical fixings/accrued swaps are unsupported: effective date precedes valuation");
    if (maturity <= effective)
        throw std::invalid_argument("Swap maturity must follow effective date");
    if (side != Side::Payer && side != Side::Receiver)
        throw std::invalid_argument("Invalid swap side");
    conventions.validate();
}
} // namespace rates
