#pragma once
#include "rates/risk_engine.hpp"

namespace rates {
// Annual ACT/360, zero payment/settlement lag. Supply the relevant holiday set.
Conventions sofr_ois_conventions(Calendar calendar = Calendar{});
struct OvernightFixing {
    Date date;
    SimpleRate rate;
};
// Complete, ordered business-day observations on [start,end); both endpoints business days.
// Each fixing accrues through the next business day, including intervening weekends/holidays.
SimpleRate compound_sofr(Date start, Date end, const std::vector<OvernightFixing> &fixings,
                         const Calendar &calendar = Calendar{});
class OisCurveBootstrapper {
  public:
    explicit OisCurveBootstrapper(Calendar calendar = Calendar{}, SolverSettings settings = {});
    // Deposit and Swap quotes only; Swap means a compounded overnight indexed swap here.
    BootstrapResult build(Date valuation, std::vector<MarketQuote> quotes) const;

  private:
    CurveBootstrapper bootstrapper_;
};
// Fully future compounded overnight coupons, no observation shift/lookback/lockout or payment lag.
SwapResult price_ois(const DiscountCurve &overnight, const Swap &swap);
struct MultiCurveRiskReport {
    SwapResult base;
    double discount_parallel_dv01;
    double forwarding_parallel_dv01;
    std::vector<RiskBucket> discount_quotes;
    std::vector<RiskBucket> forwarding_quotes;
    double bump_bps;
};
// Discount quote shocks recalibrate BOTH curves, holding forwarding market quotes fixed.
// Forwarding quote shocks hold the calibrated discount curve fixed.
MultiCurveRiskReport multi_curve_risk(const CurveBootstrapper &discount_builder,
                                      const CurveBootstrapper &forwarding_builder, Date valuation,
                                      const std::vector<MarketQuote> &discount_quotes,
                                      const std::vector<MarketQuote> &forwarding_quotes, const Swap &swap,
                                      BasisPoints bump = BasisPoints{1});
} // namespace rates
