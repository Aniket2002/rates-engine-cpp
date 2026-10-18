#include "batch_io.hpp"
#include "rates/trading.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <map>

using namespace rates;
using namespace rates::trading;
namespace {
std::uint64_t integer(const std::string &text) {
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size() || value > 1000000000)
        throw std::invalid_argument("Expected nonnegative integer at most 1000000000");
    return value;
}
std::vector<std::vector<std::string>> read_csv(const std::string &path,
                                               const std::vector<std::string> &header) {
    std::ifstream input(path);
    std::string line;
    if (!input || !std::getline(input, line) || batch_io::split(line, header.size()) != header)
        throw std::invalid_argument("Missing file or invalid CSV header: " + path);
    std::vector<std::vector<std::string>> rows;
    while (std::getline(input, line))
        rows.push_back(batch_io::split(line, header.size()));
    if (input.bad())
        throw std::runtime_error("CSV read failed");
    return rows;
}
std::ofstream output(const std::filesystem::path &path) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream.open(path);
    stream << std::setprecision(17);
    return stream;
}
} // namespace
int main(int argc, char **argv) try {
    std::map<std::string, std::string> args;
    const std::vector<std::string> allowed{"--prices",
                                           "--decisions",
                                           "--out",
                                           "--symbol",
                                           "--basis",
                                           "--evidence",
                                           "--cash",
                                           "--lookback",
                                           "--half-spread-bps",
                                           "--fee-bps",
                                           "--latency-seconds",
                                           "--max-age-seconds",
                                           "--ttl-seconds",
                                           "--max-execution-delay-seconds"};
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (key == "--help") {
            std::cout << "rates_simulate --prices prices.csv --decisions decisions.csv --out NEW_DIR\n"
                         " --symbol TLT --basis retrospective|recorded_availability|synthetic\n"
                         " --evidence SOURCE_DESCRIPTION [--cash 100000 --lookback 5]\n"
                         " [--half-spread-bps 2 --fee-bps 1 --latency-seconds 1]\n"
                         " [--max-age-seconds 345600 --ttl-seconds 345600]\n"
                         " [--max-execution-delay-seconds 60]\n"
                         "Single unadjusted ETF, long/cash, action-free interval only.\n";
            return 0;
        }
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end() || i + 1 >= argc)
            throw std::invalid_argument("Unknown or incomplete option: " + key);
        ++i;
        if (!args.emplace(key, argv[i]).second)
            throw std::invalid_argument("Unknown, repeated or incomplete option: " + key);
    }
    for (const auto *key : {"--prices", "--decisions", "--out", "--symbol", "--basis", "--evidence"})
        if (!args.contains(key) || args.at(key).empty())
            throw std::invalid_argument(std::string("Missing required option: ") + key);
    const auto &basis = args.at("--basis");
    if (basis != "retrospective" && basis != "recorded_availability" && basis != "synthetic")
        throw std::invalid_argument("Unknown availability basis");
    SimulationConfig config;
    if (args.contains("--cash"))
        config.initial_cash = batch_io::number(args.at("--cash"));
    if (args.contains("--lookback"))
        config.lookback = static_cast<std::size_t>(integer(args.at("--lookback")));
    if (args.contains("--half-spread-bps"))
        config.half_spread_bps = batch_io::number(args.at("--half-spread-bps"));
    if (args.contains("--fee-bps"))
        config.fee_bps = batch_io::number(args.at("--fee-bps"));
    if (args.contains("--latency-seconds"))
        config.latency_seconds = static_cast<std::int64_t>(integer(args.at("--latency-seconds")));
    if (args.contains("--max-age-seconds"))
        config.max_price_age_seconds = static_cast<std::int64_t>(integer(args.at("--max-age-seconds")));
    if (args.contains("--ttl-seconds"))
        config.order_ttl_seconds = static_cast<std::int64_t>(integer(args.at("--ttl-seconds")));
    if (args.contains("--max-execution-delay-seconds"))
        config.max_execution_delay_seconds =
            static_cast<std::int64_t>(integer(args.at("--max-execution-delay-seconds")));
    std::vector<PriceEvent> events;
    for (const auto &row :
         read_csv(args.at("--prices"), {"event_time", "available_at", "kind", "price", "revision"})) {
        if (row[2] != "open" && row[2] != "close")
            throw std::invalid_argument("Price kind must be open or close");
        events.push_back({Timestamp::parse(row[0]), Timestamp::parse(row[1]),
                          row[2] == "open" ? PriceKind::Open : PriceKind::Close, batch_io::number(row[3]),
                          static_cast<unsigned>(integer(row[4]))});
    }
    std::vector<Timestamp> decisions;
    for (const auto &row : read_csv(args.at("--decisions"), {"decision_time"}))
        decisions.push_back(Timestamp::parse(row[0]));
    const auto result = simulate(PriceTape{std::move(events)}, decisions, config);
    const std::filesystem::path directory{args.at("--out")};
    if (std::filesystem::exists(directory))
        throw std::invalid_argument("Output directory already exists; choose a fresh run directory");
    std::filesystem::create_directories(directory);
    auto orders = output(directory / "orders.csv");
    orders
        << "id,decision_time,signal_event_time,signal_available_at,requested_shares,filled_shares,status\n";
    for (std::size_t i = 0; i < result.orders.size(); ++i) {
        const auto &o = result.orders[i];
        orders << i << ',' << o.decision_time.str() << ',' << o.signal_event_time.str() << ','
               << o.signal_available_at.str() << ',' << o.requested_shares << ',' << o.filled_shares << ','
               << o.status << '\n';
    }
    orders.close();
    auto fills = output(directory / "fills.csv");
    fills << "order_id,event_time,available_at,shares,reference_price,execution_price,fee,spread_cost\n";
    for (const auto &f : result.fills)
        fills << f.order_index << ',' << f.event_time.str() << ',' << f.available_at.str() << ',' << f.shares
              << ',' << f.reference_price << ',' << f.execution_price << ',' << f.fee << ',' << f.spread_cost
              << '\n';
    fills.close();
    auto equity = output(directory / "equity.csv");
    equity << "decision_time,mark_time,mark_available_at,mark,cash,shares,exposure,equity,pnl,drawdown,"
              "period_return\n";
    for (const auto &p : result.equity)
        equity << p.decision_time.str() << ',' << p.mark_time.str() << ',' << p.mark_available_at.str() << ','
               << p.mark << ',' << p.cash << ',' << p.shares << ',' << p.exposure << ',' << p.equity << ','
               << p.pnl << ',' << p.drawdown << ',' << p.period_return << '\n';
    equity.close();
    auto summary = output(directory / "summary.json");
    summary << "{\n  \"schema_version\": 1,\n  \"status\": \"success\",\n  \"symbol\": "
            << batch_io::json_string(args.at("--symbol"))
            << ",\n  \"availability_basis\": " << batch_io::json_string(basis)
            << ",\n  \"availability_evidence\": " << batch_io::json_string(args.at("--evidence"))
            << ",\n  \"point_in_time_certified\": false,\n  \"execution_model\": "
               "\"next_original_open_with_costs\",\n"
               "  \"return_basis\": \"price_return_action_free_interval\",\n  \"initial_cash\": "
            << config.initial_cash << ",\n  \"lookback\": " << config.lookback
            << ",\n  \"half_spread_bps\": " << config.half_spread_bps
            << ",\n  \"fee_bps\": " << config.fee_bps
            << ",\n  \"latency_seconds\": " << config.latency_seconds
            << ",\n  \"max_price_age_seconds\": " << config.max_price_age_seconds
            << ",\n  \"order_ttl_seconds\": " << config.order_ttl_seconds
            << ",\n  \"max_execution_delay_seconds\": " << config.max_execution_delay_seconds
            << ",\n  \"decision_count\": " << result.equity.size()
            << ",\n  \"order_count\": " << result.orders.size()
            << ",\n  \"fill_count\": " << result.fills.size()
            << ",\n  \"final_equity\": " << result.equity.back().equity
            << ",\n  \"pnl\": " << result.equity.back().pnl
            << ",\n  \"max_drawdown\": " << result.max_drawdown
            << ",\n  \"period_volatility\": " << result.period_volatility
            << ",\n  \"turnover\": " << result.turnover << ",\n  \"fees\": " << result.total_fees
            << ",\n  \"spread_cost\": " << result.total_spread_cost << "\n}\n";
    summary.close();
    return 0;
} catch (const std::exception &error) {
    std::cerr << "Trading simulation failed: " << error.what() << '\n';
    return 1;
}
