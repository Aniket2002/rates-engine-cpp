#include "rates/swap_pricer.hpp"
#include <utility>

namespace rates {
PreparedSwap::PreparedSwap(Swap trade) : trade_(std::move(trade)) {
    trade_.validate(trade_.effective);
    const auto &c = trade_.conventions;
    schedules_ = std::make_shared<Schedules>(
        Schedules{make_schedule(trade_.effective, trade_.maturity, c.fixed_months, c.fixed_day_count,
                                c.calendar, c.business_day, c.end_of_month),
                  make_schedule(trade_.effective, trade_.maturity, c.floating_months, c.floating_day_count,
                                c.calendar, c.business_day, c.end_of_month)});
}
PreparedSwap::PreparedSwap(Swap trade, std::shared_ptr<const Schedules> schedules)
    : trade_(std::move(trade)), schedules_(std::move(schedules)) {
    trade_.validate(trade_.effective);
}
PreparedSwap PreparedSwap::with_economics(double notional, SimpleRate coupon, Side side) const {
    auto changed = trade_;
    changed.notional = notional;
    changed.fixed_rate = coupon;
    changed.side = side;
    return PreparedSwap{std::move(changed), schedules_};
}
SwapResult price(const DiscountCurve &curve, const Swap &swap) {
    return price(curve, PreparedSwap{swap});
}
SwapResult price(const DiscountCurve &curve, const PreparedSwap &prepared) {
    const auto &swap = prepared.trade();
    if (swap.effective < curve.valuation_date())
        throw std::invalid_argument(
            "Historical fixings/accrued swaps are unsupported: effective date precedes valuation");
    const auto &c = swap.conventions;
    SwapResult result{};
    result.fixed_flows.reserve(prepared.fixed_periods().size());
    result.floating_flows.reserve(prepared.floating_periods().size());
    for (const auto &period : prepared.fixed_periods()) {
        const double df = curve.discount(period.payment).value;
        const double amount = swap.notional * swap.fixed_rate.value * period.accrual.value;
        result.annuity += period.accrual.value * df;
        result.fixed_pv += amount * df;
        result.fixed_flows.push_back({period, swap.fixed_rate.value, amount, df, amount * df});
    }
    for (const auto &period : prepared.floating_periods()) {
        const double fwd = curve.forward(period.start, period.end, c.floating_day_count).value;
        const double df = curve.discount(period.payment).value;
        const double amount = swap.notional * fwd * period.accrual.value;
        result.floating_pv += amount * df;
        result.floating_flows.push_back({period, fwd, amount, df, amount * df});
    }
    result.par_rate = (result.floating_pv / swap.notional) / result.annuity;
    result.pv = (swap.side == Side::Payer ? 1 : -1) * (result.floating_pv - result.fixed_pv);
    if (!std::isfinite(result.pv) || !std::isfinite(result.par_rate) || !std::isfinite(result.fixed_pv) ||
        !std::isfinite(result.floating_pv))
        throw std::overflow_error("Swap valuation produced a non-finite result");
    return result;
}
} // namespace rates
