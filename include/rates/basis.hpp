#pragma once
#include "rates/sofr_curve.hpp"

namespace rates {
// Same-currency compounded overnight basis: payer pays base + simple spread,
// receives other. Each index has its own projection, conventions and fixing store.
struct BasisSwap {
    Date effective;
    Date maturity;
    double notional;
    SimpleRate spread;
    Side side = Side::Payer;
    SofrConventions base_conventions{};
    SofrConventions other_conventions{};
};
struct BasisResult {
    SofrResult base_leg;
    SofrResult other_leg;
    double spread_pv;
    double pv;
    double accrued;
    double clean_pv;
    std::optional<double> par_spread;
};
BasisResult price_basis(const DiscountCurve &discount, const DiscountCurve &base_projection,
                        const DiscountCurve &other_projection, const BasisSwap &swap,
                        const FixingStore &base_fixings, const FixingStore &other_fixings, Timestamp as_of,
                        bool include_valuation_date_payments = false);
struct BasisQuote {
    std::string label;
    int tenor_months;
    SimpleRate spread;
};
struct BasisCalibrationRow {
    BasisQuote quote;
    Date effective;
    Date pillar;
    double model_quote;
    double residual;
    int iterations;
};
struct BasisBootstrapResult {
    DiscountCurve curve;
    std::vector<BasisCalibrationRow> diagnostics;
};
class BasisCurveBootstrapper {
  public:
    explicit BasisCurveBootstrapper(SofrConventions base = {}, SofrConventions other = {},
                                    SolverSettings settings = {});
    BasisSwap instrument(Date trade_date, int tenor_months, double notional, SimpleRate spread,
                         Side side = Side::Payer) const;
    // Calibrate the OTHER projection from basis quotes; discount/base curves are exogenous.
    BasisBootstrapResult build(const DiscountCurve &discount, const DiscountCurve &base_projection,
                               Timestamp as_of, std::vector<BasisQuote> quotes,
                               const FixingStore &base_fixings = FixingStore{},
                               const FixingStore &other_fixings = FixingStore{}) const;

  private:
    SofrConventions base_, other_;
    SolverSettings settings_;
};
struct BasisQuoteRisk {
    BasisResult base;
    double discount_parallel_dv01;
    double base_parallel_dv01;
    double basis_parallel_dv01;
    std::vector<RiskBucket> discount_quotes;
    std::vector<RiskBucket> base_quotes;
    std::vector<RiskBucket> basis_quotes;
    double bump_bps;
};
// Rebuild all downstream curves for each independent quote coordinate.
BasisQuoteRisk
basis_quote_risk(const SofrCurveBootstrapper &discount_builder, const SofrCurveBootstrapper &base_builder,
                 const BasisCurveBootstrapper &basis_builder, const std::vector<SofrQuote> &discount_quotes,
                 const std::vector<SofrQuote> &base_quotes, const std::vector<BasisQuote> &basis_quotes,
                 const BasisSwap &swap, const FixingStore &discount_fixings, const FixingStore &base_fixings,
                 const FixingStore &other_fixings, Timestamp as_of, BasisPoints bump = BasisPoints{1},
                 bool include_valuation_date_payments = false);
} // namespace rates
