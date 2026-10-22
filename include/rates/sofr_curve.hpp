#pragma once
#include "rates/sofr.hpp"

namespace rates {
struct SofrQuote {
    std::string label;
    int tenor_months;
    SimpleRate rate;
};
struct SofrCalibrationRow {
    SofrQuote quote;
    Date effective;
    Date pillar; // Last payment, including its business-day lag.
    double model_quote;
    double residual;
    int iterations;
};
struct SofrBootstrapResult {
    DiscountCurve curve;
    std::vector<SofrCalibrationRow> diagnostics;
};
class SofrCurveBootstrapper {
  public:
    explicit SofrCurveBootstrapper(SofrConventions conventions = {}, SolverSettings settings = {});
    SofrBootstrapResult build(Timestamp as_of, std::vector<SofrQuote> quotes,
                              const FixingStore &fixings = FixingStore{}) const;
    SofrBootstrapResult build_forward(const DiscountCurve &discount, Timestamp as_of,
                                      std::vector<SofrQuote> quotes,
                                      const FixingStore &fixings = FixingStore{}) const;
    const SofrConventions &conventions() const { return conventions_; }

  private:
    SofrBootstrapResult build_impl(Timestamp as_of, std::vector<SofrQuote> quotes, const FixingStore &fixings,
                                   const DiscountCurve *discount) const;
    SofrConventions conventions_;
    SolverSettings settings_;
};
struct SofrQuoteRisk {
    SofrResult base;
    double parallel_dv01;
    std::vector<RiskBucket> quote_buckets;
};
// Single overnight curve, full quote rebootstrap; realized fixings are never shocked.
SofrQuoteRisk sofr_quote_risk(const SofrCurveBootstrapper &builder, const std::vector<SofrQuote> &quotes,
                              const SofrSwap &swap, const FixingStore &fixings, Timestamp as_of,
                              BasisPoints bump = BasisPoints{1});
struct SofrCurveRisk {
    SofrResult base;
    std::vector<RiskBucket> discount_zero;
    std::vector<RiskBucket> projection_zero;
};
struct SofrMultiCurveQuoteRisk {
    SofrResult base;
    double discount_parallel_dv01;
    double projection_parallel_dv01;
    std::vector<RiskBucket> discount_quotes;
    std::vector<RiskBucket> projection_quotes;
    double bump_bps;
};
// Discount quote shocks rebuild both curves with projection quotes held fixed.
// Projection quote shocks hold discounting fixed. All realized fixings remain frozen.
SofrMultiCurveQuoteRisk sofr_multi_curve_quote_risk(const SofrCurveBootstrapper &discount_builder,
                                                    const SofrCurveBootstrapper &projection_builder,
                                                    const std::vector<SofrQuote> &discount_quotes,
                                                    const std::vector<SofrQuote> &projection_quotes,
                                                    const SofrSwap &swap, const FixingStore &fixings,
                                                    Timestamp as_of, BasisPoints bump = BasisPoints{1},
                                                    bool include_valuation_date_payments = false);
// Independent zero-node coordinates, holding the other curve and realized fixings fixed.
SofrCurveRisk sofr_curve_risk(const DiscountCurve &discount, const DiscountCurve &projection,
                              const SofrSwap &swap, const FixingStore &fixings, Timestamp as_of,
                              BasisPoints bump = BasisPoints{1});
} // namespace rates
