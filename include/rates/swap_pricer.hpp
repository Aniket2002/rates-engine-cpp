#pragma once
#include "rates/discount_curve.hpp"
#include "rates/swap.hpp"
#include <memory>

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
    const std::vector<AccrualPeriod> &fixed_periods() const { return schedules_->fixed; }
    const std::vector<AccrualPeriod> &floating_periods() const { return schedules_->floating; }
    PreparedSwap with_economics(double notional, SimpleRate coupon, Side side) const;

  private:
    Swap trade_;
    struct Schedules {
        std::vector<AccrualPeriod> fixed;
        std::vector<AccrualPeriod> floating;
    };
    std::shared_ptr<const Schedules> schedules_;
    PreparedSwap(Swap trade, std::shared_ptr<const Schedules> schedules);
};
SwapResult price(const DiscountCurve &curve, const Swap &swap);
SwapResult price(const DiscountCurve &curve, const PreparedSwap &swap);
} // namespace rates
