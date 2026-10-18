#pragma once
#include "rates/history.hpp"
#include <map>

namespace rates::trading {
enum class PriceKind : unsigned char { Open, Close };
// Unadjusted USD prices for ONE instrument. Revisions retain the same event time.
// available_at is receipt/publication time, never an inferred observation date.
struct PriceEvent {
    Timestamp event_time;
    Timestamp available_at;
    PriceKind kind;
    double price;
    unsigned revision = 0;
};
class PriceTape {
  public:
    explicit PriceTape(std::vector<PriceEvent> events);
    // Strictly before decision: even an equal availability timestamp is excluded.
    std::vector<PriceEvent> closes_before(Timestamp decision, std::size_t count) const;
    const std::vector<PriceEvent> &events() const { return events_; }

  private:
    std::vector<PriceEvent> events_;
    std::map<std::pair<Timestamp, PriceKind>, std::vector<std::size_t>> versions_;
};
struct SimulationConfig {
    double initial_cash = 100000;
    std::size_t lookback = 5;
    double half_spread_bps = 2;
    double fee_bps = 1;
    std::int64_t latency_seconds = 1;
    std::int64_t max_price_age_seconds = 345600;
    std::int64_t order_ttl_seconds = 345600;
    std::int64_t max_execution_delay_seconds = 60;
};
struct Order {
    Timestamp decision_time;
    Timestamp signal_event_time;
    Timestamp signal_available_at;
    double requested_shares; // signed whole shares, bounded to exact double integers
    double filled_shares = 0;
    std::string status = "pending";
};
struct Fill {
    std::size_t order_index;
    Timestamp event_time;
    Timestamp available_at;
    double shares;
    double reference_price;
    double execution_price;
    double fee;
    double spread_cost;
};
struct EquityPoint {
    Timestamp decision_time;
    Timestamp mark_time;
    Timestamp mark_available_at;
    double mark;
    double cash;
    double shares;
    double exposure;
    double equity;
    double pnl;
    double drawdown;
    double period_return;
};
struct SimulationResult {
    std::vector<Order> orders;
    std::vector<Fill> fills;
    std::vector<EquityPoint> equity;
    double total_fees = 0;
    double total_spread_cost = 0;
    double gross_traded_notional = 0;
    double turnover = 0;          // gross execution notional / initial cash
    double max_drawdown = 0;      // sampled at decision times, including initial capital
    double period_volatility = 0; // sample standard deviation; NOT annualized
};
// Close > trailing mean => long; otherwise cash. Fixed whole-share orders are
// formed at decisions and filled once at the next eligible original open event.
// No shorting, leverage, interest, dividends, splits or terminal liquidation.
SimulationResult simulate(const PriceTape &tape, const std::vector<Timestamp> &decisions,
                          const SimulationConfig &config = {});
} // namespace rates::trading
