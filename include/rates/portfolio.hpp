#pragma once
#include "rates/risk_engine.hpp"
#include <optional>

namespace rates {
struct PortfolioTrade {
    std::string id;
    Swap trade;
};
struct TradeRisk {
    std::string id;
    double pv;
    double parallel_quote_dv01;
};
struct TradeFailure {
    std::string id;
    std::string error;
};
struct PortfolioResult {
    double pv = 0;
    double parallel_quote_dv01 = 0;
    std::vector<RiskBucket> quote_buckets;
    std::vector<RiskBucket> zero_nodes;
    std::vector<TradeRisk> trades;
    std::vector<TradeFailure> failures;
    std::size_t input_trades = 0;
    std::size_t schedule_cache_entries = 0;
    std::size_t calibration_count = 0;
    double pricing_ms = 0;
};
// Owns prepared trades; equal date/convention schedules share immutable storage.
// Invalid trades are reported individually; aggregates cover successful trades only.
class PortfolioRiskEngine {
  public:
    explicit PortfolioRiskEngine(const std::vector<PortfolioTrade> &trades);
    PortfolioResult calculate(const RiskScenarios &scenarios) const;
    std::size_t size() const { return trades_.size(); }
    std::size_t schedule_cache_entries() const { return schedule_count_; }

  private:
    struct Entry {
        std::string id;
        std::optional<PreparedSwap> prepared;
        std::string error;
    };
    std::vector<Entry> trades_;
    std::size_t schedule_count_ = 0;
};
} // namespace rates
