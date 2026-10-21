#include "rates/sofr.hpp"
#include <algorithm>
#include <utility>

namespace rates {
FixingStore::FixingStore(std::vector<FixingRecord> records) : records_(std::move(records)) {
    std::sort(records_.begin(), records_.end(), [](const auto &a, const auto &b) {
        return a.date == b.date ? a.available_at < b.available_at : a.date < b.date;
    });
    for (std::size_t i = 0; i < records_.size(); ++i) {
        const auto &row = records_[i];
        if (row.date >= row.available_at.date() || !std::isfinite(row.rate.value) || row.source.empty())
            throw std::invalid_argument("Invalid SOFR fixing date, availability, rate or source");
        const bool revision = i > 0 && row.date == records_[i - 1].date;
        if ((!revision && row.revision != 0) ||
            (revision &&
             (row.revision != records_[i - 1].revision + 1 ||
              row.available_at <= records_[i - 1].available_at || row.source != records_[i - 1].source)))
            throw std::invalid_argument("SOFR revisions must be consecutive with increasing availability");
    }
}
std::map<Date, double> FixingStore::as_of(Timestamp query) const {
    std::map<Date, double> result;
    for (const auto &record : records_)
        if (record.available_at <= query)
            result.insert_or_assign(record.date, record.rate.value);
    return result;
}
void SofrConventions::validate() const {
    if (payment_months < 1 || payment_months > 120 || payment_lag_days < 0 || payment_lag_days > 30 ||
        settlement_days < 0 || settlement_days > 30 || lookback_days < 0 || lookback_days > 30 ||
        lockout_days < 0 || lockout_days > 30)
        throw std::invalid_argument("Invalid SOFR payment interval or business-day lag");
    (void)year_fraction(Date{2025, 1, 2}, Date{2025, 2, 3}, fixed_day_count);
    (void)calendar.adjust(Date{2025, 1, 2}, adjustment);
}
SofrSwap spot_sofr_swap(Date trade_date, int tenor_months, double notional, SimpleRate coupon, Side side,
                        const SofrConventions &conventions) {
    conventions.validate();
    if (!conventions.calendar.is_business_day(trade_date) || tenor_months <= 0)
        throw std::invalid_argument("Spot SOFR trade requires a business trade date and positive tenor");
    const auto effective = conventions.calendar.advance(trade_date, conventions.settlement_days);
    return {effective,  effective.add_months(tenor_months, conventions.end_of_month), notional, coupon, side,
            conventions};
}
namespace {
std::vector<OvernightObservation> observation_schedule(const AccrualPeriod &period,
                                                       const SofrConventions &c) {
    if (!c.calendar.is_business_day(period.start) || !c.calendar.is_business_day(period.end))
        throw std::invalid_argument("SOFR accrual boundaries must be index business days");
    std::vector<OvernightObservation> result;
    for (Date start = period.start; start < period.end;) {
        const auto end = c.calendar.advance(start, 1);
        const auto observation_start = c.calendar.advance(start, -c.lookback_days);
        const auto observation_end = c.calendar.advance(end, -c.lookback_days);
        const double weight =
            static_cast<double>(c.observation_shift ? observation_end - observation_start : end - start) /
            360;
        result.push_back({start, end, observation_start, observation_end, weight, 0, false});
        start = end;
    }
    const auto lockout = static_cast<std::size_t>(c.lockout_days);
    if (lockout >= result.size())
        throw std::invalid_argument("SOFR lockout must be shorter than the coupon's business-day count");
    if (lockout > 0) {
        const auto anchor = result[result.size() - lockout - 1];
        for (std::size_t i = result.size() - lockout; i < result.size(); ++i) {
            result[i].fixing_date = anchor.fixing_date;
            result[i].observation_end = anchor.observation_end;
        }
    }
    return result;
}
double compound(const std::vector<OvernightObservation> &observations, Date cutoff) {
    double log_factor = 0;
    for (const auto &observation : observations) {
        if (cutoff <= observation.accrual_start)
            break;
        // Partial accrual prorates the observation's weight within its interest interval.
        const double portion =
            static_cast<double>(std::min(cutoff, observation.accrual_end) - observation.accrual_start) /
            static_cast<double>(observation.accrual_end - observation.accrual_start);
        const double interest = observation.rate * observation.weight * portion;
        if (!std::isfinite(interest) || interest <= -1)
            throw std::invalid_argument("SOFR observation produces a nonpositive or non-finite factor");
        log_factor += std::log1p(interest);
    }
    return std::expm1(log_factor);
}
} // namespace
SofrResult price_sofr(const DiscountCurve &discount, const DiscountCurve &projection, const SofrSwap &swap,
                      const FixingStore &fixings, Timestamp as_of, bool include_valuation_date_payments) {
    const auto valuation = as_of.date();
    if (discount.valuation_date() != valuation || projection.valuation_date() != valuation)
        throw std::invalid_argument("SOFR curves and as-of date must have the same valuation date");
    const auto &c = swap.conventions;
    c.validate();
    if (!std::isfinite(swap.notional) || swap.notional <= 0 || !std::isfinite(swap.fixed_rate.value) ||
        (swap.side != Side::Payer && swap.side != Side::Receiver))
        throw std::invalid_argument("Invalid SOFR swap economics");
    const auto periods = make_schedule(swap.effective, swap.maturity, c.payment_months, c.fixed_day_count,
                                       c.calendar, c.adjustment, c.end_of_month);
    const auto known = fixings.as_of(as_of);
    SofrResult result;
    double annuity = 0;
    const double sign = swap.side == Side::Payer ? 1 : -1;
    for (auto period : periods) {
        period.payment = c.payment_calendar.advance(period.end, c.payment_lag_days);
        if (period.payment < valuation || (period.payment == valuation && !include_valuation_date_payments)) {
            ++result.settled_coupons;
            continue;
        }
        auto observations = observation_schedule(period, c);
        for (auto &observation : observations) {
            const auto found = known.find(observation.fixing_date);
            if (found != known.end()) {
                observation.rate = found->second;
                observation.realized = true;
            } else {
                if (observation.fixing_date < valuation)
                    throw MarketDataError(MarketDataFailure::Missing, "Missing available SOFR fixing for " +
                                                                          observation.fixing_date.str());
                observation.rate =
                    projection
                        .forward(observation.fixing_date, observation.observation_end, DayCount::Actual360)
                        .value;
            }
        }
        const double floating_amount = swap.notional * compound(observations, period.end);
        const double fixed_amount = swap.notional * swap.fixed_rate.value * period.accrual.value;
        const double df = discount.discount(period.payment).value;
        const auto accrued_end = std::max(period.start, std::min(valuation, period.end));
        const double fixed_accrued = swap.notional * swap.fixed_rate.value *
                                     year_fraction(period.start, accrued_end, c.fixed_day_count).value;
        const double floating_accrued = swap.notional * compound(observations, accrued_end);
        result.fixed_pv += fixed_amount * df;
        result.floating_pv += floating_amount * df;
        result.accrued += sign * (floating_accrued - fixed_accrued);
        annuity += period.accrual.value * df;
        result.coupons.push_back({period, fixed_amount, floating_amount, df, fixed_accrued, floating_accrued,
                                  std::move(observations)});
    }
    result.pv = sign * (result.floating_pv - result.fixed_pv);
    result.clean_pv = result.pv - result.accrued;
    if (annuity > 0)
        result.par_rate = result.floating_pv / swap.notional / annuity;
    if (!std::isfinite(result.pv) || !std::isfinite(result.accrued) || !std::isfinite(result.clean_pv) ||
        !std::isfinite(result.fixed_pv) || !std::isfinite(result.floating_pv) ||
        (result.par_rate && !std::isfinite(*result.par_rate)))
        throw std::overflow_error("SOFR valuation produced a non-finite result");
    return result;
}
} // namespace rates
