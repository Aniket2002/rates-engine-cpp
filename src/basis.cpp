#include "rates/basis.hpp"
#include <algorithm>
#include <set>
#include <utility>

namespace rates {
BasisResult price_basis(const DiscountCurve &discount, const DiscountCurve &base_projection,
                        const DiscountCurve &other_projection, const BasisSwap &swap,
                        const FixingStore &base_fixings, const FixingStore &other_fixings, Timestamp as_of,
                        bool include_valuation_date_payments) {
    if (!std::isfinite(swap.spread.value))
        throw std::invalid_argument("Basis spread must be finite");
    const SofrSwap base{swap.effective, swap.maturity, swap.notional,
                        swap.spread,    swap.side,     swap.base_conventions};
    const SofrSwap other{swap.effective, swap.maturity, swap.notional,
                         SimpleRate{0},  swap.side,     swap.other_conventions};
    auto b =
        price_sofr(discount, base_projection, base, base_fixings, as_of, include_valuation_date_payments);
    auto o =
        price_sofr(discount, other_projection, other, other_fixings, as_of, include_valuation_date_payments);
    double annuity = 0, accrued = 0;
    for (const auto &cf : b.coupons) {
        annuity += swap.notional * cf.period.accrual.value * cf.discount;
        accrued -= cf.floating_accrued + cf.fixed_accrued;
    }
    for (const auto &cf : o.coupons)
        accrued += cf.floating_accrued;
    const double sign = swap.side == Side::Payer ? 1 : -1;
    const double pv = sign * (o.floating_pv - b.floating_pv - b.fixed_pv);
    const double spread_pv = b.fixed_pv;
    const auto par =
        annuity > 0 ? std::optional<double>{(o.floating_pv - b.floating_pv) / annuity} : std::nullopt;
    accrued *= sign;
    if (!std::isfinite(pv) || !std::isfinite(accrued) || !std::isfinite(pv - accrued) ||
        (par && !std::isfinite(*par)))
        throw std::overflow_error("Non-finite basis valuation");
    return {std::move(b), std::move(o), spread_pv, pv, accrued, pv - accrued, par};
}
BasisCurveBootstrapper::BasisCurveBootstrapper(SofrConventions base, SofrConventions other,
                                               SolverSettings settings)
    : base_(std::move(base)), other_(std::move(other)), settings_(settings) {
    base_.validate();
    other_.validate();
    (void)CurveBootstrapper{Conventions{}, settings_};
}
BasisSwap BasisCurveBootstrapper::instrument(Date trade_date, int months, double notional, SimpleRate spread,
                                             Side side) const {
    const auto s = spot_sofr_swap(trade_date, months, notional, spread, side, base_);
    if (!other_.calendar.is_business_day(s.effective))
        throw std::invalid_argument("Basis effective date must be a business day for both indices");
    return {s.effective, s.maturity, notional, spread, side, base_, other_};
}
BasisBootstrapResult BasisCurveBootstrapper::build(const DiscountCurve &discount,
                                                   const DiscountCurve &base_projection, Timestamp as_of,
                                                   std::vector<BasisQuote> quotes,
                                                   const FixingStore &base_fixings,
                                                   const FixingStore &other_fixings) const {
    if (quotes.empty() || discount.valuation_date() != as_of.date() ||
        base_projection.valuation_date() != as_of.date())
        throw std::invalid_argument("Basis calibration needs quotes and matching curve dates");
    std::sort(quotes.begin(), quotes.end(),
              [](const auto &a, const auto &b) { return a.tenor_months < b.tenor_months; });
    std::set<std::string> labels;
    std::vector<CurveNode> nodes;
    std::vector<BasisCalibrationRow> rows;
    for (const auto &quote : quotes) {
        if (quote.label.empty() || !labels.insert(quote.label).second || !std::isfinite(quote.spread.value))
            throw std::invalid_argument("Basis labels must be unique/nonempty and spreads finite");
        const auto trade = instrument(as_of.date(), quote.tenor_months, 1, quote.spread);
        const auto base_end = base_.calendar.adjust(trade.maturity, base_.adjustment);
        const auto other_end = other_.calendar.adjust(trade.maturity, other_.adjustment);
        const auto pillar = std::max(base_.payment_calendar.advance(base_end, base_.payment_lag_days),
                                     other_.payment_calendar.advance(other_end, other_.payment_lag_days));
        if (pillar <= as_of.date() || (!nodes.empty() && pillar <= nodes.back().date))
            throw std::invalid_argument("Duplicate or reversed basis pillar");
        const auto objective = [&](double log_df) {
            auto trial = nodes;
            trial.push_back({pillar, DiscountFactor{std::exp(log_df)}});
            const DiscountCurve projection{as_of.date(), trial};
            const auto value =
                price_basis(discount, base_projection, projection, trade, base_fixings, other_fixings, as_of);
            if (!value.par_spread)
                throw std::logic_error("Basis calibration has no spread annuity");
            return *value.par_spread - quote.spread.value;
        };
        double width = .25;
        while (width < 50 && std::signbit(objective(-width)) == std::signbit(objective(width)))
            width = std::min(50., 2 * width);
        const auto solved = bisect(objective, -width, width, settings_);
        nodes.push_back({pillar, DiscountFactor{std::exp(solved.root)}});
        rows.push_back({quote, trade.effective, pillar, 0, 0, solved.iterations});
    }
    DiscountCurve curve{as_of.date(), nodes};
    for (auto &row : rows) {
        const auto trade = instrument(as_of.date(), row.quote.tenor_months, 1, row.quote.spread);
        const auto value =
            price_basis(discount, base_projection, curve, trade, base_fixings, other_fixings, as_of);
        if (!value.par_spread)
            throw std::logic_error("Basis calibration has no spread annuity");
        row.model_quote = *value.par_spread;
        row.residual = row.model_quote - row.quote.spread.value;
        if (std::abs(row.residual) > settings_.quote_tolerance)
            throw std::runtime_error("Basis curve failed repricing: " + row.quote.label);
    }
    return {std::move(curve), std::move(rows)};
}
BasisQuoteRisk basis_quote_risk(const SofrCurveBootstrapper &db, const SofrCurveBootstrapper &bb,
                                const BasisCurveBootstrapper &ob, const std::vector<SofrQuote> &dq,
                                const std::vector<SofrQuote> &bq, const std::vector<BasisQuote> &oq,
                                const BasisSwap &swap, const FixingStore &df, const FixingStore &bf,
                                const FixingStore &of, Timestamp t, BasisPoints bump, bool include_today) {
    if (!std::isfinite(bump.value) || bump.value <= 0)
        throw std::invalid_argument("Basis risk bump must be finite and positive");
    const auto d = db.build(t, dq, df);
    const auto b = bb.build_forward(d.curve, t, bq, bf);
    const auto o = ob.build(d.curve, b.curve, t, oq, bf, of);
    BasisQuoteRisk result{price_basis(d.curve, b.curve, o.curve, swap, bf, of, t, include_today),
                          0,
                          0,
                          0,
                          {},
                          {},
                          {},
                          bump.value};
    const auto shift_quotes = [](auto quotes, std::size_t bucket, double shift) {
        for (std::size_t i = 0; i < quotes.size(); ++i) {
            if (bucket != quotes.size() && bucket != i)
                continue;
            auto &rate = [&]() -> SimpleRate & {
                if constexpr (requires { quotes[i].rate; })
                    return quotes[i].rate;
                else
                    return quotes[i].spread;
            }();
            if (rate.value + shift == rate.value)
                throw std::invalid_argument("Basis quote bump is below floating-point resolution");
            rate = SimpleRate{rate.value + shift};
        }
        return quotes;
    };
    for (int coordinate = 0; coordinate < 3; ++coordinate) {
        const auto size = coordinate == 0 ? dq.size() : coordinate == 1 ? bq.size() : oq.size();
        auto &buckets = coordinate == 0   ? result.discount_quotes
                        : coordinate == 1 ? result.base_quotes
                                          : result.basis_quotes;
        const auto scenario = [&](std::size_t bucket, double shift) {
            const auto dc =
                coordinate == 0 ? db.build(t, shift_quotes(dq, bucket, shift), df).curve : d.curve;
            const auto bc =
                coordinate < 2
                    ? bb.build_forward(dc, t, coordinate == 1 ? shift_quotes(bq, bucket, shift) : bq, bf)
                          .curve
                    : b.curve;
            const auto oc =
                ob.build(dc, bc, t, coordinate == 2 ? shift_quotes(oq, bucket, shift) : oq, bf, of).curve;
            return price_basis(dc, bc, oc, swap, bf, of, t, include_today).pv;
        };
        const auto risk = [&](std::size_t bucket) {
            return (scenario(bucket, -bump.value * 1e-4) - scenario(bucket, bump.value * 1e-4)) /
                   (2 * bump.value);
        };
        (coordinate == 0   ? result.discount_parallel_dv01
         : coordinate == 1 ? result.base_parallel_dv01
                           : result.basis_parallel_dv01) = risk(size);
        for (std::size_t i = 0; i < size; ++i) {
            const auto &label = coordinate == 0 ? dq[i].label : coordinate == 1 ? bq[i].label : oq[i].label;
            const auto pillar = [&](const auto &rows) {
                return std::find_if(rows.begin(), rows.end(),
                                    [&](const auto &row) { return row.quote.label == label; })
                    ->pillar;
            };
            const auto date = coordinate == 2   ? pillar(o.diagnostics)
                              : coordinate == 0 ? pillar(d.diagnostics)
                                                : pillar(b.diagnostics);
            buckets.push_back({label, date, risk(i)});
        }
        std::sort(buckets.begin(), buckets.end(),
                  [](const auto &a, const auto &b) { return a.maturity < b.maturity; });
    }
    return result;
}
} // namespace rates
