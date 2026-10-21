#pragma once
#include "rates/history.hpp"
#include "rates/multicurve.hpp"
#include <map>

namespace rates {
struct FixingRecord {
    Date date;
    Timestamp available_at;
    SimpleRate rate;
    std::string source;
    int revision = 0;
};
// Immutable revisions; availability is when the observation became usable by the caller.
class FixingStore {
  public:
    explicit FixingStore(std::vector<FixingRecord> records = {});
    std::map<Date, double> as_of(Timestamp query) const;

  private:
    std::vector<FixingRecord> records_;
};
struct SofrConventions {
    int payment_months = 12;
    int payment_lag_days = 2;
    int settlement_days = 2;
    int lookback_days = 0;
    int lockout_days = 0;
    bool observation_shift = false;
    bool end_of_month = true;
    DayCount fixed_day_count = DayCount::Actual360;
    BusinessDay adjustment = BusinessDay::ModifiedFollowing;
    Calendar calendar{};
    Calendar payment_calendar{};
    void validate() const;
};
struct SofrSwap {
    Date effective;
    Date maturity;
    double notional;
    SimpleRate fixed_rate;
    Side side = Side::Payer;
    SofrConventions conventions{};
};
SofrSwap spot_sofr_swap(Date trade_date, int tenor_months, double notional, SimpleRate coupon,
                        Side side = Side::Payer, const SofrConventions &conventions = {});
struct OvernightObservation {
    Date accrual_start;
    Date accrual_end;
    Date fixing_date;
    Date observation_end;
    double weight;
    double rate;
    bool realized;
};
struct SofrCoupon {
    AccrualPeriod period;
    double fixed_amount;
    double floating_amount;
    double discount;
    double fixed_accrued;
    double floating_accrued;
    std::vector<OvernightObservation> observations;
};
struct SofrResult {
    double fixed_pv = 0;
    double floating_pv = 0;
    double pv = 0;                   // Dirty NPV at valuation, signed for payer/receiver.
    double accrued = 0;              // Undiscounted signed accrued unpaid coupons at valuation.
    double clean_pv = 0;             // Defined as dirty NPV minus accrued.
    std::optional<double> par_rate;  // No par rate when all coupons have settled.
    std::vector<SofrCoupon> coupons; // Only outstanding payments, including matured/unpaid coupons.
    std::size_t settled_coupons = 0;
};
// Past observations must have an available fixing; never forecast missing history.
// Payments on valuation date are excluded by default (end-of-day settlement convention).
SofrResult price_sofr(const DiscountCurve &discount, const DiscountCurve &projection, const SofrSwap &swap,
                      const FixingStore &fixings, Timestamp as_of,
                      bool include_valuation_date_payments = false);
} // namespace rates
