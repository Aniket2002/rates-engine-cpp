#pragma once
#include "rates/discount_curve.hpp"
#include "rates/swap.hpp"

namespace rates {
struct CashFlow {
    AccrualPeriod period;
    double decimal_rate;
    double amount;
    double discount;
    double present_value;
};
struct SwapResult {
    double fixed_pv;
    double floating_pv;
    double pv;
    double par_rate;
    double annuity; // Per unit notional.
    std::vector<CashFlow> fixed_flows;
    std::vector<CashFlow> floating_flows;
};
SwapResult price(const DiscountCurve &curve, const Swap &swap);
} // namespace rates
