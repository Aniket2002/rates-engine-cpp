#include "rates/multicurve.hpp"
#include <algorithm>
#include <gtest/gtest.h>

using namespace rates;
namespace {
const Date valuation{2025, 1, 2};
DiscountCurve flat(double rate) {
    const auto end = valuation.add_months(120);
    return {valuation, {{end, DiscountFactor{std::exp(-rate * static_cast<double>(end - valuation) / 365)}}}};
}
std::vector<MarketQuote> quotes(double spread = 0) {
    std::vector<MarketQuote> result;
    for (const int months : {6, 12, 24, 36, 60, 120})
        result.push_back({months == 6 ? Instrument::Deposit : Instrument::Swap, std::to_string(months),
                          valuation.add_months(months), SimpleRate{0.03 + months * 0.00002 + spread}});
    return result;
}
Swap trade() {
    return {valuation, valuation.add_months(60), 1e6, SimpleRate{0.035}};
}
} // namespace
TEST(MultiCurve, AnalyticalSeparateDiscountAndProjection) {
    auto t = trade();
    t.maturity = valuation.add_months(12);
    t.conventions.business_day = BusinessDay::Unadjusted;
    t.conventions.fixed_months = t.conventions.floating_months = 12;
    const auto discount = flat(0.02), projection = flat(0.05);
    const auto result = price(discount, projection, t);
    const double df = std::exp(-0.02);
    const double floating = t.notional * std::expm1(0.05) * df;
    EXPECT_NEAR(result.fixed_pv, t.notional * 0.035 * df, 1e-8);
    EXPECT_NEAR(result.floating_pv, floating, 1e-8);
    EXPECT_NEAR(result.par_rate, std::expm1(0.05), 1e-14);
    t.side = Side::Receiver;
    EXPECT_NEAR(price(discount, projection, t).pv, -result.pv, 1e-8);
}
TEST(MultiCurve, EqualCurvesRecoverLegacyAndForwardBootstrapReprices) {
    const CurveBootstrapper builder;
    const auto q = quotes();
    const auto discount = builder.build(valuation, q).curve;
    const auto projection = builder.build_forward(discount, q);
    for (std::size_t i = 1; i < discount.nodes().size(); ++i)
        EXPECT_NEAR(projection.curve.nodes()[i].discount.value, discount.nodes()[i].discount.value, 1e-10);
    EXPECT_DOUBLE_EQ(price(discount, discount, trade()).pv, price(discount, trade()).pv);
    const auto basis = builder.build_forward(discount, quotes(0.01));
    for (const auto &row : basis.diagnostics)
        EXPECT_LT(std::abs(row.residual), 1e-12);
    EXPECT_GT(std::abs(price(discount, basis.curve, trade()).pv - price(discount, trade()).pv), 10000);
}
TEST(MultiCurve, RejectsMismatchedDatesPastTradesAndInsufficientCoverage) {
    const auto curve = flat(0.03);
    const DiscountCurve other{valuation.add_days(1), {curve.nodes().back()}};
    EXPECT_THROW(price(curve, other, trade()), std::invalid_argument);
    auto past = trade();
    past.effective = valuation.add_days(-1);
    EXPECT_THROW(price(curve, curve, past), std::invalid_argument);
    const DiscountCurve short_curve{valuation, {{valuation.add_months(12), DiscountFactor{0.97}}}};
    EXPECT_THROW(price(short_curve, curve, trade()), std::out_of_range);
    EXPECT_THROW(price(curve, short_curve, trade()), std::out_of_range);
    EXPECT_THROW(CurveBootstrapper{}.build_forward(short_curve, quotes()), std::runtime_error);
}
TEST(Sofr, CompoundsWeekendsHolidaysAndNegativeRates) {
    const Date start{2025, 1, 3}, end{2025, 1, 8};
    const Calendar calendar{{Date{2025, 1, 6}}};
    const std::vector<OvernightFixing> history{{start, SimpleRate{0.04}},
                                               {Date{2025, 1, 7}, SimpleRate{-0.01}}};
    const double expected = ((1 + 0.04 * 4 / 360) * (1 - 0.01 / 360) - 1) * 360 / 5;
    EXPECT_NEAR(compound_sofr(start, end, history, calendar).value, expected, 1e-13);
    EXPECT_DOUBLE_EQ(compound_sofr(start, Date{2025, 1, 6}, {{start, SimpleRate{0}}}).value, 0);
}
TEST(Sofr, RejectsMissingDuplicateUnorderedExtraAndInvalidFixings) {
    const Date start{2025, 1, 3}, end{2025, 1, 7};
    const OvernightFixing first{start, SimpleRate{0.04}}, last{Date{2025, 1, 6}, SimpleRate{0.04}};
    for (const auto &history : std::vector<std::vector<OvernightFixing>>{{},
                                                                         {first},
                                                                         {last, first},
                                                                         {first, first, last},
                                                                         {first, last, last},
                                                                         {{start, SimpleRate{-120}}, last}})
        EXPECT_THROW(compound_sofr(start, end, history), std::invalid_argument);
    EXPECT_THROW(compound_sofr(end, start, {}), std::invalid_argument);
    EXPECT_THROW(compound_sofr(Date{2025, 1, 4}, end, {}), std::invalid_argument);
}
TEST(Ois, CalibrationCompoundedCouponsAndParIdentity) {
    const auto q = quotes();
    const auto built = OisCurveBootstrapper{}.build(valuation, q);
    auto t = trade();
    t.conventions = sofr_ois_conventions();
    const auto result = price_ois(built.curve, t);
    EXPECT_NEAR(result.floating_pv, t.notional * (1 - built.curve.discount(t.maturity).value), 1e-8);
    t.fixed_rate = SimpleRate{result.par_rate};
    EXPECT_NEAR(price_ois(built.curve, t).pv, 0, 1e-8);
    for (const auto &row : built.diagnostics)
        EXPECT_LT(std::abs(row.residual), 1e-12);
    EXPECT_THROW(price_ois(built.curve, trade()), std::invalid_argument);
    auto invalid = q;
    invalid.front().instrument = Instrument::ContinuousZero;
    EXPECT_THROW(OisCurveBootstrapper{}.build(valuation, invalid), std::invalid_argument);
}
TEST(MultiCurveRisk, RecalibratesForwardCurveAfterDiscountShock) {
    const CurveBootstrapper d{sofr_ois_conventions()}, f;
    auto dq = quotes(), fq = quotes(0.01);
    const auto base = multi_curve_risk(d, f, valuation, dq, fq, trade());
    const auto half = multi_curve_risk(d, f, valuation, dq, fq, trade(), BasisPoints{0.5});
    EXPECT_NEAR(base.discount_parallel_dv01, half.discount_parallel_dv01, 1e-4);
    EXPECT_NEAR(base.forwarding_parallel_dv01, half.forwarding_parallel_dv01, 1e-4);
    const auto discount = d.build(valuation, dq).curve;
    const auto projection = f.build_forward(discount, fq).curve;
    auto up = dq, down = dq;
    for (std::size_t i = 0; i < dq.size(); ++i) {
        up[i].rate.value += 1e-4;
        down[i].rate.value -= 1e-4;
    }
    const auto du = d.build(valuation, up).curve, dd = d.build(valuation, down).curve;
    const double expected = (price(dd, f.build_forward(dd, fq).curve, trade()).pv -
                             price(du, f.build_forward(du, fq).curve, trade()).pv) /
                            2;
    EXPECT_NEAR(base.discount_parallel_dv01, expected, 1e-8);
    const double frozen = (price(dd, projection, trade()).pv - price(du, projection, trade()).pv) / 2;
    EXPECT_GT(std::abs(frozen - expected), 1);
    std::reverse(dq.begin(), dq.end());
    const auto reversed = multi_curve_risk(d, f, valuation, dq, fq, trade());
    for (std::size_t i = 0; i < base.discount_quotes.size(); ++i) {
        EXPECT_EQ(reversed.discount_quotes[i].maturity, base.discount_quotes[i].maturity);
        EXPECT_NEAR(reversed.discount_quotes[i].dv01, base.discount_quotes[i].dv01, 1e-8);
    }
    EXPECT_THROW(multi_curve_risk(d, f, valuation, dq, fq, trade(), BasisPoints{0}), std::invalid_argument);
    EXPECT_THROW(multi_curve_risk(d, f, valuation, dq, fq, trade(), BasisPoints{1e-20}),
                 std::invalid_argument);
    EXPECT_THROW(multi_curve_risk(d, f, valuation, {}, fq, trade()), std::invalid_argument);
}
