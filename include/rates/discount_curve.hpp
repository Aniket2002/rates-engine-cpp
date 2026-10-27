#pragma once
#include "rates/conventions.hpp"
#include <vector>

namespace rates {
struct CurveNode {
    Date date;
    DiscountFactor discount;
};
struct CurveDiagnostic {
    Date date;
    double time;
    double discount;
    double zero;
    double segment_forward;
};
class DiscountCurve {
  public:
    // Pillars exclude the automatically inserted valuation-date P(0)=1 anchor.
    // Times are ACT/365F from valuation. No extrapolation is supported.
    DiscountCurve(Date valuation, const std::vector<CurveNode> &pillars);
    Date valuation_date() const { return valuation_; }
    const std::vector<CurveNode> &nodes() const { return nodes_; }
    DiscountFactor discount(Date date) const;
    DiscountFactor discount(YearFraction time) const;
    ZeroRate zero(Date date) const;
    SimpleRate forward(Date start, Date end, DayCount day_count) const;
    std::vector<CurveDiagnostic> diagnostics() const;
    DiscountCurve shift_zero_node(std::size_t pillar_index, BasisPoints shift) const;

  private:
    // Only calibration may update its unshared trial curve. Public curves stay immutable.
    friend class CurveBootstrapper;
    void replace_last_discount(DiscountFactor discount);
    Date valuation_;
    std::vector<CurveNode> nodes_;
    std::vector<double> times_;
    std::vector<double> log_discounts_;
};
} // namespace rates
