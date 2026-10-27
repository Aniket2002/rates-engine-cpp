#pragma once
#include "rates/swap_pricer.hpp"
#include <functional>

namespace rates {
struct SolverSettings {
    double quote_tolerance = 1e-12; // Decimal rate residual, not currency PV.
    double log_discount_tolerance = 1e-14;
    int max_iterations = 200;
};
struct RootResult {
    double root;
    int iterations;
    double residual;
};
RootResult bisect(const std::function<double(double)> &function, double lower, double upper,
                  const SolverSettings &settings);
// Bracket-preserving secant steps with bisection fallback; residual tolerance is mandatory.
RootResult safeguarded_secant(const std::function<double(double)> &function, double lower, double upper,
                              const SolverSettings &settings);
struct CalibrationRow {
    MarketQuote quote;
    Date pillar;
    double model_quote;
    double residual;
    int iterations;
    bool converged;
};
struct BootstrapResult {
    DiscountCurve curve;
    std::vector<CalibrationRow> diagnostics;
};
// Shared by calibration and risk metadata; zero quotes are exact-date observations.
Date quote_pillar(const MarketQuote &quote, const Conventions &conventions);
class CurveBootstrapper {
  public:
    explicit CurveBootstrapper(Conventions conventions = {}, SolverSettings settings = {});
    BootstrapResult build(Date valuation, std::vector<MarketQuote> quotes) const;
    // Calibrate projection factors while discounting swap coupons on an exogenous curve.
    BootstrapResult build_forward(const DiscountCurve &discount, std::vector<MarketQuote> quotes) const;
    const Conventions &conventions() const { return conventions_; }
    const SolverSettings &settings() const { return settings_; }

  private:
    BootstrapResult build_impl(Date valuation, std::vector<MarketQuote> quotes,
                               const DiscountCurve *discount) const;
    Conventions conventions_;
    SolverSettings settings_;
};
} // namespace rates
