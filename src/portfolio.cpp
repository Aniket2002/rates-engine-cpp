#include "rates/portfolio.hpp"
#include <chrono>
#include <map>
#include <set>

namespace rates {
namespace {
std::string schedule_key(const Swap &trade) {
    const auto &c = trade.conventions;
    std::string key = trade.effective.str() + "/" + trade.maturity.str();
    for (const int value : {c.fixed_months, c.floating_months, static_cast<int>(c.fixed_day_count),
                            static_cast<int>(c.floating_day_count), static_cast<int>(c.deposit_day_count),
                            static_cast<int>(c.business_day), static_cast<int>(c.end_of_month)})
        key += "/" + std::to_string(value);
    for (const auto date : c.calendar.holidays())
        key += "/" + date.str();
    return key;
}
struct Sum {
    double value = 0, correction = 0;
    void add(double x) {
        const double adjusted = x - correction;
        const double next = value + adjusted;
        correction = (next - value) - adjusted;
        value = next;
        if (!std::isfinite(value))
            throw std::overflow_error("Portfolio aggregate overflow");
    }
};
} // namespace
PortfolioRiskEngine::PortfolioRiskEngine(const std::vector<PortfolioTrade> &trades) {
    if (trades.empty())
        throw std::invalid_argument("Portfolio must contain at least one trade");
    std::set<std::string> ids;
    std::map<std::string, PreparedSwap> schedules;
    trades_.reserve(trades.size());
    for (const auto &input : trades) {
        if (input.id.empty() || !ids.insert(input.id).second)
            throw std::invalid_argument("Portfolio trade IDs must be nonempty and unique");
        try {
            const auto key = schedule_key(input.trade);
            const auto found = schedules.find(key);
            if (found == schedules.end()) {
                auto prepared = PreparedSwap{input.trade};
                schedules.emplace(key, prepared);
                trades_.push_back({input.id, std::move(prepared), ""});
            } else {
                trades_.push_back({input.id,
                                   found->second.with_economics(input.trade.notional, input.trade.fixed_rate,
                                                                input.trade.side),
                                   ""});
            }
        } catch (const std::exception &error) {
            trades_.push_back({input.id, std::nullopt, error.what()});
        }
    }
    schedule_count_ = schedules.size();
}
PortfolioResult PortfolioRiskEngine::calculate(const RiskScenarios &scenarios) const {
    const auto start = std::chrono::steady_clock::now();
    PortfolioResult result;
    result.input_trades = trades_.size();
    result.schedule_cache_entries = schedule_count_;
    result.calibration_count = scenarios.calibration_count();
    result.trades.reserve(trades_.size());
    for (const auto &shock : scenarios.quote_shocks())
        result.quote_buckets.push_back({shock.label, shock.maturity, 0});
    for (const auto &shock : scenarios.zero_shocks())
        result.zero_nodes.push_back({shock.label, shock.maturity, 0});
    Sum pv, parallel;
    std::vector<Sum> quotes(result.quote_buckets.size()), zeros(result.zero_nodes.size());
    for (const auto &entry : trades_) {
        if (!entry.prepared) {
            result.failures.push_back({entry.id, entry.error});
            continue;
        }
        std::optional<RiskReport> report;
        try {
            report = scenarios.report(*entry.prepared, "portfolio");
        } catch (const std::exception &error) {
            result.failures.push_back({entry.id, error.what()});
            continue;
        }
        // Aggregate overflow is a portfolio failure, not a partially applied trade.
        pv.add(report->base.pv);
        parallel.add(report->parallel_quote_dv01);
        for (std::size_t i = 0; i < quotes.size(); ++i)
            quotes[i].add(report->quote_buckets[i].dv01);
        for (std::size_t i = 0; i < zeros.size(); ++i)
            zeros[i].add(report->zero_nodes[i].dv01);
        result.trades.push_back({entry.id, report->base.pv, report->parallel_quote_dv01});
    }
    result.pv = pv.value;
    result.parallel_quote_dv01 = parallel.value;
    for (std::size_t i = 0; i < quotes.size(); ++i)
        result.quote_buckets[i].dv01 = quotes[i].value;
    for (std::size_t i = 0; i < zeros.size(); ++i)
        result.zero_nodes[i].dv01 = zeros[i].value;
    result.pricing_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
} // namespace rates
