#include "rates/trading.hpp"
#include <gtest/gtest.h>
#include <limits>

using namespace rates;
using namespace rates::trading;
namespace {
const auto epoch = Timestamp::parse("2025-01-02T00:00:00Z");
Timestamp time_at(std::int64_t offset) {
    return epoch.add_seconds(offset);
}
PriceEvent close_at(std::int64_t offset, double price) {
    return {time_at(offset), time_at(offset + 1), PriceKind::Close, price};
}
PriceEvent open_at(std::int64_t offset, double price) {
    return {time_at(offset), time_at(offset + 1), PriceKind::Open, price};
}
SimulationConfig config() {
    SimulationConfig c;
    c.initial_cash = 1000;
    c.lookback = 2;
    c.half_spread_bps = 0;
    c.fee_bps = 0;
    return c;
}
std::vector<PriceEvent> round_trip() {
    return {close_at(10, 90), close_at(20, 100), open_at(30, 100), close_at(40, 110),
            close_at(50, 80), open_at(60, 80),   close_at(70, 80)};
}
const std::vector<Timestamp> decisions{time_at(12), time_at(22), time_at(42), time_at(52), time_at(72)};
} // namespace
TEST(TradingData, StrictAvailabilityRevisionsAndEventOrder) {
    auto first = close_at(10, 90);
    auto revision = first;
    revision.available_at = time_at(40);
    revision.price = 900;
    revision.revision = 1;
    const PriceTape tape{{first, close_at(20, 100), revision, close_at(50, 101)}};
    EXPECT_TRUE(tape.closes_before(time_at(11), 2).empty());
    EXPECT_DOUBLE_EQ(tape.closes_before(time_at(40), 2).front().price, 90);
    EXPECT_DOUBLE_EQ(tape.closes_before(time_at(41), 2).front().price, 900);
    EXPECT_DOUBLE_EQ(tape.closes_before(time_at(41), 1).front().price, 100);
    EXPECT_EQ(tape.closes_before(time_at(52), 9).size(), 3U);
    EXPECT_TRUE(tape.closes_before(time_at(52), 0).empty());
}
TEST(TradingData, RejectsInvalidTapeAndRevisionChains) {
    EXPECT_THROW((PriceTape{{}}), std::invalid_argument);
    auto e = close_at(10, 90);
    EXPECT_THROW((PriceTape{{e, e}}), std::invalid_argument);
    EXPECT_THROW((PriceTape{{close_at(20, 90), e}}), std::invalid_argument);
    e.available_at = time_at(9);
    EXPECT_THROW((PriceTape{{e}}), std::invalid_argument);
    e = close_at(10, 90);
    e.revision = 1;
    EXPECT_THROW((PriceTape{{e}}), std::invalid_argument);
    for (const double bad :
         {0.0, -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        e = close_at(10, bad);
        EXPECT_THROW((PriceTape{{e}}), std::invalid_argument);
    }
    e = close_at(10, 90);
    e.kind = static_cast<PriceKind>(99);
    EXPECT_THROW((PriceTape{{e}}), std::invalid_argument);
}
TEST(TradingSimulation, HandCalculatedRoundTripAndRisk) {
    const auto r = simulate(PriceTape{round_trip()}, decisions, config());
    ASSERT_EQ(r.fills.size(), 2U);
    EXPECT_DOUBLE_EQ(r.fills[0].shares, 10);
    EXPECT_EQ(r.fills[0].event_time, time_at(30));
    EXPECT_DOUBLE_EQ(r.equity[2].equity, 1100);
    EXPECT_DOUBLE_EQ(r.equity.back().cash, 800);
    EXPECT_DOUBLE_EQ(r.equity.back().shares, 0);
    EXPECT_DOUBLE_EQ(r.equity.back().pnl, -200);
    EXPECT_NEAR(r.max_drawdown, 300.0 / 1100, 1e-15);
    EXPECT_DOUBLE_EQ(r.turnover, 1.8);
    EXPECT_GT(r.period_volatility, 0);
    EXPECT_EQ(r.orders[0].status, "filled");
}
TEST(TradingSimulation, CostsGapAffordabilityAndFixedOrderSize) {
    auto c = config();
    c.half_spread_bps = 100;
    c.fee_bps = 100;
    auto events = round_trip();
    events[2].price = 150;
    const auto r = simulate(PriceTape{events}, decisions, c);
    ASSERT_EQ(r.fills.size(), 2U);
    EXPECT_DOUBLE_EQ(r.orders[0].requested_shares, 9);
    EXPECT_DOUBLE_EQ(r.fills[0].shares, 6);
    EXPECT_DOUBLE_EQ(r.fills[0].execution_price, 151.5);
    EXPECT_NEAR(r.fills[0].fee, 9.09, 1e-12);
    EXPECT_EQ(r.orders[0].status, "partial_cancelled");
    EXPECT_NEAR(r.equity.back().cash, 1000 - 909 - 9.09 + 475.2 - 4.752, 1e-10);
    EXPECT_NEAR(r.total_spread_cost, 6 * 1.5 + 6 * 0.8, 1e-12);
    EXPECT_NEAR(r.total_fees, 9.09 + 4.752, 1e-12);
}
TEST(TradingSimulation, RevisionsCannotFillAndFutureChangesDoNotChangePast) {
    auto events = round_trip();
    auto revision = events[2];
    revision.available_at = time_at(45);
    revision.price = 1;
    revision.revision = 1;
    events.insert(events.begin() + 4, revision);
    auto future = close_at(100, 1e6);
    events.push_back(future);
    const auto original = simulate(PriceTape{round_trip()}, decisions, config());
    const auto changed = simulate(PriceTape{events}, decisions, config());
    ASSERT_EQ(changed.fills.size(), original.fills.size());
    EXPECT_DOUBLE_EQ(changed.equity.back().equity, original.equity.back().equity);
    EXPECT_DOUBLE_EQ(changed.fills[0].execution_price, 100);
    events[2].event_time = time_at(21); // received after decision, happened before it
    events.erase(events.begin() + 4);   // remove revision of now different key
    const auto delayed = simulate(PriceTape{events}, {time_at(22), time_at(42)}, config());
    EXPECT_TRUE(delayed.fills.empty());
}
TEST(TradingSimulation, ExpiryLatencyAndLatePublication) {
    auto c = config();
    c.order_ttl_seconds = 5;
    auto r = simulate(PriceTape{round_trip()}, {time_at(22), time_at(42)}, c);
    EXPECT_TRUE(r.fills.empty());
    EXPECT_EQ(r.orders.front().status, "expired");
    c = config();
    c.latency_seconds = 8; // exact boundary open must be excluded
    r = simulate(PriceTape{round_trip()}, {time_at(22), time_at(42)}, c);
    EXPECT_TRUE(r.fills.empty());
    EXPECT_EQ(r.orders.front().status, "replaced");
    c = config();
    c.max_execution_delay_seconds = 0;
    r = simulate(PriceTape{round_trip()}, {time_at(22), time_at(42)}, c);
    EXPECT_TRUE(r.fills.empty());
    EXPECT_EQ(r.orders.back().status, "unfilled_end");
}
TEST(TradingSimulation, MissingStaleAndRevisionFreshness) {
    const PriceTape tape{round_trip()};
    EXPECT_THROW(simulate(tape, {time_at(11)}, config()), MarketDataError);
    auto c = config();
    c.max_price_age_seconds = 2;
    EXPECT_NO_THROW(simulate(tape, {time_at(22)}, c));
    EXPECT_THROW(simulate(tape, {time_at(23)}, c), MarketDataError);
    auto revision = close_at(10, 100);
    revision.available_at = time_at(100);
    revision.revision = 1;
    EXPECT_THROW(simulate(PriceTape{{close_at(10, 90), revision}}, {time_at(102)}, c), MarketDataError);
}
TEST(TradingSimulation, DelayedRevisionChangesOnlyLaterDecisions) {
    // Original 90 -> 100 means buy. A correction 90 -> 110 reverses that
    // signal, but cannot be seen at or before its publication timestamp.
    auto revision = close_at(10, 110);
    revision.available_at = time_at(25);
    revision.revision = 1;
    const PriceTape tape{{close_at(10, 90), close_at(20, 100), revision}};
    const auto early = simulate(tape, {time_at(22)}, config());
    const auto equal = simulate(tape, {time_at(25)}, config());
    const auto late = simulate(tape, {time_at(26)}, config());
    EXPECT_EQ(early.orders.size(), 1U);
    EXPECT_EQ(equal.orders.size(), 1U);
    EXPECT_TRUE(late.orders.empty());
    EXPECT_TRUE(early.fills.empty());                         // no same-close execution
    EXPECT_EQ(tape.closes_before(time_at(26), 2).size(), 2U); // not three samples
}
TEST(TradingSimulation, NumericalOverflowFailsWithoutPublishingResults) {
    auto events = round_trip();
    events[3].price = 1e308; // ten held shares cannot be marked finitely
    EXPECT_THROW(simulate(PriceTape{events}, decisions, config()), std::overflow_error);
    events = round_trip();
    events[5].price = 1e308; // sale proceeds overflow
    EXPECT_THROW(simulate(PriceTape{events}, decisions, config()), std::overflow_error);
    events = round_trip();
    events[2].price = std::numeric_limits<double>::max();
    auto c = config();
    c.half_spread_bps = 100;
    EXPECT_THROW(simulate(PriceTape{events}, decisions, c), std::overflow_error);
}
TEST(TradingSimulation, ConfigurationAndNumericalBounds) {
    const PriceTape tape{round_trip()};
    EXPECT_THROW(simulate(tape, {}, config()), std::invalid_argument);
    EXPECT_THROW(simulate(tape, {time_at(22), time_at(22)}, config()), std::invalid_argument);
    EXPECT_THROW(simulate(tape, {time_at(42), time_at(22)}, config()), std::invalid_argument);
    auto c = config();
    c.lookback = 1;
    EXPECT_THROW(simulate(tape, decisions, c), std::invalid_argument);
    c = config();
    c.initial_cash = -1;
    EXPECT_THROW(simulate(tape, decisions, c), std::invalid_argument);
    c = config();
    c.half_spread_bps = 10000;
    EXPECT_THROW(simulate(tape, decisions, c), std::invalid_argument);
    c = config();
    c.fee_bps = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(simulate(tape, decisions, c), std::invalid_argument);
    c = config();
    c.initial_cash = 1e20;
    EXPECT_THROW(simulate(tape, decisions, c), std::overflow_error);
    c = config();
    c.initial_cash = 1;
    EXPECT_TRUE(simulate(tape, decisions, c).orders.empty());
    auto events = round_trip();
    events[2].price = 1e9;
    const auto r = simulate(PriceTape{events}, decisions, config());
    EXPECT_EQ(r.orders.front().status, "unaffordable");
}
