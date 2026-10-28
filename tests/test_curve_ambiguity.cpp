#include "rates/curve_ambiguity.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <numeric>

using namespace rates;
namespace {
Date valuation_date() {
    return {2025, 1, 2};
}
Conventions unadjusted() {
    Conventions c;
    c.business_day = BusinessDay::Unadjusted;
    return c;
}
Swap target(Side side = Side::Payer) {
    return {valuation_date().add_months(6),
            valuation_date().add_months(42),
            1e6,
            SimpleRate{0.035},
            side,
            unadjusted()};
}
std::vector<QuoteBand> quotes() {
    return {
        {{Instrument::Deposit, "6M", valuation_date().add_months(6), SimpleRate{0.03}}, 0.02, 0.05},
        {{Instrument::Swap, "5Y", valuation_date().add_months(60), SimpleRate{0.04}}, 0.02, 0.05},
        {{Instrument::ContinuousZero, "2Y", valuation_date().add_months(24), SimpleRate{0.03}}, 0.02, 0.05}};
}
DiscountCurve flat(double rate = 0.03) {
    const auto end = valuation_date().add_months(60);
    return {valuation_date(),
            {{end, rate_to_discount(rate, year_fraction(valuation_date(), end, DayCount::Actual365Fixed),
                                    Compounding::Continuous)}}};
}
std::vector<double> sample(const CurveAmbiguityProblem &p, const DiscountCurve &curve) {
    std::vector<double> result;
    result.reserve(p.dates.size());
    for (const auto date : p.dates)
        result.push_back(curve.discount(date).value);
    return result;
}
} // namespace
TEST(CurveAmbiguity, LinearObjectiveEqualsCashflowPricingAndWitnessReprices) {
    const auto curve = flat();
    for (const auto side : {Side::Payer, Side::Receiver}) {
        const auto t = target(side);
        const auto problem =
            curve_ambiguity_problem(valuation_date(), quotes(), {t}, {-0.01, 0.10}, unadjusted());
        const auto dfs = sample(problem, curve);
        const auto witness = problem.witness(dfs);
        EXPECT_NEAR(std::inner_product(problem.objective.begin(), problem.objective.end(), dfs.begin(), 0.0),
                    price(curve, t).pv, 1e-8);
        EXPECT_NEAR(price(witness, t).pv, price(curve, t).pv, 1e-8);
    }
}
TEST(CurveAmbiguity, FixedProjectionObjectiveMatchesMulticurveAndPortfolioNets) {
    const auto discount = flat(), projection = flat(0.05);
    const auto t = target();
    const auto problem =
        curve_ambiguity_problem(valuation_date(), quotes(), {t}, {0.01, 0.10}, unadjusted(), projection);
    const auto dfs = sample(problem, discount);
    EXPECT_NEAR(std::inner_product(problem.objective.begin(), problem.objective.end(), dfs.begin(), 0.0),
                price(discount, projection, t).pv, 1e-8);
    const auto net = curve_ambiguity_problem(valuation_date(), quotes(), {t, target(Side::Receiver)},
                                             {0.01, 0.10}, unadjusted(), projection);
    for (const auto amount : net.objective)
        EXPECT_DOUBLE_EQ(amount, 0);
}
TEST(CurveAmbiguity, RejectsInvalidInputsAndOutOfDomainTrades) {
    const auto build = [](const std::vector<QuoteBand> &q, ForwardBounds f = {-0.01, 0.10}) {
        return curve_ambiguity_problem(valuation_date(), q, {target()}, f, unadjusted());
    };
    EXPECT_THROW(build({}), std::invalid_argument);
    EXPECT_THROW(build(quotes(), {0.10, 0.01}), std::invalid_argument);
    EXPECT_THROW(build(quotes(), {0, std::numeric_limits<double>::infinity()}), std::invalid_argument);
    auto q = quotes();
    q[0].bid = q[0].ask + 1;
    EXPECT_THROW(build(q), std::invalid_argument);
    q = quotes();
    q.push_back(q.front());
    EXPECT_THROW(build(q), std::invalid_argument);
    q = quotes();
    q[0].quote.maturity = valuation_date();
    EXPECT_THROW(build(q), std::invalid_argument);
    q = quotes();
    q[0].bid = -10;
    EXPECT_THROW(build(q), std::invalid_argument);
    auto t = target();
    t.maturity = valuation_date().add_months(72);
    EXPECT_THROW(curve_ambiguity_problem(valuation_date(), quotes(), {t}, {-0.01, 0.10}),
                 std::invalid_argument);
    t.effective = valuation_date().add_days(-1);
    EXPECT_THROW(curve_ambiguity_problem(valuation_date(), quotes(), {t}, {-0.01, 0.10}),
                 std::invalid_argument);
    EXPECT_THROW(curve_ambiguity_problem(valuation_date(), quotes(), {}, {-0.01, 0.10}),
                 std::invalid_argument);
}
TEST(CurveAmbiguity, WitnessRejectsInfeasibilityAndNonfiniteValues) {
    const auto p =
        curve_ambiguity_problem(valuation_date(), quotes(), {target()}, {-0.01, 0.10}, unadjusted());
    auto dfs = sample(p, flat());
    EXPECT_THROW(p.witness({}), std::invalid_argument);
    EXPECT_THROW(p.witness(dfs, -1), std::invalid_argument);
    dfs.front() = 0.99;
    EXPECT_THROW(p.witness(dfs), std::invalid_argument);
    dfs = sample(p, flat());
    dfs[1] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(p.witness(dfs), std::invalid_argument);
    dfs[1] = 0;
    EXPECT_THROW(p.witness(dfs), std::invalid_argument);
    dfs[1] = 2;
    EXPECT_THROW(p.witness(dfs), std::invalid_argument);
    EXPECT_THROW(p.witness(sample(p, flat(0.08))), std::invalid_argument);
}
