#include "rates/sofr_curve.hpp"
#include <algorithm>
#include <set>
#include <utility>

namespace rates {
SofrCurveBootstrapper::SofrCurveBootstrapper(SofrConventions conventions, SolverSettings settings)
    : conventions_(std::move(conventions)), settings_(settings) {
    conventions_.validate();
    // Reuse the solver's settings validation even before a quote set is supplied.
    (void)CurveBootstrapper{Conventions{}, settings_};
}
SofrBootstrapResult SofrCurveBootstrapper::build(Timestamp as_of, std::vector<SofrQuote> quotes,
                                                 const FixingStore &fixings) const {
    return build_impl(as_of, std::move(quotes), fixings, nullptr);
}
SofrBootstrapResult SofrCurveBootstrapper::build_forward(const DiscountCurve &discount, Timestamp as_of,
                                                         std::vector<SofrQuote> quotes,
                                                         const FixingStore &fixings) const {
    if (discount.valuation_date() != as_of.date())
        throw std::invalid_argument("SOFR discount curve and calibration as-of date differ");
    return build_impl(as_of, std::move(quotes), fixings, &discount);
}
SofrBootstrapResult SofrCurveBootstrapper::build_impl(Timestamp as_of, std::vector<SofrQuote> quotes,
                                                      const FixingStore &fixings,
                                                      const DiscountCurve *discount) const {
    if (quotes.empty())
        throw std::invalid_argument("SOFR calibration requires a nonempty quote set");
    std::sort(quotes.begin(), quotes.end(),
              [](const auto &a, const auto &b) { return a.tenor_months < b.tenor_months; });
    std::vector<CurveNode> nodes;
    std::vector<SofrCalibrationRow> rows;
    std::set<std::string> labels;
    const auto v = as_of.date();
    for (const auto &quote : quotes) {
        if (quote.label.empty() || !labels.insert(quote.label).second || !std::isfinite(quote.rate.value))
            throw std::invalid_argument("SOFR quote labels must be unique/nonempty and rates finite");
        const auto trade = spot_sofr_swap(v, quote.tenor_months, 1, quote.rate, Side::Payer, conventions_);
        const auto end = conventions_.calendar.adjust(trade.maturity, conventions_.adjustment);
        const auto pillar = conventions_.payment_calendar.advance(end, conventions_.payment_lag_days);
        if (pillar <= v || (!nodes.empty() && pillar <= nodes.back().date))
            throw std::invalid_argument("Duplicate or reversed SOFR payment pillar");
        const auto objective = [&](double log_df) {
            auto candidate = nodes;
            candidate.push_back({pillar, DiscountFactor{std::exp(log_df)}});
            const DiscountCurve projection{v, candidate};
            const auto value =
                price_sofr(discount ? *discount : projection, projection, trade, fixings, as_of);
            if (!value.par_rate)
                throw std::logic_error("SOFR calibration instrument has no outstanding annuity");
            return *value.par_rate - quote.rate.value;
        };
        double width = 0.25;
        while (width < 50 && std::signbit(objective(-width)) == std::signbit(objective(width)))
            width = std::min(50.0, width * 2);
        const auto solved = bisect(objective, -width, width, settings_);
        nodes.push_back({pillar, DiscountFactor{std::exp(solved.root)}});
        rows.push_back({quote, trade.effective, pillar, 0, 0, solved.iterations});
    }
    DiscountCurve curve{v, nodes};
    for (auto &row : rows) {
        const auto trade =
            spot_sofr_swap(v, row.quote.tenor_months, 1, row.quote.rate, Side::Payer, conventions_);
        const auto value = price_sofr(discount ? *discount : curve, curve, trade, fixings, as_of);
        if (!value.par_rate)
            throw std::logic_error("SOFR calibration instrument has no outstanding annuity");
        row.model_quote = *value.par_rate;
        row.residual = row.model_quote - row.quote.rate.value;
        if (std::abs(row.residual) > settings_.quote_tolerance)
            throw std::runtime_error("Final SOFR curve failed repricing: " + row.quote.label);
    }
    return {std::move(curve), std::move(rows)};
}
namespace {
void check_bump(BasisPoints bump) {
    if (!std::isfinite(bump.value) || bump.value <= 0)
        throw std::invalid_argument("SOFR risk bump must be finite and positive");
}
} // namespace
SofrQuoteRisk sofr_quote_risk(const SofrCurveBootstrapper &builder, const std::vector<SofrQuote> &quotes,
                              const SofrSwap &swap, const FixingStore &fixings, Timestamp as_of,
                              BasisPoints bump) {
    check_bump(bump);
    const auto built = builder.build(as_of, quotes, fixings);
    const auto pv = [&](std::size_t bucket, double shift) {
        auto changed = quotes;
        for (std::size_t i = 0; i < changed.size(); ++i)
            if (bucket == changed.size() || bucket == i) {
                if (changed[i].rate.value + shift == changed[i].rate.value)
                    throw std::invalid_argument("SOFR quote bump is below floating-point resolution");
                changed[i].rate = SimpleRate{changed[i].rate.value + shift};
            }
        const auto curve = builder.build(as_of, changed, fixings).curve;
        return price_sofr(curve, curve, swap, fixings, as_of).pv;
    };
    const auto risk = [&](std::size_t bucket) {
        return (pv(bucket, -bump.value * 1e-4) - pv(bucket, bump.value * 1e-4)) / (2 * bump.value);
    };
    SofrQuoteRisk result{price_sofr(built.curve, built.curve, swap, fixings, as_of), risk(quotes.size()), {}};
    for (std::size_t i = 0; i < quotes.size(); ++i) {
        const auto found = std::find_if(built.diagnostics.begin(), built.diagnostics.end(),
                                        [&](const auto &row) { return row.quote.label == quotes[i].label; });
        result.quote_buckets.push_back({quotes[i].label, found->pillar, risk(i)});
    }
    std::sort(result.quote_buckets.begin(), result.quote_buckets.end(),
              [](const auto &a, const auto &b) { return a.maturity < b.maturity; });
    return result;
}
SofrCurveRisk sofr_curve_risk(const DiscountCurve &discount, const DiscountCurve &projection,
                              const SofrSwap &swap, const FixingStore &fixings, Timestamp as_of,
                              BasisPoints bump) {
    check_bump(bump);
    SofrCurveRisk result{price_sofr(discount, projection, swap, fixings, as_of), {}, {}};
    for (const bool is_discount : {true, false}) {
        const auto &curve = is_discount ? discount : projection;
        auto &buckets = is_discount ? result.discount_zero : result.projection_zero;
        for (std::size_t i = 1; i < curve.nodes().size(); ++i) {
            const auto down = curve.shift_zero_node(i - 1, BasisPoints{-bump.value});
            const auto up = curve.shift_zero_node(i - 1, bump);
            const auto pv = [&](const DiscountCurve &shocked) {
                return price_sofr(is_discount ? shocked : discount, is_discount ? projection : shocked, swap,
                                  fixings, as_of)
                    .pv;
            };
            const auto day = curve.nodes()[i].date;
            buckets.push_back({day.str(), day, (pv(down) - pv(up)) / (2 * bump.value)});
        }
    }
    return result;
}
} // namespace rates
