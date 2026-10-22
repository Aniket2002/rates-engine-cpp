#include "rates/sofr_curve.hpp"
#include <gtest/gtest.h>
#include <limits>

using namespace rates;
namespace {
Timestamp at(Date date, const std::string &clock = "16:00:00") {
    return Timestamp::parse(date.str() + "T" + clock + "Z");
}
DiscountCurve flat(Date date, double rate = 0.03) {
    const auto end = date.add_months(36);
    return {date, {{end, DiscountFactor{std::exp(-rate * static_cast<double>(end - date) / 365)}}}};
}
FixingStore history(Date start, Date end, const Calendar &calendar = Calendar{}) {
    std::vector<FixingRecord> records;
    for (auto d = start; d < end; d = calendar.advance(d, 1))
        records.push_back({d, at(calendar.advance(d, 1), "13:00:00"), SimpleRate{0.04}, "test", 0});
    return FixingStore{records};
}
SofrSwap swap(Date start = Date{2025, 1, 2}, Date end = Date{2025, 2, 3}) {
    return {start, end, 1e6, SimpleRate{0.035}};
}
} // namespace
TEST(SofrCalendar, BusinessDayLagsCrossWeekendsAndHolidays) {
    const Calendar cal{{Date{2025, 1, 6}}};
    EXPECT_EQ(cal.advance(Date{2025, 1, 3}, 2), (Date{2025, 1, 8}));
    EXPECT_EQ(cal.advance(Date{2025, 1, 8}, -2), (Date{2025, 1, 3}));
    EXPECT_EQ(cal.advance(Date{2025, 1, 4}, 0), (Date{2025, 1, 7}));
    EXPECT_THROW(cal.advance(Date{2025, 1, 3}, 4000), std::out_of_range);
    SofrConventions c;
    c.calendar = cal;
    const auto trade = spot_sofr_swap(Date{2025, 1, 3}, 12, 1e6, SimpleRate{0.04}, Side::Payer, c);
    EXPECT_EQ(trade.effective, (Date{2025, 1, 8}));
    EXPECT_EQ(trade.maturity, (Date{2026, 1, 8}));
    EXPECT_THROW(spot_sofr_swap(Date{2025, 1, 4}, 12, 1e6, SimpleRate{0}), std::invalid_argument);
}
TEST(SofrFixings, RevisionsAreSelectedByAvailabilityWithoutLookahead) {
    const Date d{2025, 1, 2};
    const auto early = at(d.add_days(1), "13:00:00"), late = at(d.add_days(1), "19:30:00");
    const FixingStore store{{{d, late, SimpleRate{0.05}, "NYF", 1}, {d, early, SimpleRate{0.04}, "NYF", 0}}};
    EXPECT_TRUE(store.as_of(early.add_seconds(-1)).empty());
    EXPECT_DOUBLE_EQ(store.as_of(early).at(d), 0.04);
    EXPECT_DOUBLE_EQ(store.as_of(late).at(d), 0.05);
    EXPECT_THROW((FixingStore{{{d, early, SimpleRate{0.04}, "NYF", 2}}}), std::invalid_argument);
    EXPECT_THROW((FixingStore{{{d, at(d), SimpleRate{0.04}, "NYF", 0}}}), std::invalid_argument);
    EXPECT_THROW(
        (FixingStore{{{d, early, SimpleRate{0.04}, "NYF", 0}, {d, early, SimpleRate{0.05}, "NYF", 1}}}),
        std::invalid_argument);
}
TEST(SofrLifecycle, FutureCouponRecoversLegacyAndPaymentLagDiscountsLater) {
    const Date v{2025, 1, 2};
    const auto curve = flat(v);
    auto trade = swap(v, Date{2026, 1, 2});
    trade.conventions.payment_lag_days = 0;
    const auto result = price_sofr(curve, curve, trade, FixingStore{}, at(v));
    const Swap legacy{v,           trade.maturity,        trade.notional, trade.fixed_rate,
                      Side::Payer, sofr_ois_conventions()};
    EXPECT_NEAR(result.pv, price_ois(curve, legacy).pv, 1e-8);
    EXPECT_DOUBLE_EQ(result.accrued, 0);
    trade.conventions.payment_lag_days = 2;
    const auto lagged = price_sofr(curve, curve, trade, FixingStore{}, at(v));
    EXPECT_EQ(lagged.coupons.front().period.payment, (Date{2026, 1, 6}));
    EXPECT_NEAR(lagged.pv,
                result.pv * curve.discount(Date{2026, 1, 6}).value / curve.discount(Date{2026, 1, 2}).value,
                1e-8);
}
TEST(SofrLifecycle, SeasonedCouponMixesRealizedAndForecastAndAccrues) {
    const Date v{2025, 1, 8};
    const auto curve = flat(v);
    const auto trade = swap();
    const auto fixing = history(trade.effective, v);
    const auto result = price_sofr(curve, curve, trade, fixing, at(v));
    double product = 1;
    for (const int days : {1, 3, 1, 1})
        product *= 1 + 0.04 * days / 360;
    const double realized = 1e6 * (product - 1);
    const double total = 1e6 * (product / curve.discount(trade.maturity).value - 1);
    EXPECT_NEAR(result.coupons.front().floating_amount, total, 1e-8);
    EXPECT_NEAR(result.coupons.front().floating_accrued, realized, 1e-8);
    EXPECT_NEAR(result.accrued, realized - 1e6 * 0.035 * 6 / 360, 1e-8);
    EXPECT_DOUBLE_EQ(result.clean_pv, result.pv - result.accrued);
    EXPECT_TRUE(result.coupons.front().observations.front().realized);
    EXPECT_FALSE(result.coupons.front().observations.back().realized);
    EXPECT_THROW(price_sofr(curve, curve, trade, FixingStore{}, at(v)), MarketDataError);
    EXPECT_THROW(price_sofr(curve, curve, trade, fixing, at(v, "12:00:00")), MarketDataError);
}
TEST(SofrLifecycle, ObservationShiftChangesWeightsAndLockoutRepeatsFixing) {
    const Date v{2025, 1, 9};
    const auto curve = flat(v);
    auto trade = swap(Date{2025, 1, 3}, Date{2025, 1, 8});
    trade.conventions.lookback_days = 1;
    const FixingStore fixings{{{Date{2025, 1, 2}, at(Date{2025, 1, 3}), SimpleRate{0.01}, "test"},
                               {Date{2025, 1, 3}, at(Date{2025, 1, 6}), SimpleRate{0.02}, "test"},
                               {Date{2025, 1, 6}, at(Date{2025, 1, 7}), SimpleRate{0.03}, "test"}}};
    const auto unshifted = price_sofr(curve, curve, trade, fixings, at(v));
    EXPECT_NEAR(unshifted.coupons[0].floating_amount,
                1e6 * ((1 + 0.01 * 3 / 360) * (1 + 0.02 / 360) * (1 + 0.03 / 360) - 1), 1e-8);
    trade.conventions.observation_shift = true;
    const auto shifted = price_sofr(curve, curve, trade, fixings, at(v));
    EXPECT_NEAR(shifted.coupons[0].floating_amount,
                1e6 * ((1 + 0.01 / 360) * (1 + 0.02 * 3 / 360) * (1 + 0.03 / 360) - 1), 1e-8);
    trade.conventions.lockout_days = 1;
    const auto locked = price_sofr(curve, curve, trade, fixings, at(v));
    EXPECT_NEAR(locked.coupons[0].floating_amount,
                1e6 * ((1 + 0.01 / 360) * (1 + 0.02 * 3 / 360) * (1 + 0.02 / 360) - 1), 1e-8);
}
TEST(SofrLifecycle, MaturedUnpaidPaymentDateAndSettledStates) {
    auto trade = swap(Date{2025, 1, 2}, Date{2025, 1, 6});
    const auto fixing = history(trade.effective, trade.maturity);
    const Date payment{2025, 1, 8};
    const auto due = price_sofr(flat(payment), flat(payment), trade, fixing, at(payment), true);
    ASSERT_EQ(due.coupons.size(), 1U);
    EXPECT_DOUBLE_EQ(due.coupons[0].discount, 1);
    EXPECT_NEAR(due.clean_pv, 0, 1e-8);
    const auto paid = price_sofr(flat(payment), flat(payment), trade, FixingStore{}, at(payment));
    EXPECT_DOUBLE_EQ(paid.pv, 0);
    EXPECT_EQ(paid.settled_coupons, 1U);
    EXPECT_FALSE(paid.par_rate);
    const Date next{2025, 1, 9};
    EXPECT_TRUE(price_sofr(flat(next), flat(next), trade, FixingStore{}, at(next), true).coupons.empty());
}
TEST(SofrLifecycle, ShiftedRateNormalizesDifferentObservationAndInterestLengths) {
    const Date v{2025, 1, 9};
    auto trade = swap(Date{2025, 1, 6}, Date{2025, 1, 8});
    trade.conventions.lookback_days = 1;
    trade.conventions.observation_shift = true;
    const FixingStore fixings{{{Date{2025, 1, 3}, at(Date{2025, 1, 6}), SimpleRate{0.02}, "test"},
                               {Date{2025, 1, 6}, at(Date{2025, 1, 7}), SimpleRate{0.03}, "test"}}};
    const auto result = price_sofr(flat(v), flat(v), trade, fixings, at(v));
    const double expected = 1e6 * ((1 + 0.02 * 3 / 360) * (1 + 0.03 / 360) - 1) * 2 / 4;
    EXPECT_NEAR(result.coupons[0].floating_amount, expected, 1e-8);
    EXPECT_NEAR(result.coupons[0].floating_accrued, expected, 1e-8);
}
TEST(SofrLifecycle, RejectsInvalidEconomicsConventionsDatesAndFactors) {
    const Date v{2025, 1, 2};
    auto trade = swap();
    auto c = trade.conventions;
    c.payment_lag_days = -1;
    EXPECT_THROW(c.validate(), std::invalid_argument);
    c = trade.conventions;
    c.lookback_days = 31;
    EXPECT_THROW(c.validate(), std::invalid_argument);
    trade.notional = -1;
    EXPECT_THROW(price_sofr(flat(v), flat(v), trade, FixingStore{}, at(v)), std::invalid_argument);
    trade = swap();
    EXPECT_THROW(price_sofr(flat(v), flat(v.add_days(1)), trade, FixingStore{}, at(v)),
                 std::invalid_argument);
    trade.conventions.lockout_days = 30;
    EXPECT_THROW(price_sofr(flat(v), flat(v), trade, FixingStore{}, at(v)), std::invalid_argument);
    trade = swap(v, v.add_days(1));
    const auto next = v.add_days(1);
    const FixingStore invalid{{{v, at(next), SimpleRate{-360}, "test"}}};
    EXPECT_THROW(price_sofr(flat(next), flat(next), trade, invalid, at(next)), std::invalid_argument);
}
TEST(SofrBootstrap, SpotSettlementAndPaymentPillarsReprice) {
    const auto asof = at(Date{2025, 1, 2});
    const SofrCurveBootstrapper builder;
    const std::vector<SofrQuote> quotes{{"1M", 1, SimpleRate{0.04}},
                                        {"3M", 3, SimpleRate{0.038}},
                                        {"1Y", 12, SimpleRate{0.035}},
                                        {"2Y", 24, SimpleRate{0.03}}};
    const auto built = builder.build(asof, quotes);
    EXPECT_EQ(built.diagnostics.front().effective, (Date{2025, 1, 6}));
    EXPECT_EQ(built.diagnostics.front().pillar, (Date{2025, 2, 10}));
    for (const auto &row : built.diagnostics)
        EXPECT_LE(std::abs(row.residual), 1e-12);
    const auto forward = builder.build_forward(flat(asof.date(), 0.02), asof, quotes);
    for (const auto &row : forward.diagnostics)
        EXPECT_LE(std::abs(row.residual), 1e-12);
    EXPECT_GT(
        std::abs(forward.curve.nodes().back().discount.value - built.curve.nodes().back().discount.value),
        1e-6);
    EXPECT_THROW(builder.build(asof, {}), std::invalid_argument);
    EXPECT_THROW(builder.build(asof, {quotes.front(), quotes.front()}), std::invalid_argument);
    const auto trade = spot_sofr_swap(asof.date(), 12, 1e6, SimpleRate{0.03});
    const auto risk = sofr_quote_risk(builder, quotes, trade, FixingStore{}, asof);
    EXPECT_LT(risk.parallel_dv01, 0);
    EXPECT_EQ(risk.quote_buckets.front().maturity, built.diagnostics.front().pillar);
}
TEST(SofrRisk, RealizedCouponsHaveNoProjectionRiskAndNegativeRatesCalibrate) {
    const auto trade = swap(Date{2025, 1, 2}, Date{2025, 1, 6});
    const auto asof = at(Date{2025, 1, 7});
    const auto fixing = history(trade.effective, trade.maturity);
    const auto risk = sofr_curve_risk(flat(asof.date()), flat(asof.date(), 0.05), trade, fixing, asof);
    EXPECT_DOUBLE_EQ(risk.projection_zero.front().dv01, 0);
    EXPECT_NE(risk.discount_zero.front().dv01, 0);
    const auto built = SofrCurveBootstrapper{}.build(asof, {{"1Y", 12, SimpleRate{-0.01}}});
    EXPECT_GT(built.curve.nodes().back().discount.value, 1);
    EXPECT_THROW(sofr_curve_risk(flat(asof.date()), flat(asof.date()), trade, fixing, asof, BasisPoints{0}),
                 std::invalid_argument);
}
TEST(SofrRisk, TwoCurveQuotesFreezeRealizedFixingsAndRespectSettlement) {
    const auto trade = swap(Date{2025, 1, 2}, Date{2025, 1, 6});
    const auto asof = at(Date{2025, 1, 7});
    const auto fixing = history(trade.effective, trade.maturity);
    const SofrCurveBootstrapper builder;
    const std::vector<SofrQuote> discount{{"3Y", 36, SimpleRate{0.03}}, {"1M", 1, SimpleRate{0.04}}};
    const std::vector<SofrQuote> projection{{"2Y", 24, SimpleRate{0.035}}, {"3M", 3, SimpleRate{0.045}}};
    const auto risk =
        sofr_multi_curve_quote_risk(builder, builder, discount, projection, trade, fixing, asof);
    ASSERT_EQ(risk.discount_quotes.size(), 2U);
    ASSERT_EQ(risk.projection_quotes.size(), 2U);
    EXPECT_EQ(risk.discount_quotes.front().label, "1M");
    EXPECT_EQ(risk.projection_quotes.front().label, "3M");
    EXPECT_DOUBLE_EQ(risk.projection_parallel_dv01, 0);
    for (const auto &bucket : risk.projection_quotes)
        EXPECT_DOUBLE_EQ(bucket.dv01, 0);
    EXPECT_NE(risk.discount_parallel_dv01, 0);
    const auto paid = sofr_multi_curve_quote_risk(builder, builder, discount, projection, trade,
                                                  FixingStore{}, at(Date{2025, 1, 8}));
    EXPECT_DOUBLE_EQ(paid.base.pv, 0);
    EXPECT_DOUBLE_EQ(paid.discount_parallel_dv01, 0);
    EXPECT_DOUBLE_EQ(paid.projection_parallel_dv01, 0);
    for (const double bump : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN(), 1e-300})
        EXPECT_THROW(sofr_multi_curve_quote_risk(builder, builder, discount, projection, trade, fixing, asof,
                                                 BasisPoints{bump}),
                     std::invalid_argument);
    EXPECT_THROW(sofr_multi_curve_quote_risk(builder, builder, {}, projection, trade, fixing, asof),
                 std::invalid_argument);
    EXPECT_THROW(sofr_multi_curve_quote_risk(builder, builder, discount, {}, trade, fixing, asof),
                 std::invalid_argument);
    EXPECT_THROW(
        sofr_multi_curve_quote_risk(builder, builder, discount, projection, trade, FixingStore{}, asof),
        MarketDataError);
}
