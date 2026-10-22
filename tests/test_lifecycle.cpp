#include "rates/basis.hpp"
#include "rates/lifecycle.hpp"
#include <gtest/gtest.h>
#include <limits>

using namespace rates;
namespace {
Timestamp ts(const std::string &day) {
    return Timestamp::parse(day + "T16:00:00Z");
}
DiscountCurve flat_curve(Date v, double rate) {
    const auto end = v.add_months(60);
    return {v, {{end, DiscountFactor{std::exp(-rate * (end - v) / 365.)}}}};
}
} // namespace
TEST(Basis, AnalyticalLegsParSpreadReceiverAndCalibration) {
    const Date v{2025, 1, 2}, end{2026, 1, 2};
    const auto d = flat_curve(v, .02), b = flat_curve(v, .03), o = flat_curve(v, .04);
    SofrConventions c;
    c.payment_lag_days = 0;
    BasisSwap trade{v, end, 1e6, SimpleRate{.005}, Side::Payer, c, c};
    const auto value = price_basis(d, b, o, trade, FixingStore{}, FixingStore{}, ts(v.str()));
    const double base = 1e6 * std::expm1(.03), other = 1e6 * std::expm1(.04);
    const double spread = 1e6 * .005 * 365 / 360;
    EXPECT_NEAR(value.pv, (other - base - spread) * std::exp(-.02), 1e-8);
    ASSERT_TRUE(value.par_spread);
    EXPECT_NEAR(*value.par_spread, (other - base) / 1e6 * 360 / 365, 1e-14);
    trade.side = Side::Receiver;
    EXPECT_NEAR(price_basis(d, b, o, trade, FixingStore{}, FixingStore{}, ts(v.str())).pv, -value.pv, 1e-8);
    const BasisCurveBootstrapper builder{c, c};
    const std::vector<BasisQuote> quotes{
        {"2Y", 24, SimpleRate{-.001}}, {"3M", 3, SimpleRate{.002}}, {"1Y", 12, SimpleRate{.001}}};
    const auto result = builder.build(d, b, ts(v.str()), quotes);
    for (const auto &row : result.diagnostics) {
        EXPECT_LE(std::abs(row.residual), 1e-12);
        const auto par = builder.instrument(v, row.quote.tenor_months, 1e6, row.quote.spread);
        EXPECT_NEAR(price_basis(d, b, result.curve, par, FixingStore{}, FixingStore{}, ts(v.str())).pv, 0,
                    3e-6);
    }
    EXPECT_THROW(builder.build(d, b, ts(v.str()), {}), std::invalid_argument);
    EXPECT_THROW(builder.build(d, b, ts(v.str()), {quotes[0], quotes[0]}), std::invalid_argument);
    EXPECT_THROW(builder.build(d, b, ts("2025-01-03"), quotes), std::invalid_argument);
}
TEST(Basis, DistinctFixingsSeasonedUnpaidAndSettled) {
    const Date start{2025, 1, 2}, end{2025, 1, 6}, v{2025, 1, 7};
    const auto d = flat_curve(v, .03);
    const FixingStore bf{{{start, ts("2025-01-03"), SimpleRate{.02}, "base"},
                          {Date{2025, 1, 3}, ts("2025-01-06"), SimpleRate{.025}, "base"}}};
    const FixingStore of{{{start, ts("2025-01-03"), SimpleRate{.04}, "other"},
                          {Date{2025, 1, 3}, ts("2025-01-06"), SimpleRate{.045}, "other"}}};
    const BasisSwap trade{start, end, 1e6, SimpleRate{.001}};
    const auto value = price_basis(d, d, d, trade, bf, of, ts(v.str()));
    const double expected = 1e6 * ((1 + .04 / 360) * (1 + .045 * 3 / 360) -
                                   (1 + .02 / 360) * (1 + .025 * 3 / 360) - .001 * 4 / 360);
    EXPECT_NEAR(value.accrued, expected, 1e-8);
    EXPECT_NEAR(value.pv, expected * d.discount(Date{2025, 1, 8}).value, 1e-8);
    EXPECT_THROW(price_basis(d, d, d, trade, bf, FixingStore{}, ts(v.str())), MarketDataError);
    const auto paid = flat_curve(Date{2025, 1, 8}, .03);
    EXPECT_DOUBLE_EQ(price_basis(paid, paid, paid, trade, FixingStore{}, FixingStore{}, ts("2025-01-08")).pv,
                     0);
    EXPECT_NEAR(price_basis(paid, paid, paid, trade, bf, of, ts("2025-01-08"), true).pv, expected, 1e-8);
}
TEST(Clearing, AcceptSettleNetAndIdempotentReplay) {
    ClearingLedger ledger;
    const ClearingEvent submit{"s", ts("2025-01-02"), ClearingEventKind::Submit, "trade", "", 0, {}};
    EXPECT_TRUE(ledger.apply(submit));
    EXPECT_FALSE(ledger.apply(submit));
    EXPECT_THROW(ledger.apply({"s", ts("2025-01-02"), ClearingEventKind::Submit, "different", "", 0, {}}),
                 std::invalid_argument);
    ledger.apply({"a", ts("2025-01-02"), ClearingEventKind::Accept, "trade", "", 0, Date{2025, 1, 6}});
    ledger.apply({"p", ts("2025-01-03"), ClearingEventKind::RegisterPayment, "trade", "coupon", 125,
                  Date{2025, 1, 6}});
    EXPECT_DOUBLE_EQ(ledger.net_exposure({{"trade", 125}}), 125);
    EXPECT_THROW(ledger.net_exposure({}), std::invalid_argument);
    EXPECT_THROW(ledger.net_exposure({{"trade", 125}, {"missing", 2}}), std::invalid_argument);
    EXPECT_THROW(
        ledger.apply({"early", ts("2025-01-03"), ClearingEventKind::SettlePayment, "trade", "coupon", 0, {}}),
        std::invalid_argument);
    EXPECT_THROW(ledger.apply({"close", ts("2025-01-06"), ClearingEventKind::Close, "trade", "", 0, {}}),
                 std::invalid_argument);
    EXPECT_EQ(ledger.journal().size(), 3U);
    ledger.apply({"pay", ts("2025-01-06"), ClearingEventKind::SettlePayment, "trade", "coupon", 0, {}});
    ledger.apply({"close", ts("2025-01-06"), ClearingEventKind::Close, "trade", "", 0, {}});
    EXPECT_DOUBLE_EQ(ledger.settled_cash(), 125);
    EXPECT_DOUBLE_EQ(ledger.net_exposure({}), 0);
    EXPECT_EQ(ledger.trades().at("trade").state, ClearingState::Closed);
    ClearingLedger replay;
    for (const auto &e : ledger.journal())
        replay.apply(e);
    EXPECT_DOUBLE_EQ(replay.settled_cash(), 125);
    EXPECT_THROW(ledger.apply({"old", ts("2025-01-02"), ClearingEventKind::Submit, "late", "", 0, {}}),
                 std::invalid_argument);
}
TEST(Clearing, RejectionAndTerminationReplaceUnpaidObligations) {
    ClearingLedger l;
    l.apply({"s", ts("2025-01-02"), ClearingEventKind::Submit, "t", "", 0, {}});
    l.apply({"a", ts("2025-01-02"), ClearingEventKind::Accept, "t", "", 0, Date{2026, 1, 2}});
    l.apply(
        {"p", ts("2025-01-02"), ClearingEventKind::RegisterPayment, "t", "coupon", 100, Date{2025, 4, 2}});
    l.apply({"end", ts("2025-01-03"), ClearingEventKind::Terminate, "t", "closeout", -50, Date{2025, 1, 6}});
    EXPECT_TRUE(l.payments().at("coupon").cancelled);
    EXPECT_THROW(l.apply({"bad", ts("2025-04-02"), ClearingEventKind::SettlePayment, "t", "coupon", 0, {}}),
                 std::invalid_argument);
    l.apply({"pay", ts("2025-01-06"), ClearingEventKind::SettlePayment, "t", "closeout", 0, {}});
    l.apply({"close", ts("2025-01-06"), ClearingEventKind::Close, "t", "", 0, {}});
    EXPECT_DOUBLE_EQ(l.settled_cash(), -50);
    l.apply({"s2", ts("2025-01-06"), ClearingEventKind::Submit, "t2", "", 0, {}});
    l.apply({"r2", ts("2025-01-06"), ClearingEventKind::Reject, "t2", "ineligible", 0, {}});
    EXPECT_THROW(l.apply({"a2", ts("2025-01-06"), ClearingEventKind::Accept, "t2", "", 0, Date{2026, 1, 2}}),
                 std::invalid_argument);
}
TEST(Collateral, CallsSettlementInterestReturnAndReplay) {
    CollateralAgreement c;
    c.receive_threshold = 100;
    c.post_threshold = 200;
    c.minimum_transfer = 50;
    c.rounding = 10;
    c.calendar = Calendar{{Date{2025, 1, 6}}};
    CollateralAccount a{Date{2025, 1, 3}, c};
    const CollateralEvent mark{"m", ts("2025-01-03"), CollateralEventKind::Mark, "", 1003};
    a.apply(mark);
    EXPECT_FALSE(a.apply(mark));
    EXPECT_DOUBLE_EQ(a.pending(), 910);
    EXPECT_EQ(a.transfers().at("m").due, (Date{2025, 1, 7}));
    a.apply({"m2", ts("2025-01-03"), CollateralEventKind::Mark, "", 1003});
    EXPECT_EQ(a.transfers().size(), 1U); // Pending calls must not be requested again.
    EXPECT_THROW(a.apply({"s", ts("2025-01-07"), CollateralEventKind::SettleTransfer, "m", 0}),
                 std::invalid_argument);
    a.apply({"i0", ts("2025-01-07"), CollateralEventKind::AccrueInterest, "", .036});
    a.apply({"s", ts("2025-01-07"), CollateralEventKind::SettleTransfer, "m", 0});
    a.apply({"i1", ts("2025-01-10"), CollateralEventKind::AccrueInterest, "", .036});
    EXPECT_NEAR(a.accrued_interest(), -.273, 1e-12);
    a.apply({"ip", ts("2025-01-10"), CollateralEventKind::PayInterest, "", 0});
    a.apply({"zero", ts("2025-01-10"), CollateralEventKind::Mark, "", 0});
    a.apply({"cancel", ts("2025-01-10"), CollateralEventKind::CancelTransfer, "zero", 0});
    a.apply({"return", ts("2025-01-10"), CollateralEventKind::ReturnCollateral, "", 0});
    a.apply({"i2", ts("2025-01-13"), CollateralEventKind::AccrueInterest, "", -.01});
    a.apply({"s2", ts("2025-01-13"), CollateralEventKind::SettleTransfer, "return", 0});
    a.apply({"ip2", ts("2025-01-13"), CollateralEventKind::PayInterest, "", 0});
    a.apply({"close", ts("2025-01-13"), CollateralEventKind::Close, "", 0});
    EXPECT_DOUBLE_EQ(a.balance(), 0);
    EXPECT_TRUE(a.closed());
    CollateralAccount replay{Date{2025, 1, 3}, c};
    for (const auto &e : a.journal())
        replay.apply(e);
    EXPECT_DOUBLE_EQ(a.settled_interest(), replay.settled_interest());
    EXPECT_THROW(a.apply({"late", ts("2025-01-14"), CollateralEventKind::Mark, "", 1}),
                 std::invalid_argument);
}
TEST(Collateral, DirectionMtaValidationAndFailureAtomicity) {
    CollateralAgreement c;
    c.minimum_transfer = 50;
    c.post_threshold = 100;
    c.settlement_days = 0;
    CollateralAccount a{Date{2025, 1, 2}, c};
    a.apply({"small", ts("2025-01-02"), CollateralEventKind::Mark, "", 49});
    EXPECT_DOUBLE_EQ(a.pending(), 0);
    a.apply({"post", ts("2025-01-02"), CollateralEventKind::Mark, "", -200});
    EXPECT_DOUBLE_EQ(a.pending(), -100);
    a.apply({"settle", ts("2025-01-02"), CollateralEventKind::SettleTransfer, "post", 0});
    a.apply({"interest", ts("2025-01-03"), CollateralEventKind::AccrueInterest, "", .036});
    EXPECT_NEAR(a.accrued_interest(), .01, 1e-12);
    const auto count = a.journal().size();
    EXPECT_THROW(a.apply({"inf", ts("2025-01-03"), CollateralEventKind::Mark, "",
                          std::numeric_limits<double>::infinity()}),
                 std::invalid_argument);
    EXPECT_EQ(a.journal().size(), count);
    EXPECT_DOUBLE_EQ(a.exposure(), -200);
    EXPECT_THROW(a.apply({"dup", ts("2025-01-03"), CollateralEventKind::SettleTransfer, "post", 0}),
                 std::invalid_argument);
    c.minimum_transfer = -1;
    EXPECT_THROW((CollateralAccount{Date{2025, 1, 2}, c}), std::invalid_argument);
}
TEST(Collateral, RoundedTargetIsStableWithZeroMinimumTransfer) {
    CollateralAgreement c;
    c.rounding = 25;
    c.settlement_days = 0;
    CollateralAccount a{Date{2025, 1, 2}, c};
    a.apply({"m", ts("2025-01-02"), CollateralEventKind::Mark, "", 101});
    a.apply({"repeat-pending", ts("2025-01-02"), CollateralEventKind::Mark, "", 101});
    EXPECT_DOUBLE_EQ(a.pending(), 125);
    EXPECT_EQ(a.transfers().size(), 1U);
    a.apply({"s", ts("2025-01-02"), CollateralEventKind::SettleTransfer, "m", 0});
    a.apply({"repeat-settled", ts("2025-01-02"), CollateralEventKind::Mark, "", 101});
    EXPECT_DOUBLE_EQ(a.pending(), 0);
    EXPECT_EQ(a.transfers().size(), 1U);
}
