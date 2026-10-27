#include "rates/risk_engine.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <limits>
#include <numeric>
#include <random>

using namespace rates;
namespace {
const Date valuation{2025, 1, 2};
std::vector<MarketQuote> quotes() {
    return {{Instrument::Deposit, "1M", valuation.add_months(1), SimpleRate{0.031}},
            {Instrument::Deposit, "3M", valuation.add_months(3), SimpleRate{0.032}},
            {Instrument::Deposit, "6M", valuation.add_months(6), SimpleRate{0.033}},
            {Instrument::Swap, "1Y", valuation.add_months(12), SimpleRate{0.034}},
            {Instrument::Swap, "2Y", valuation.add_months(24), SimpleRate{0.035}},
            {Instrument::Swap, "3Y", valuation.add_months(36), SimpleRate{0.036}},
            {Instrument::Swap, "5Y", valuation.add_months(60), SimpleRate{0.038}},
            {Instrument::Swap, "10Y", valuation.add_months(120), SimpleRate{0.04}}};
}
Swap trade() {
    return {valuation, valuation.add_months(60), 1e6, SimpleRate{0.037}};
}
DiscountCurve flat(double rate) {
    const auto end = valuation.add_months(120);
    return {valuation, {{end, DiscountFactor{std::exp(-rate * (end - valuation) / 365.0)}}}};
}
} // namespace
TEST(Dates, StrictIsoAndLeapYear) {
    EXPECT_EQ(Date::parse("2024-02-29").str(), "2024-02-29");
    EXPECT_THROW(Date::parse("2025-02-29"), std::invalid_argument);
    EXPECT_THROW(Date::parse("2024-2-29"), std::invalid_argument);
    EXPECT_THROW(Date::parse("2024-02-2x"), std::invalid_argument);
    EXPECT_EQ((Date{2024, 3, 1} - Date{2024, 2, 28}), 2);
    EXPECT_EQ(Date(2024, 2, 28).add_days(2), Date(2024, 3, 1));
    EXPECT_THROW(Date(2199, 12, 31).add_days(1), std::invalid_argument);
}
TEST(Dates, MonthEndAndTenors) {
    EXPECT_EQ(Date(2024, 1, 31).add_months(1), Date(2024, 2, 29));
    EXPECT_EQ(Date(2024, 2, 29).add_months(1), Date(2024, 3, 31));
    EXPECT_EQ(Date(2024, 2, 29).add_months(1, false), Date(2024, 3, 29));
    EXPECT_EQ(Date(2024, 2, 29).add_months(Tenor::years(1).months), Date(2025, 2, 28));
    EXPECT_THROW(Tenor::years(std::numeric_limits<int>::max()), std::invalid_argument);
    EXPECT_THROW(valuation.add_months(99999), std::out_of_range);
}
TEST(Dates, BusinessDaysAndExplicitHolidays) {
    Calendar calendar{{Date{2025, 9, 1}}};
    EXPECT_EQ(calendar.adjust(Date{2025, 8, 31}, BusinessDay::Following), Date(2025, 9, 2));
    EXPECT_EQ(calendar.adjust(Date{2025, 8, 31}, BusinessDay::ModifiedFollowing), Date(2025, 8, 29));
    EXPECT_EQ(calendar.adjust(Date{2025, 8, 31}, BusinessDay::Unadjusted), Date(2025, 8, 31));
}
TEST(Schedules, AnchoredMonthEndAndShortFinalStub) {
    auto periods = make_schedule(Date{2024, 1, 31}, Date{2024, 5, 15}, 1, DayCount::Actual360, Calendar{},
                                 BusinessDay::Unadjusted);
    ASSERT_EQ(periods.size(), 4U);
    EXPECT_EQ(periods[0].end, Date(2024, 2, 29));
    EXPECT_EQ(periods[1].end, Date(2024, 3, 31));
    EXPECT_EQ(periods[3].start, Date(2024, 4, 30));
    EXPECT_NEAR(periods[3].accrual.value, 15.0 / 360, 1e-15);
}
TEST(Schedules, NonEndOfMonthDoesNotDrift) {
    auto p = make_schedule(Date{2024, 1, 30}, Date{2024, 4, 30}, 1, DayCount::Actual360, Calendar{},
                           BusinessDay::Unadjusted);
    EXPECT_EQ(p[1].end, Date(2024, 3, 30));
}
TEST(Schedules, InvalidAndCollapsedDates) {
    EXPECT_THROW(
        make_schedule(valuation, valuation, 3, DayCount::Actual360, Calendar{}, BusinessDay::Following),
        std::invalid_argument);
    EXPECT_THROW(make_schedule(valuation, valuation.add_months(1), 0, DayCount::Actual360, Calendar{},
                               BusinessDay::Following),
                 std::invalid_argument);
    EXPECT_THROW(make_schedule(Date{2025, 8, 29}, Date{2025, 8, 31}, 1, DayCount::Actual360, Calendar{},
                               BusinessDay::ModifiedFollowing),
                 std::invalid_argument);
    EXPECT_THROW(make_schedule(Date{2025, 8, 30}, Date{2025, 12, 1}, 1, DayCount::Actual360, Calendar{},
                               BusinessDay::Following),
                 std::invalid_argument);
}
TEST(DayCounts, LeapAndEuropeanThirty360) {
    EXPECT_DOUBLE_EQ(year_fraction(Date{2024, 1, 1}, Date{2025, 1, 1}, DayCount::Actual360).value,
                     366.0 / 360);
    EXPECT_DOUBLE_EQ(year_fraction(Date{2024, 1, 1}, Date{2025, 1, 1}, DayCount::Actual365Fixed).value,
                     366.0 / 365);
    EXPECT_DOUBLE_EQ(year_fraction(Date{2024, 2, 29}, Date{2024, 3, 31}, DayCount::ThirtyE360).value,
                     31.0 / 360);
    EXPECT_DOUBLE_EQ(year_fraction(Date{2024, 1, 31}, Date{2024, 2, 29}, DayCount::ThirtyE360).value,
                     29.0 / 360);
    EXPECT_THROW(year_fraction(valuation, valuation.add_days(-1), DayCount::Actual360),
                 std::invalid_argument);
}
TEST(Conventions, RateConversionsAndInvalidInputs) {
    for (double r : {-0.02, 0.0, 0.05})
        for (auto c : {Compounding::Simple, Compounding::Continuous}) {
            const auto df = rate_to_discount(r, YearFraction{0.5}, c);
            EXPECT_NEAR(discount_to_rate(df, YearFraction{0.5}, c), r, 1e-15);
        }
    EXPECT_THROW(rate_to_discount(-2, YearFraction{1}, Compounding::Simple), std::invalid_argument);
    EXPECT_THROW(discount_to_rate(DiscountFactor{0}, YearFraction{1}, Compounding::Simple),
                 std::invalid_argument);
    EXPECT_THROW(ZeroRate{std::numeric_limits<double>::quiet_NaN()}, std::invalid_argument);
}
TEST(Curves, LogLinearInterpolation) {
    const Date middle = valuation.add_days(182);
    DiscountCurve curve{valuation, {{valuation.add_days(364), DiscountFactor{0.9}}}};
    EXPECT_NEAR(curve.discount(middle).value, std::sqrt(0.9), 1e-15);
    EXPECT_DOUBLE_EQ(curve.discount(valuation).value, 1);
    EXPECT_NEAR(curve.zero(middle).value, -std::log(0.9) * 365 / 364, 1e-15);
    EXPECT_NEAR(curve.forward(valuation, middle, DayCount::Actual360).value,
                (1 / std::sqrt(0.9) - 1) * 360 / 182, 1e-15);
    EXPECT_THROW(curve.zero(valuation), std::invalid_argument);
}
TEST(Curves, RejectsInvalidPillarsAndExtrapolation) {
    EXPECT_THROW((DiscountCurve{valuation, {}}), std::invalid_argument);
    EXPECT_THROW((DiscountCurve{valuation, {{valuation, DiscountFactor{1}}}}), std::invalid_argument);
    EXPECT_THROW((DiscountCurve{valuation,
                                {{valuation.add_days(2), DiscountFactor{1}},
                                 {valuation.add_days(1), DiscountFactor{1}}}}),
                 std::invalid_argument);
    EXPECT_THROW((DiscountCurve{valuation, {{valuation.add_days(1), DiscountFactor{-1}}}}),
                 std::invalid_argument);
    auto curve = flat(0.03);
    EXPECT_THROW(curve.discount(valuation.add_months(121)), std::out_of_range);
    EXPECT_THROW(curve.discount(YearFraction{-1}), std::out_of_range);
    EXPECT_THROW(curve.shift_zero_node(1, BasisPoints{1}), std::out_of_range);
}
TEST(Curves, NegativeRatesAreAllowed) {
    const auto curve = flat(-0.01);
    EXPECT_GT(curve.discount(valuation.add_months(12)).value, 1);
    EXPECT_NEAR(curve.zero(valuation.add_months(12)).value, -0.01, 1e-15);
    EXPECT_LT(curve.forward(valuation, valuation.add_months(3), DayCount::Actual360).value, 0);
}
TEST(Solver, BracketingAndFailureDiagnostics) {
    EXPECT_NEAR(bisect([](double x) { return x * x - 2; }, 0, 2, {}).root, std::sqrt(2), 1e-12);
    EXPECT_THROW(bisect([](double x) { return x * x + 1; }, -1, 1, {}), std::runtime_error);
    EXPECT_THROW(bisect([](double x) { return x * x - 2; }, 0, 2, {1e-12, 1e-14, 1}), std::runtime_error);
    EXPECT_THROW(bisect([](double) { return std::numeric_limits<double>::infinity(); }, 0, 2, {}),
                 std::runtime_error);
    EXPECT_THROW(bisect([](double x) { return x; }, 1, 0, {}), std::invalid_argument);
}
TEST(Bootstrap, RepricesSparseIntermediateCouponsAndSorts) {
    auto q = quotes();
    std::reverse(q.begin(), q.end());
    const auto result = CurveBootstrapper{}.build(valuation, q);
    ASSERT_EQ(result.diagnostics.size(), 8U);
    for (const auto &row : result.diagnostics) {
        EXPECT_TRUE(row.converged);
        EXPECT_LE(std::abs(row.residual), 1e-12);
    }
    EXPECT_EQ(result.diagnostics.front().quote.label, "1M");
    EXPECT_GT(result.diagnostics.back().iterations, 0);
    EXPECT_NEAR(result.curve.discount(Date{2025, 2, 3}).value, 1 / (1 + 0.031 * 32 / 360), 1e-15);
}
TEST(Solver, SafeguardedSecantPreservesResidualAndCutsEvaluations) {
    int secant_calls = 0;
    const auto fast = safeguarded_secant(
        [&](double x) {
            ++secant_calls;
            return std::exp(x) - 1.1;
        },
        -0.25, 0.25, {});
    int bisection_calls = 0;
    const auto reference = bisect(
        [&](double x) {
            ++bisection_calls;
            return std::exp(x) - 1.1;
        },
        -0.25, 0.25, {});
    EXPECT_NEAR(fast.root, std::log(1.1), 1e-12);
    EXPECT_NEAR(fast.root, reference.root, 2e-12);
    EXPECT_LE(std::abs(fast.residual), 1e-12);
    EXPECT_LT(secant_calls, bisection_calls / 2);
    EXPECT_DOUBLE_EQ(safeguarded_secant([](double x) { return x; }, 0, 1, {}).root, 0);
    EXPECT_DOUBLE_EQ(safeguarded_secant([](double x) { return x - 1; }, 0, 1, {}).root, 1);
}
TEST(Solver, SafeguardedSecantEscapesFalsePositionStagnation) {
    for (const double sign : {-1.0, 1.0}) {
        const auto result =
            safeguarded_secant([=](double x) { return sign * (std::pow(x, 10) - 0.01); }, 0, 1, {});
        EXPECT_NEAR(result.root, std::pow(0.01, 0.1), 1e-10);
        EXPECT_LE(std::abs(result.residual), 1e-12);
    }
    const double largest = std::numeric_limits<double>::max();
    EXPECT_DOUBLE_EQ(safeguarded_secant([](double x) { return x; }, -largest, largest, {}).root, 0);
}
TEST(Solver, SafeguardedSecantRejectsInvalidAndUnconvergedObjectives) {
    EXPECT_THROW(safeguarded_secant([](double x) { return x * x + 1; }, -1, 1, {}), std::runtime_error);
    EXPECT_THROW(safeguarded_secant([](double x) { return x * x - 2; }, 0, 2, {1e-12, 1e-14, 1}),
                 std::runtime_error);
    EXPECT_THROW(
        safeguarded_secant([](double x) { return x < 0.1 ? -1.0 : 1.0; }, -1, 1, {1e-12, 1e-14, 200}),
        std::runtime_error);
    EXPECT_THROW(safeguarded_secant([](double) { return std::numeric_limits<double>::infinity(); }, 0, 2, {}),
                 std::runtime_error);
    EXPECT_THROW(safeguarded_secant([](double x) { return x; }, 1, 0, {}), std::invalid_argument);
    EXPECT_THROW(safeguarded_secant([](double x) { return x; }, 0, 1, {0, 1e-14, 200}),
                 std::invalid_argument);
    EXPECT_THROW(
        safeguarded_secant([](double x) { return x; }, 0, std::numeric_limits<double>::infinity(), {}),
        std::invalid_argument);
    // Finite endpoints must not hide an invalid interior evaluation.
    EXPECT_THROW(safeguarded_secant(
                     [](double x) {
                         return x == 0 ? -1.0 : (x == 1 ? 1.0 : std::numeric_limits<double>::quiet_NaN());
                     },
                     0, 1, {}),
                 std::runtime_error);
}
TEST(Solver, SafeguardedSecantChecksResidualInsideNarrowBrackets) {
    // Interval tolerance must not preempt an exactly representable interior root.
    EXPECT_DOUBLE_EQ(safeguarded_secant([](double x) { return 1e16 * x; }, -1e-15, 1e-15, {}).root, 0);
    const double tiny = std::numeric_limits<double>::denorm_min();
    EXPECT_DOUBLE_EQ(
        safeguarded_secant([=](double x) { return x / tiny; }, -tiny, tiny, {1e-12, tiny, 200}).root, 0);
}
TEST(Solver, SafeguardedSecantCrossChecksDeterministicKnownRoots) {
    std::mt19937 generator{20261028};
    std::uniform_real_distribution<double> roots{-0.8, 0.8};
    for (int i = 0; i < 200; ++i) {
        const double root = roots(generator);
        const double scale = i % 3 == 0 ? 0.01 : (i % 3 == 1 ? 1.0 : 10.0);
        const double sign = i % 2 == 0 ? 1.0 : -1.0;
        const auto objective = [&](double x) {
            EXPECT_GE(x, -1.0);
            EXPECT_LE(x, 1.0);
            return sign * std::expm1(scale * (x - root));
        };
        const auto solved = safeguarded_secant(objective, -1, 1, {});
        const auto reference = bisect(objective, -1, 1, {});
        EXPECT_LE(std::abs(solved.residual), 1e-12);
        EXPECT_NEAR(solved.root, root, 1.1e-12 / scale);
        EXPECT_NEAR(solved.root, reference.root, 2.1e-12 / scale);
    }
}
TEST(Bootstrap, BracketPricingFailureIncludesInstrumentLabel) {
    const DiscountCurve short_discount{valuation, {{valuation.add_months(12), DiscountFactor{0.97}}}};
    try {
        CurveBootstrapper{}.build_forward(
            short_discount, {{Instrument::Swap, "5Y", valuation.add_months(60), SimpleRate{0.04}}});
        FAIL() << "A discount curve that cannot cover the swap must fail";
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("Calibration failed for 5Y:"), std::string::npos);
        EXPECT_NE(message.find("extrapolation disabled"), std::string::npos);
    }
}
TEST(Bootstrap, SecantCalibratesPositiveAndNegativeQuotesEfficiently) {
    for (const double sign : {-1.0, 1.0}) {
        auto market = quotes();
        for (auto &q : market)
            q.rate = SimpleRate{sign * q.rate.value};
        const auto calibrated = CurveBootstrapper{}.build(valuation, market);
        for (const auto &row : calibrated.diagnostics) {
            EXPECT_LE(std::abs(row.residual), 1e-12);
            EXPECT_LT(row.iterations, 20) << row.quote.label;
        }
    }
}
TEST(Bootstrap, InvalidQuotes) {
    auto q = quotes();
    q.push_back(q.front());
    EXPECT_THROW(CurveBootstrapper{}.build(valuation, q), std::invalid_argument);
    q = quotes();
    q[1].maturity = q[0].maturity;
    EXPECT_THROW(CurveBootstrapper{}.build(valuation, q), std::invalid_argument);
    EXPECT_THROW(CurveBootstrapper{}.build(valuation, {}), std::invalid_argument);
    q = quotes();
    q[0].rate = SimpleRate{-100};
    EXPECT_THROW(CurveBootstrapper{}.build(valuation, q), std::invalid_argument);
    q = quotes();
    q.back().rate = SimpleRate{-100};
    EXPECT_THROW(CurveBootstrapper{}.build(valuation, q), std::runtime_error);
}
TEST(Bootstrap, ZeroAndNegativeQuotes) {
    for (const double rate : {0.0, -0.005}) {
        auto q = quotes();
        for (auto &item : q)
            item.rate = SimpleRate{rate};
        auto calibrated = CurveBootstrapper{}.build(valuation, q);
        for (const auto &row : calibrated.diagnostics)
            EXPECT_NEAR(row.model_quote, rate, 1e-12);
        if (rate < 0)
            EXPECT_GT(calibrated.curve.nodes().back().discount.value, 1);
        else
            // A residual-converged zero-rate solve need not return an exact unit discount.
            EXPECT_NEAR(calibrated.curve.nodes().back().discount.value, 1, 1e-11);
    }
}
TEST(Pricing, ParPayerReceiverCouponMonotonicityAndScaling) {
    auto curve = CurveBootstrapper{}.build(valuation, quotes()).curve;
    auto t = trade();
    const auto base = price(curve, t);
    t.side = Side::Receiver;
    EXPECT_DOUBLE_EQ(price(curve, t).pv, -base.pv);
    t.fixed_rate = SimpleRate{t.fixed_rate.value + 0.001};
    EXPECT_GT(price(curve, t).pv, -base.pv);
    t.side = Side::Payer;
    EXPECT_LT(price(curve, t).pv, base.pv);
    t = trade();
    t.notional *= 7;
    EXPECT_NEAR(price(curve, t).pv, 7 * base.pv, 1e-8);
    t.fixed_rate = SimpleRate{base.par_rate};
    EXPECT_NEAR(price(curve, t).pv, 0, 1e-8);
}
TEST(Pricing, IndependentCouponSumsAndTelescoping) {
    auto curve = flat(0.03);
    auto t = trade();
    t.effective = valuation.add_months(3); // Forward-starting is supported.
    const auto result = price(curve, t);
    double fixed = 0, floating = 0;
    for (const auto &cf : result.fixed_flows)
        fixed += t.notional * t.fixed_rate.value * cf.period.accrual.value *
                 curve.discount(cf.period.payment).value;
    for (const auto &cf : result.floating_flows)
        floating += t.notional *
                    (curve.discount(cf.period.start).value / curve.discount(cf.period.end).value - 1) *
                    curve.discount(cf.period.payment).value;
    EXPECT_NEAR(result.fixed_pv, fixed, 1e-9);
    EXPECT_NEAR(result.floating_pv, floating, 1e-9);
    const double telescope =
        curve.discount(t.effective).value - curve.discount(result.floating_flows.back().period.end).value;
    EXPECT_NEAR(result.floating_pv, t.notional * telescope, 1e-9);
    EXPECT_NEAR(result.par_rate, telescope / result.annuity, 1e-14);
}
TEST(Pricing, AnalyticalOnePeriodAndZeroCoupon) {
    Conventions c;
    c.business_day = BusinessDay::Unadjusted;
    c.fixed_months = 12;
    c.floating_months = 12;
    c.fixed_day_count = DayCount::Actual365Fixed;
    DiscountCurve curve{valuation, {{valuation.add_days(365), DiscountFactor{0.95}}}};
    Swap t{valuation, valuation.add_days(365), 1e6, SimpleRate{0.04}, Side::Payer, c};
    auto r = price(curve, t);
    EXPECT_NEAR(r.fixed_pv, 38000, 1e-9);
    EXPECT_NEAR(r.floating_pv, 50000, 1e-9);
    EXPECT_NEAR(r.pv, 12000, 1e-9);
    EXPECT_NEAR(r.par_rate, 0.05 / 0.95, 1e-14);
    t.fixed_rate = SimpleRate{0};
    EXPECT_NEAR(price(curve, t).pv, 50000, 1e-9);
}
TEST(Pricing, InvalidTradeAndMissingFixings) {
    const auto curve = flat(0.03);
    auto t = trade();
    t.notional = 0;
    EXPECT_THROW(price(curve, t), std::invalid_argument);
    t.notional = -1;
    EXPECT_THROW(price(curve, t), std::invalid_argument);
    t = trade();
    t.effective = valuation.add_days(-1);
    EXPECT_THROW(price(curve, t), std::invalid_argument);
    t = trade();
    t.maturity = valuation;
    EXPECT_THROW(price(curve, t), std::invalid_argument);
    t = trade();
    t.maturity = valuation.add_months(121);
    EXPECT_THROW(price(curve, t), std::out_of_range);
}
TEST(Risk, ParallelAndBucketSumWithBumpStability) {
    RiskEngine risk;
    const auto q = quotes();
    const auto t = trade();
    const double parallel = risk.parallel_quote_dv01(valuation, q, t);
    EXPECT_LT(parallel, 0);
    double sum = 0;
    for (const auto &b : risk.quote_risk(valuation, q, t))
        sum += b.dv01;
    EXPECT_NEAR(parallel, sum, 0.002); // Nonlinear cross effects and calibration error.
    EXPECT_NEAR(parallel, risk.parallel_quote_dv01(valuation, q, t, BasisPoints{0.5}), 0.002);
    auto par = t;
    par.fixed_rate = SimpleRate{0.038};
    EXPECT_LT(risk.parallel_quote_dv01(valuation, q, par), 0);
}
TEST(Risk, SymmetryNotionalAndUnmodifiedInputs) {
    RiskEngine risk;
    const auto q = quotes();
    auto t = trade();
    const double original_coupon = t.fixed_rate.value;
    const double base = risk.parallel_quote_dv01(valuation, q, t);
    t.side = Side::Receiver;
    EXPECT_NEAR(risk.parallel_quote_dv01(valuation, q, t), -base, 1e-10);
    t.side = Side::Payer;
    t.notional *= 2;
    EXPECT_NEAR(risk.parallel_quote_dv01(valuation, q, t), 2 * base, 1e-8);
    EXPECT_DOUBLE_EQ(t.fixed_rate.value, original_coupon);
    EXPECT_DOUBLE_EQ(q.front().rate.value, 0.031);
}
TEST(Risk, ZeroNodesDifferFromQuoteCoordinates) {
    RiskEngine risk;
    const auto q = quotes();
    const auto t = trade();
    const auto curve = CurveBootstrapper{}.build(valuation, q).curve;
    const auto zeros = risk.zero_risk(curve, t);
    const auto buckets = risk.quote_risk(valuation, q, t);
    ASSERT_EQ(zeros.size(), buckets.size());
    EXPECT_GT(std::abs(zeros[6].dv01 - buckets[6].dv01), 1.0);
    EXPECT_DOUBLE_EQ(zeros.back().dv01, 0); // 10Y node cannot affect a 5Y trade.
    EXPECT_LT(zeros[6].dv01, 0);
    auto receiver = t;
    receiver.side = Side::Receiver;
    auto opposite = risk.zero_risk(curve, receiver);
    for (std::size_t i = 0; i < zeros.size(); ++i)
        EXPECT_NEAR(zeros[i].dv01, -opposite[i].dv01, 1e-10);
}
TEST(Risk, AnalyticalOnePeriodQuoteAndZeroNodeRisk) {
    Conventions c;
    c.business_day = BusinessDay::Unadjusted;
    c.fixed_day_count = DayCount::Actual365Fixed;
    c.deposit_day_count = DayCount::Actual365Fixed;
    c.fixed_months = 12;
    const Date maturity = valuation.add_days(365);
    const std::vector<MarketQuote> q{{Instrument::Deposit, "1Y", maturity, SimpleRate{0.05}}};
    Swap t{valuation, maturity, 1e6, SimpleRate{0.04}, Side::Payer, c};
    RiskEngine risk{CurveBootstrapper{c}};
    const auto analytic_pv = [](double r) { return 1e6 * (1 - 1.04 / (1 + r)); };
    EXPECT_NEAR(risk.parallel_quote_dv01(valuation, q, t), (analytic_pv(0.0499) - analytic_pv(0.0501)) / 2,
                1e-8);
    const auto curve = CurveBootstrapper{c}.build(valuation, q).curve;
    EXPECT_NEAR(risk.zero_risk(curve, t)[0].dv01, -1e6 * 1.04 / 1.05 * std::sinh(0.0001), 1e-8);
}
TEST(Risk, RejectsInvalidBumpAndShockedCalibrationFailure) {
    RiskEngine risk;
    EXPECT_THROW(risk.parallel_quote_dv01(valuation, quotes(), trade(), BasisPoints{0}),
                 std::invalid_argument);
    EXPECT_THROW(risk.parallel_quote_dv01(valuation, quotes(), trade(), BasisPoints{1e6}),
                 std::runtime_error);
}
TEST(Regression, DeterministicRandomFlatCurves) {
    std::mt19937 generator(20251017);
    std::uniform_real_distribution<double> rates(-0.03, 0.12);
    for (int trial = 0; trial < 50; ++trial) {
        const double rate = rates(generator);
        const auto curve = flat(rate);
        auto t = trade();
        t.maturity = valuation.add_months(12 + trial);
        const auto r = price(curve, t);
        const auto end = r.floating_flows.back().period.end;
        EXPECT_NEAR(r.floating_pv, t.notional * (1 - std::exp(-rate * (end - valuation) / 365.0)), 2e-9);
        t.fixed_rate = SimpleRate{r.par_rate};
        EXPECT_NEAR(price(curve, t).pv, 0, 2e-9);
    }
}
TEST(Risk, EmptyQuoteRiskIsAnError) {
    EXPECT_THROW(RiskEngine{}.quote_risk(valuation, {}, trade()), std::invalid_argument);
}
TEST(Risk, QuoteBucketSymmetryAndScaling) {
    const RiskEngine risk;
    auto receiver = trade();
    receiver.side = Side::Receiver;
    receiver.notional *= 3;
    const auto payer = risk.quote_risk(valuation, quotes(), trade());
    const auto scaled = risk.quote_risk(valuation, quotes(), receiver);
    for (std::size_t i = 0; i < payer.size(); ++i)
        EXPECT_NEAR(scaled[i].dv01, -3 * payer[i].dv01, 1e-8);
}
TEST(Risk, NonlinearDifferencesShrinkWithBump) {
    const RiskEngine risk;
    const auto difference = [&](double bump) {
        const auto buckets = risk.quote_risk(valuation, quotes(), trade(), BasisPoints{bump});
        double sum = 0;
        for (const auto &bucket : buckets)
            sum += bucket.dv01;
        return std::abs(sum - risk.parallel_quote_dv01(valuation, quotes(), trade(), BasisPoints{bump}));
    };
    EXPECT_LT(difference(5), difference(50));
}
TEST(Regression, PublishedDemoValues) {
    const auto curve = CurveBootstrapper{}.build(valuation, quotes()).curve;
    const auto result = price(curve, trade());
    EXPECT_NEAR(result.pv, 4531.3529001537, 2e-5);
    EXPECT_NEAR(result.fixed_pv, 167660.057350828, 2e-5);
    EXPECT_NEAR(result.floating_pv, 172191.410250982, 2e-5);
    EXPECT_NEAR(RiskEngine{}.parallel_quote_dv01(valuation, quotes(), trade()), -451.94430030424, 0.002);
}
TEST(PreparedPricing, OwnsSchedulesAndPreservesValuationChecks) {
    auto original = trade();
    const PreparedSwap prepared{original};
    original.fixed_rate = SimpleRate{0.09};
    original.conventions.fixed_months = 12;
    for (double rate : {-0.01, 0.0, 0.06}) {
        const auto curve = flat(rate);
        EXPECT_DOUBLE_EQ(price(curve, prepared).pv, price(curve, trade()).pv);
        EXPECT_EQ(prepared.fixed_periods().size(), 10U);
        EXPECT_EQ(prepared.floating_periods().size(), 20U);
    }
    DiscountCurve later{valuation.add_days(1), {{valuation.add_months(120), DiscountFactor{0.8}}}};
    EXPECT_THROW(price(later, prepared), std::invalid_argument);
}
TEST(ScenarioReuse, MatchesIndependentPerTradeRiskForHeterogeneousPortfolio) {
    const RiskEngine engine;
    auto q = quotes();
    std::reverse(q.begin(), q.end());
    const auto scenarios = engine.prepare_scenarios(valuation, q, BasisPoints{0.5});
    EXPECT_EQ(scenarios.calibration_count(), 19U);
    EXPECT_EQ(scenarios.quote_shocks().size(), 8U);
    EXPECT_EQ(scenarios.zero_shocks().size(), 8U);
    EXPECT_LT(scenarios.parallel().up.discount(valuation.add_months(60)).value,
              scenarios.parallel().down.discount(valuation.add_months(60)).value);
    std::vector<Swap> portfolio{trade(), trade(), trade()};
    portfolio[1].side = Side::Receiver;
    portfolio[1].notional *= 2;
    portfolio[1].maturity = valuation.add_months(24);
    portfolio[2].effective = valuation.add_months(3);
    portfolio[2].maturity = valuation.add_months(53);
    portfolio[2].conventions.fixed_months = 12;
    portfolio[2].conventions.fixed_day_count = DayCount::Actual365Fixed;
    const auto reused = scenarios.portfolio(portfolio, "test");
    const auto direct = engine.portfolio_report(valuation, q, portfolio, "test", BasisPoints{0.5});
    const auto parallel = engine.portfolio_parallel_dv01(valuation, q, portfolio, BasisPoints{0.5});
    for (std::size_t i = 0; i < portfolio.size(); ++i) {
        EXPECT_DOUBLE_EQ(reused[i].base.pv, price(scenarios.base_curve(), portfolio[i]).pv);
        EXPECT_DOUBLE_EQ(reused[i].parallel_quote_dv01, parallel[i]);
        EXPECT_DOUBLE_EQ(reused[i].base.pv, direct[i].base.pv);
        const auto independent = engine.quote_risk(valuation, q, portfolio[i], BasisPoints{0.5});
        const auto zeros = engine.zero_risk(scenarios.base_curve(), portfolio[i], BasisPoints{0.5});
        for (std::size_t j = 0; j < independent.size(); ++j) {
            EXPECT_EQ(reused[i].quote_buckets[j].label, independent[j].label);
            EXPECT_DOUBLE_EQ(reused[i].quote_buckets[j].dv01, independent[j].dv01);
            EXPECT_DOUBLE_EQ(reused[i].zero_nodes[j].dv01, zeros[j].dv01);
        }
    }
}
TEST(ScenarioReuse, SnapshotIsolationAndFailureSafety) {
    const RiskEngine engine;
    auto q = quotes();
    const auto scenarios = engine.prepare_scenarios(valuation, q);
    const double pv = scenarios.report(trade(), "original").base.pv;
    for (auto &quote : q)
        quote.rate = SimpleRate{quote.rate.value + 0.01};
    EXPECT_DOUBLE_EQ(scenarios.report(trade(), "still-original").base.pv, pv);
    EXPECT_GT(std::abs(engine.prepare_scenarios(valuation, q).report(trade(), "new").base.pv - pv), 1000);
    auto invalid = trade();
    invalid.maturity = valuation.add_months(121);
    EXPECT_THROW(scenarios.portfolio({trade(), invalid}, "bad"), std::out_of_range);
    EXPECT_DOUBLE_EQ(scenarios.report(trade(), "after-failure").base.pv, pv);
    EXPECT_THROW(scenarios.portfolio({}, "empty"), std::invalid_argument);
    EXPECT_THROW(engine.portfolio_report(valuation, q, {}, "empty"), std::invalid_argument);
    EXPECT_THROW(engine.portfolio_parallel_dv01(valuation, q, {}), std::invalid_argument);
    EXPECT_THROW(engine.prepare_scenarios(valuation, {}), std::invalid_argument);
    EXPECT_THROW(engine.prepare_scenarios(valuation, q, BasisPoints{1e6}), std::runtime_error);
    EXPECT_THROW(engine.parallel_quote_dv01(valuation, q, trade(), BasisPoints{1e-20}), std::runtime_error);
}
TEST(NumericalFailures, InvalidConversionsAndOverflow) {
    const auto infinity = std::numeric_limits<double>::infinity();
    EXPECT_THROW(rate_to_discount(infinity, YearFraction{1}, Compounding::Simple), std::invalid_argument);
    EXPECT_THROW(rate_to_discount(0.03, YearFraction{0}, Compounding::Continuous), std::invalid_argument);
    EXPECT_THROW(rate_to_discount(-1000, YearFraction{1}, Compounding::Continuous), std::invalid_argument);
    EXPECT_THROW(rate_to_discount(0.03, YearFraction{1}, static_cast<Compounding>(255)),
                 std::invalid_argument);
    EXPECT_THROW(discount_to_rate(DiscountFactor{0.9}, YearFraction{0}, Compounding::Continuous),
                 std::invalid_argument);
    EXPECT_THROW(discount_to_rate(DiscountFactor{0.9}, YearFraction{1}, static_cast<Compounding>(255)),
                 std::invalid_argument);
    EXPECT_THROW(discount_to_rate(DiscountFactor{1e-308}, YearFraction{1e-308}, Compounding::Simple),
                 std::overflow_error);
    EXPECT_THROW(year_fraction(valuation, valuation.add_days(1), static_cast<DayCount>(255)),
                 std::invalid_argument);
    auto huge = trade();
    huge.notional = 1e308;
    huge.fixed_rate = SimpleRate{1e308};
    EXPECT_THROW(price(flat(0.03), huge), std::overflow_error);
    EXPECT_THROW(flat(0.03).forward(valuation, valuation, DayCount::Actual360), std::invalid_argument);
}
TEST(NumericalFailures, SolverEndpointsAndUnattainableAccuracy) {
    EXPECT_DOUBLE_EQ(bisect([](double x) { return x; }, 0, 1, {}).root, 0);
    EXPECT_DOUBLE_EQ(bisect([](double x) { return x - 1; }, 0, 1, {}).root, 1);
    EXPECT_THROW(bisect([](double x) { return x < 0.1 ? -1.0 : 1.0; }, -1, 1, {1e-12, 0.5, 100}),
                 std::runtime_error);
    for (const auto settings :
         {SolverSettings{-1, 1e-14, 200}, SolverSettings{1e-12, 0, 200}, SolverSettings{1e-12, 1e-14, 0}})
        EXPECT_THROW(CurveBootstrapper(Conventions{}, settings), std::invalid_argument);
    const std::vector<MarketQuote> deposit{quotes().front()};
    EXPECT_THROW(CurveBootstrapper(Conventions{}, {1e-20, 1e-14, 200}).build(valuation, deposit),
                 std::runtime_error);
    EXPECT_THROW(CurveBootstrapper{}.build(Date{2025, 1, 4}, quotes()), std::invalid_argument);
    auto q = quotes();
    q[0].instrument = static_cast<Instrument>(255);
    EXPECT_THROW(CurveBootstrapper{}.build(valuation, q), std::invalid_argument);
}
TEST(Conventions, InvalidContractsAndZeroThirtyEAccrual) {
    auto c = Conventions{};
    c.fixed_months = 121;
    EXPECT_THROW(c.validate(), std::invalid_argument);
    c = {};
    c.floating_months = 0;
    EXPECT_THROW(c.validate(), std::invalid_argument);
    EXPECT_THROW(Calendar{}.adjust(valuation, static_cast<BusinessDay>(255)), std::invalid_argument);
    EXPECT_THROW(make_schedule(Date{2025, 1, 30}, Date{2025, 1, 31}, 1, DayCount::ThirtyE360, Calendar{},
                               BusinessDay::Unadjusted),
                 std::invalid_argument);
    auto t = trade();
    t.side = static_cast<Side>(255);
    EXPECT_THROW(price(flat(0.03), t), std::invalid_argument);
    t = trade();
    t.notional = std::numeric_limits<double>::infinity();
    EXPECT_THROW(price(flat(0.03), t), std::invalid_argument);
    EXPECT_THROW(Date(1899, 12, 31), std::invalid_argument);
    EXPECT_THROW(valuation.add_days(110001), std::out_of_range);
    EXPECT_EQ(Calendar({valuation}).holidays().size(), 1U);
}
