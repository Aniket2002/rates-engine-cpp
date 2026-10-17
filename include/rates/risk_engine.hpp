#pragma once
#include "rates/curve_bootstrapper.hpp"

namespace rates {
struct RiskBucket {
    std::string label;
    Date maturity;
    double dv01;
};
struct RiskReport {
    Date valuation;
    std::string curve_id;
    Swap trade;
    SwapResult base;
    double parallel_quote_dv01;
    std::vector<RiskBucket> quote_buckets;
    std::vector<RiskBucket> zero_nodes;
    double bump_bps;
    double calibration_tolerance;
};
class RiskEngine {
  public:
    explicit RiskEngine(CurveBootstrapper bootstrapper = CurveBootstrapper{});
    // All results: currency/bp, signed (PV_down-PV_up)/(2*bump_bps).
    double parallel_quote_dv01(Date valuation, const std::vector<MarketQuote> &quotes, const Swap &trade,
                               BasisPoints bump = BasisPoints{1}) const;
    std::vector<RiskBucket> quote_risk(Date valuation, const std::vector<MarketQuote> &quotes,
                                       const Swap &trade, BasisPoints bump = BasisPoints{1}) const;
    std::vector<RiskBucket> zero_risk(const DiscountCurve &curve, const Swap &trade,
                                      BasisPoints bump = BasisPoints{1}) const;
    RiskReport report(Date valuation, const std::vector<MarketQuote> &quotes, const Swap &trade,
                      std::string curve_id, BasisPoints bump = BasisPoints{1}) const;

  private:
    CurveBootstrapper bootstrapper_;
};
} // namespace rates
