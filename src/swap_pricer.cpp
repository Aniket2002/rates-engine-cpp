#include "rates/swap_pricer.hpp"

namespace rates {
SwapResult price(const DiscountCurve &curve, const Swap &swap) {
    swap.validate(curve.valuation_date());
    const auto &c = swap.conventions;
    const auto fixed = make_schedule(swap.effective, swap.maturity, c.fixed_months, c.fixed_day_count,
                                     c.calendar, c.business_day, c.end_of_month);
    const auto floating = make_schedule(swap.effective, swap.maturity, c.floating_months,
                                        c.floating_day_count, c.calendar, c.business_day, c.end_of_month);
    SwapResult result{};
    for (const auto &period : fixed) {
        const double df = curve.discount(period.payment).value;
        const double amount = swap.notional * swap.fixed_rate.value * period.accrual.value;
        result.annuity += period.accrual.value * df;
        result.fixed_pv += amount * df;
        result.fixed_flows.push_back({period, swap.fixed_rate.value, amount, df, amount * df});
    }
    for (const auto &period : floating) {
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
