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
class CurveBootstrapper {
  public:
    explicit CurveBootstrapper(Conventions conventions = {}, SolverSettings settings = {});
    BootstrapResult build(Date valuation, std::vector<MarketQuote> quotes) const;
    const Conventions &conventions() const { return conventions_; }
    const SolverSettings &settings() const { return settings_; }

  private:
    Conventions conventions_;
    SolverSettings settings_;
};
} // namespace rates
