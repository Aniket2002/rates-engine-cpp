#include "rates/history.hpp"
#include "rates/portfolio.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>

using namespace rates;
namespace {
const Date valuation{2025, 1, 2};
std::vector<MarketQuote> market() {
    std::vector<MarketQuote> result;
    for (const int months : {1, 3, 6, 12, 24, 36, 60, 120})
        result.push_back({months <= 6 ? Instrument::Deposit : Instrument::Swap, std::to_string(months),
                          valuation.add_months(months), SimpleRate{0.03 + months * 0.00005}});
    return result;
}
Swap swap_trade() {
    return {valuation, valuation.add_months(60), 1e6, SimpleRate{0.032}};
}
struct HistoryFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("rates-history-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".csv");
    void write(const std::string &body) const {
        std::ofstream file(path, std::ios::binary);
        file << "timestamp,valuation_date,type,label,maturity,rate\n" << body;
    }
    ~HistoryFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
const std::string early = "2025-01-02T10:00:00Z,2025-01-02,deposit,1M,2025-02-02,0.03\n"
                          "2025-01-02T10:00:00Z,2025-01-02,deposit,6M,2025-07-02,0.032\n";
const std::string later = "2025-01-02T10:05:00Z,2025-01-02,deposit,1M,2025-02-02,0.04\n"
                          "2025-01-02T10:05:00Z,2025-01-02,deposit,6M,2025-07-02,0.042\n";
} // namespace
TEST(Timestamps, UtcValidationAndCalendarRollover) {
    const auto leap = Timestamp::parse("2024-02-29T23:59:59Z");
    EXPECT_EQ(leap.add_seconds(1).str(), "2024-03-01T00:00:00Z");
    EXPECT_EQ(Timestamp::parse("1969-12-31T23:59:59Z").str(), "1969-12-31T23:59:59Z");
    EXPECT_EQ(leap - leap.add_seconds(-31), 31);
    for (const auto *text : {"2025-02-29T00:00:00Z", "2025-01-01T24:00:00Z", "2025-01-01T10:60:00Z",
                             "2025-01-01T10:00:60Z", "2025-01-01T1x:00:00Z", "2025-01-01T10:00:00+00:00"})
        EXPECT_THROW(Timestamp::parse(text), std::invalid_argument);
    EXPECT_THROW(leap.add_seconds(10000000001LL), std::out_of_range);
    EXPECT_THROW(Timestamp::parse("2199-12-31T23:59:59Z").add_seconds(1), std::invalid_argument);
}
TEST(History, AsOfNeverSelectsFutureAndFreshnessBoundaryIsInclusive) {
    HistoryFile file;
    file.write(early + later);
    const HistoricalQuoteStore history{file.path};
    EXPECT_EQ(history.snapshot_count(), 2U);
    const auto before = history.as_of(Timestamp::parse("2025-01-02T10:04:59Z"), 300);
    EXPECT_EQ(before.age_seconds, 299);
    EXPECT_DOUBLE_EQ(before.quotes.front().rate.value, 0.03);
    const auto exact = history.as_of(Timestamp::parse("2025-01-02T10:05:00Z"), 0);
    EXPECT_DOUBLE_EQ(exact.quotes.front().rate.value, 0.04);
    EXPECT_EQ(history.as_of(Timestamp::parse("2025-01-02T10:10:00Z"), 300).age_seconds, 300);
    try {
        (void)history.as_of(Timestamp::parse("2025-01-02T10:10:01Z"), 300);
        FAIL();
    } catch (const MarketDataError &error) {
        EXPECT_EQ(error.code(), MarketDataFailure::Stale);
        EXPECT_EQ(*error.age_seconds(), 301);
        EXPECT_EQ(error.observed()->str(), "2025-01-02T10:05:00Z");
    }
    EXPECT_THROW(history.as_of(Timestamp::parse("2025-01-02T09:59:59Z"), 300), MarketDataError);
    EXPECT_THROW(history.as_of(Timestamp::parse("2025-01-02T10:00:00Z"), -1), std::invalid_argument);
}
TEST(History, RejectsMissingDuplicateUnorderedAndChangedData) {
    HistoryFile file;
    EXPECT_THROW(HistoricalQuoteStore{file.path}, MarketDataError);
    for (const auto &body : {std::string{}, later + early, early + early,
                             early + "2025-01-02T10:05:00Z,2025-01-02,deposit,1M,2025-02-02,0.04\n",
                             std::string{"2025-01-02T10:00:00Z,2025-01-03,deposit,1M,2025-02-02,0.04\n"},
                             std::string{"2025-01-02T10:00:00Z,2025-01-02,deposit,1M,2025-02-02,nan\n"}}) {
        file.write(body);
        EXPECT_THROW(HistoricalQuoteStore{file.path}, MarketDataError);
    }
    file.write(early + later);
    const HistoricalQuoteStore history{file.path};
    file.write(early);
    EXPECT_THROW(history.as_of(Timestamp::parse("2025-01-02T10:00:00Z"), 0), MarketDataError);
}
TEST(History, FutureQuoteChangesCannotChangeEarlierValuation) {
    HistoryFile original, altered;
    original.write(early + later);
    auto future = later;
    for (std::size_t offset = 0; (offset = future.find("0.04", offset)) != std::string::npos; offset += 4)
        future.replace(offset, 4, "0.09");
    altered.write(early + future);
    const auto query = Timestamp::parse("2025-01-02T10:04:00Z");
    const auto first = HistoricalQuoteStore{original.path}.as_of(query, 300);
    const auto second = HistoricalQuoteStore{altered.path}.as_of(query, 300);
    const Swap short_trade{valuation, valuation.add_months(3), 1e6, SimpleRate{0.03}};
    EXPECT_DOUBLE_EQ(price(CurveBootstrapper{}.build(valuation, first.quotes).curve, short_trade).pv,
                     price(CurveBootstrapper{}.build(valuation, second.quotes).curve, short_trade).pv);
}
TEST(History, RejectsMalformedCsvAndAcceptsCrLf) {
    HistoryFile file;
    for (const auto &body :
         {std::string{"bad\n"}, std::string{"2025-01-02T10:00:00Z,2025-01-02,deposit,,2025-02-02,0.03\n"},
          std::string{"2025-01-02T10:00:00Z,2025-01-02,unknown,1M,2025-02-02,0.03\n"},
          std::string{"2025-01-02T10:00:00Z,2025-01-02,deposit,1M,2025-02-02,0.03x\n"},
          std::string{"2025-01-02T10:00:00Z,2025-01-02,deposit,1M,2025-02-02,0.03,\n"}}) {
        file.write(body);
        EXPECT_THROW(HistoricalQuoteStore{file.path}, MarketDataError);
    }
    file.write("2025-01-02T10:00:00Z,2025-01-02,deposit,1M,2025-02-02,0.03\r\n");
    EXPECT_EQ(HistoricalQuoteStore{file.path}.snapshot_count(), 1U);
    {
        std::ofstream invalid(file.path);
        invalid << "wrong-header\n";
    }
    EXPECT_THROW(HistoricalQuoteStore{file.path}, MarketDataError);
}
TEST(Portfolio, ThousandTradesAggregateAndShareSchedules) {
    const auto scenarios = RiskEngine{}.prepare_scenarios(valuation, market());
    std::vector<PortfolioTrade> trades;
    trades.reserve(1000);
    double scale = 0;
    for (int i = 0; i < 1000; ++i) {
        auto trade = swap_trade();
        trade.notional *= 1 + i % 7;
        if (i % 3 == 0)
            trade.side = Side::Receiver;
        scale += (trade.side == Side::Payer ? 1 : -1) * (1 + i % 7);
        trades.push_back({std::to_string(i), trade});
    }
    const PortfolioRiskEngine portfolio{trades};
    const auto result = portfolio.calculate(scenarios);
    const auto unit = scenarios.report(swap_trade(), "unit");
    EXPECT_EQ(portfolio.schedule_cache_entries(), 1U);
    EXPECT_EQ(result.trades.size(), 1000U);
    EXPECT_TRUE(result.failures.empty());
    EXPECT_EQ(result.calibration_count, 19U);
    EXPECT_NEAR(result.pv, scale * unit.base.pv, 1e-6);
    EXPECT_NEAR(result.parallel_quote_dv01, scale * unit.parallel_quote_dv01, 1e-6);
    for (std::size_t i = 0; i < result.quote_buckets.size(); ++i)
        EXPECT_NEAR(result.quote_buckets[i].dv01, scale * unit.quote_buckets[i].dv01, 1e-6);
}
TEST(Portfolio, SharedScheduleOwnershipAndConventionKeys) {
    const PreparedSwap base{swap_trade()};
    const auto changed = base.with_economics(2e6, SimpleRate{0.04}, Side::Receiver);
    EXPECT_EQ(base.fixed_periods().data(), changed.fixed_periods().data());
    EXPECT_EQ(base.floating_periods().data(), changed.floating_periods().data());
    auto different = swap_trade();
    different.conventions.calendar = Calendar{{Date{2026, 1, 2}}};
    const PortfolioRiskEngine portfolio{{{"a", swap_trade()}, {"b", different}, {"c", swap_trade()}}};
    EXPECT_EQ(portfolio.schedule_cache_entries(), 2U);
}
TEST(Portfolio, FailedTradesAreExplicitAndDoNotPolluteAggregates) {
    const auto scenarios = RiskEngine{}.prepare_scenarios(valuation, market());
    auto bad = swap_trade();
    bad.notional = -1;
    auto expired = swap_trade();
    expired.effective = valuation.add_days(-1);
    auto outside = swap_trade();
    outside.maturity = valuation.add_months(121);
    const PortfolioRiskEngine portfolio{
        {{"valid", swap_trade()}, {"bad", bad}, {"past", expired}, {"outside", outside}}};
    const auto result = portfolio.calculate(scenarios);
    EXPECT_EQ(result.trades.size(), 1U);
    EXPECT_EQ(result.failures.size(), 3U);
    EXPECT_DOUBLE_EQ(result.pv, scenarios.report(swap_trade(), "reference").base.pv);
    const auto failed = PortfolioRiskEngine{{{"bad", bad}}}.calculate(scenarios);
    EXPECT_TRUE(failed.trades.empty());
    EXPECT_EQ(failed.failures.size(), 1U);
    EXPECT_THROW((PortfolioRiskEngine{{{"same", swap_trade()}, {"same", swap_trade()}}}),
                 std::invalid_argument);
    EXPECT_THROW((PortfolioRiskEngine{std::vector<PortfolioTrade>{}}), std::invalid_argument);
}
TEST(Portfolio, AggregateOverflowFailsTheWholeCalculation) {
    auto huge = swap_trade();
    huge.notional = 1e308;
    huge.fixed_rate = SimpleRate{0.1};
    std::vector<PortfolioTrade> trades;
    for (int i = 0; i < 20; ++i)
        trades.push_back({std::to_string(i), huge});
    const auto scenarios = RiskEngine{}.prepare_scenarios(valuation, market());
    EXPECT_TRUE(std::isfinite(scenarios.report(huge, "unit").base.pv));
    EXPECT_THROW(PortfolioRiskEngine{trades}.calculate(scenarios), std::overflow_error);
}
