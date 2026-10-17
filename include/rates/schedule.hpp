#pragma once
#include "rates/conventions.hpp"
#include <vector>

namespace rates {
struct AccrualPeriod {
    Date start;
    Date end;
    Date payment;
    YearFraction accrual;
};
// Forward generation, anchored on original effective date; short final stub.
// Accrual boundaries and payments are adjusted together; payment lag is zero.
std::vector<AccrualPeriod> make_schedule(Date effective, Date maturity, int months, DayCount day_count,
                                         const Calendar &calendar, BusinessDay adjustment,
                                         bool end_of_month = true);
} // namespace rates
