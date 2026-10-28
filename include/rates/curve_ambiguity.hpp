#pragma once
#include "rates/curve_bootstrapper.hpp"
#include <optional>

namespace rates {
// Decimal-rate intervals. The quote's rate is metadata; bid/ask define the constraints.
struct QuoteBand {
    MarketQuote quote;
    double bid;
    double ask;
};
// Continuous ACT/365F forwards, explicitly supplied modelling assumptions.
struct ForwardBounds {
    double lower;
    double upper;
};
struct LinearConstraint {
    std::string label;
    std::vector<double> coefficients;
    double upper;
};
// Optimizer-independent LP: minimize/maximize objective dot discounts, subject to
// inequalities and D(valuation)=1. Grid contains every relevant cash-flow date.
struct CurveAmbiguityProblem {
    Date valuation;
    std::vector<Date> dates;
    std::vector<LinearConstraint> constraints;
    std::vector<double> objective;
    ForwardBounds forwards;
    DiscountCurve witness(const std::vector<double> &discounts, double tolerance = 1e-9) const;
};
// Quotes constrain a single discount curve. An optional fixed projection curve
// changes target cash flows only; it does not imply joint multi-curve bounds.
CurveAmbiguityProblem curve_ambiguity_problem(Date valuation, const std::vector<QuoteBand> &quotes,
                                              const std::vector<Swap> &portfolio, ForwardBounds forwards,
                                              const Conventions &quote_conventions = {},
                                              const std::optional<DiscountCurve> &projection = std::nullopt);
} // namespace rates
