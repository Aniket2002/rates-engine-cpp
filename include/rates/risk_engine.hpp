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
struct CurveShock {
    std::string label;
    Date maturity;
    DiscountCurve down;
    DiscountCurve up;
};
// Immutable snapshot of quotes/conventions/bump. Rebuild when inputs change; no global cache.
class RiskScenarios {
  public:
    const DiscountCurve &base_curve() const { return base_; }
    const CurveShock &parallel() const { return parallel_; }
    const std::vector<CurveShock> &quote_shocks() const { return quotes_; }
    const std::vector<CurveShock> &zero_shocks() const { return zeros_; }
    std::size_t calibration_count() const { return calibrations_; }
    RiskReport report(const Swap &trade, std::string curve_id) const;
    std::vector<RiskReport> portfolio(const std::vector<Swap> &trades, const std::string &curve_id) const;

  private:
    friend class RiskEngine;
    RiskScenarios(DiscountCurve base, CurveShock parallel, std::vector<CurveShock> quotes,
                  std::vector<CurveShock> zeros, double bump_bps, double tolerance, std::size_t calibrations);
    DiscountCurve base_;
    CurveShock parallel_;
    std::vector<CurveShock> quotes_;
    std::vector<CurveShock> zeros_;
    double bump_bps_;
    double tolerance_;
    std::size_t calibrations_;
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
    RiskScenarios prepare_scenarios(Date valuation, const std::vector<MarketQuote> &quotes,
                                    BasisPoints bump = BasisPoints{1}) const;
    std::vector<RiskReport> portfolio_report(Date valuation, const std::vector<MarketQuote> &quotes,
                                             const std::vector<Swap> &trades, const std::string &curve_id,
                                             BasisPoints bump = BasisPoints{1}) const;
    // Parallel-only portfolio path calibrates two curves regardless of trade count.
    std::vector<double> portfolio_parallel_dv01(Date valuation, const std::vector<MarketQuote> &quotes,
                                                const std::vector<Swap> &trades,
                                                BasisPoints bump = BasisPoints{1}) const;

  private:
    CurveBootstrapper bootstrapper_;
};
} // namespace rates
