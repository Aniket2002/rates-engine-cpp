#include "rates/multicurve.hpp"
#include <algorithm>
#include <utility>

namespace rates {
Conventions sofr_ois_conventions(Calendar calendar) {
    Conventions result;
    result.fixed_months = 12;
    result.floating_months = 12;
    result.fixed_day_count = DayCount::Actual360;
    result.floating_day_count = DayCount::Actual360;
    result.calendar = std::move(calendar);
    return result;
}
SimpleRate compound_sofr(Date start, Date end, const std::vector<OvernightFixing> &fixings,
                         const Calendar &calendar) {
    if (start >= end || !calendar.is_business_day(start) || !calendar.is_business_day(end))
        throw std::invalid_argument("SOFR compounding requires increasing business-day endpoints");
    Date expected = start;
    double log_factor = 0;
    for (const auto &fixing : fixings) {
        if (fixing.date != expected || expected >= end)
            throw std::invalid_argument("SOFR fixings must cover every business day in order exactly once");
        const Date next = calendar.adjust(expected.add_days(1), BusinessDay::Following);
        const double accrual = static_cast<double>(next - expected) / 360;
        const double interest = fixing.rate.value * accrual;
        if (!std::isfinite(interest) || interest <= -1)
            throw std::invalid_argument("SOFR fixing produces an invalid daily compound factor");
        log_factor += std::log1p(interest);
        expected = next;
    }
    if (expected != end)
        throw std::invalid_argument("SOFR fixing history is incomplete");
    const double rate = std::expm1(log_factor) * 360 / static_cast<double>(end - start);
    if (!std::isfinite(rate))
        throw std::overflow_error("SOFR compounding produced a non-finite rate");
    return SimpleRate{rate};
}
OisCurveBootstrapper::OisCurveBootstrapper(Calendar calendar, SolverSettings settings)
    : bootstrapper_(sofr_ois_conventions(std::move(calendar)), settings) {}
BootstrapResult OisCurveBootstrapper::build(Date valuation, std::vector<MarketQuote> quotes) const {
    for (const auto &quote : quotes)
        if (quote.instrument != Instrument::Deposit && quote.instrument != Instrument::Swap)
            throw std::invalid_argument("OIS calibration accepts only deposit and OIS swap quotes");
    return bootstrapper_.build(valuation, std::move(quotes));
}
SwapResult price_ois(const DiscountCurve &overnight, const Swap &swap) {
    const auto &c = swap.conventions;
    if (c.fixed_day_count != DayCount::Actual360 || c.floating_day_count != DayCount::Actual360 ||
        c.fixed_months != 12 || c.floating_months != 12)
        throw std::invalid_argument("SOFR OIS requires annual ACT/360 legs");
    // Product of future overnight factors telescopes to P(start)/P(end).
    // With zero payment lag this equals the existing period forward coupon amount.
    return price(overnight, swap);
}
namespace {
std::vector<MarketQuote> shifted(std::vector<MarketQuote> quotes, std::size_t bucket, double shift) {
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        if (bucket == quotes.size() || bucket == i) {
            const double changed = quotes[i].rate.value + shift;
            if (changed == quotes[i].rate.value)
                throw std::invalid_argument("Quote bump is below floating-point resolution");
            quotes[i].rate = SimpleRate{changed};
        }
    }
    return quotes;
}
} // namespace
MultiCurveRiskReport multi_curve_risk(const CurveBootstrapper &discount_builder,
                                      const CurveBootstrapper &forwarding_builder, Date valuation,
                                      const std::vector<MarketQuote> &discount_quotes,
                                      const std::vector<MarketQuote> &forwarding_quotes, const Swap &swap,
                                      BasisPoints bump) {
    if (!std::isfinite(bump.value) || bump.value <= 0)
        throw std::invalid_argument("Risk bump must be finite and positive in basis points");
    const auto discount = discount_builder.build(valuation, discount_quotes).curve;
    const auto projection = forwarding_builder.build_forward(discount, forwarding_quotes).curve;
    const PreparedSwap prepared{swap};
    const auto sensitivity = [&](bool discount_shock, std::size_t bucket) {
        const auto pv = [&](double shift) {
            const auto d =
                discount_shock
                    ? discount_builder.build(valuation, shifted(discount_quotes, bucket, shift)).curve
                    : discount;
            const auto f = forwarding_builder
                               .build_forward(d, discount_shock ? forwarding_quotes
                                                                : shifted(forwarding_quotes, bucket, shift))
                               .curve;
            return price(d, f, prepared).pv;
        };
        return (pv(-bump.value * 1e-4) - pv(bump.value * 1e-4)) / (2 * bump.value);
    };
    MultiCurveRiskReport result{price(discount, projection, prepared),
                                sensitivity(true, discount_quotes.size()),
                                sensitivity(false, forwarding_quotes.size()),
                                {},
                                {},
                                bump.value};
    for (const bool discount_shock : {true, false}) {
        const auto &quotes = discount_shock ? discount_quotes : forwarding_quotes;
        const auto &conventions = (discount_shock ? discount_builder : forwarding_builder).conventions();
        auto &buckets = discount_shock ? result.discount_quotes : result.forwarding_quotes;
        for (std::size_t i = 0; i < quotes.size(); ++i)
            buckets.push_back(
                {quotes[i].label, quote_pillar(quotes[i], conventions), sensitivity(discount_shock, i)});
        std::sort(buckets.begin(), buckets.end(),
                  [](const auto &a, const auto &b) { return a.maturity < b.maturity; });
    }
    return result;
}
} // namespace rates
