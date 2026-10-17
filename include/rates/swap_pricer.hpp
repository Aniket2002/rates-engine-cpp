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
// Owns a validated trade and schedules; reusable across curves without stale references.
class PreparedSwap {
  public:
    explicit PreparedSwap(Swap trade);
    const Swap &trade() const { return trade_; }
    const std::vector<AccrualPeriod> &fixed_periods() const { return fixed_; }
    const std::vector<AccrualPeriod> &floating_periods() const { return floating_; }

  private:
    Swap trade_;
    std::vector<AccrualPeriod> fixed_;
    std::vector<AccrualPeriod> floating_;
};
SwapResult price(const DiscountCurve &curve, const Swap &swap);
SwapResult price(const DiscountCurve &curve, const PreparedSwap &swap);
} // namespace rates
