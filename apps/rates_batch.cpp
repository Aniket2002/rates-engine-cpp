#include "batch_io.hpp"
#include "rates/history.hpp"
#include <chrono>
#include <iostream>
#include <map>

using namespace rates;
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
struct InputContext {
    std::string source = "unspecified", replay_basis = "provided_timestamps";
};
struct Summary {
    InputContext context;
    std::string as_of, status = "failed", category, error, calibration = "not_attempted";
    std::optional<Timestamp> observed;
    std::optional<std::int64_t> age;
    std::optional<PortfolioResult> portfolio;
    std::size_t input_trades = 0;
    double processing_ms = 0, setup_ms = 0, calibration_ms = 0;
    bool scenarios_reused = false;
};
std::ofstream output(const std::filesystem::path &path) {
    std::ofstream stream(path);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << std::setprecision(17);
    return stream;
}
std::string summary_json(const Summary &s) {
    using batch_io::json_string;
    std::ostringstream out;
    out << std::setprecision(17) << "{\"schema_version\":1,\"as_of\":" << json_string(s.as_of)
        << ",\"status\":" << json_string(s.status) << ",\"error_category\":" << json_string(s.category)
        << ",\"error\":" << json_string(s.error) << ",\"data_source\":" << json_string(s.context.source)
        << ",\"replay_basis\":" << json_string(s.context.replay_basis)
        << ",\"snapshot_timestamp\":" << (s.observed ? json_string(s.observed->str()) : "null")
        << ",\"data_age_seconds\":" << (s.age ? std::to_string(*s.age) : "null")
        << ",\"calibration_status\":" << json_string(s.calibration)
        << ",\"processing_ms\":" << s.processing_ms << ",\"setup_ms\":" << s.setup_ms
        << ",\"calibration_ms\":" << s.calibration_ms
        << ",\"scenarios_reused\":" << (s.scenarios_reused ? "true" : "false")
        << ",\"input_trades\":" << s.input_trades
        << ",\"priced_trades\":" << (s.portfolio ? s.portfolio->trades.size() : 0)
        << ",\"failed_trades\":" << (s.portfolio ? std::to_string(s.portfolio->failures.size()) : "null")
        << ",\"schedule_cache_entries\":" << (s.portfolio ? s.portfolio->schedule_cache_entries : 0)
        << ",\"scenario_calibrations\":" << (s.portfolio ? s.portfolio->calibration_count : 0)
        << ",\"calibrations_this_run\":"
        << (s.scenarios_reused ? "0"
                               : (s.portfolio ? std::to_string(s.portfolio->calibration_count) : "null"))
        << ",\"aggregate_scope\":\"successfully_priced_trades_only\",\"portfolio_pv\":";
    if (s.portfolio && !s.portfolio->trades.empty())
        out << s.portfolio->pv;
    else
        out << "null";
    out << ",\"parallel_quote_dv01\":";
    if (s.portfolio && !s.portfolio->trades.empty())
        out << s.portfolio->parallel_quote_dv01;
    else
        out << "null";
    return out.str() + '}';
}
void write_report(const std::filesystem::path &folder, const Summary &s) {
    std::filesystem::create_directories(folder);
    auto trades = output(folder / "trades.csv"), failures = output(folder / "failed_trades.csv"),
         risk = output(folder / "risk.csv");
    trades << "id,pv,parallel_quote_dv01\n";
    failures << "id,error\n";
    risk << "coordinate,label,maturity,dv01\n";
    if (s.portfolio) {
        for (const auto &t : s.portfolio->trades)
            trades << batch_io::csv_string(t.id) << ',' << t.pv << ',' << t.parallel_quote_dv01 << '\n';
        for (const auto &f : s.portfolio->failures)
            failures << batch_io::csv_string(f.id) << ',' << batch_io::csv_string(f.error) << '\n';
        if (!s.portfolio->trades.empty()) {
            for (const auto &b : s.portfolio->quote_buckets)
                risk << "market_quote," << batch_io::csv_string(b.label) << ',' << b.maturity.str() << ','
                     << b.dv01 << '\n';
            for (const auto &b : s.portfolio->zero_nodes)
                risk << "zero_node," << batch_io::csv_string(b.label) << ',' << b.maturity.str() << ','
                     << b.dv01 << '\n';
        }
    }
    trades.close();
    failures.close();
    risk.close();
    auto summary = output(folder / "summary.json");
    summary << summary_json(s) << '\n';
    summary.close();
}
void data_error(Summary &s, const MarketDataError &error) {
    s.category =
        error.code() == MarketDataFailure::Missing
            ? "missing_market_data"
            : (error.code() == MarketDataFailure::Stale ? "stale_market_data" : "invalid_market_data");
    s.error = error.what();
    s.observed = error.observed();
    s.age = error.age_seconds();
}
std::int64_t integer(const std::string &text, bool positive) {
    std::size_t used = 0;
    const auto value = std::stoll(text, &used);
    if (used != text.size() || value < (positive ? 1 : 0) || value > 10000000000LL)
        throw std::invalid_argument("Expected bounded nonnegative integer seconds");
    return value;
}
Summary run(Timestamp time, std::int64_t max_age, const HistoricalQuoteStore &history,
            const PortfolioRiskEngine &portfolio, std::optional<RiskScenarios> &cache,
            std::optional<Timestamp> &cached_time, double setup_ms, const InputContext &context) {
    const auto start = Clock::now();
    Summary summary;
    summary.context = context;
    summary.as_of = time.str();
    summary.input_trades = portfolio.size();
    summary.setup_ms = setup_ms;
    try {
        const auto snapshot = history.as_of(time, max_age);
        summary.observed = snapshot.observed;
        summary.age = snapshot.age_seconds;
        summary.category = "calibration";
        summary.calibration = "failed";
        if (!cached_time || *cached_time != snapshot.observed) {
            const auto calibration_start = Clock::now();
            try {
                auto next = RiskEngine{}.prepare_scenarios(snapshot.valuation, snapshot.quotes);
                cache = std::move(next);
            } catch (...) {
                summary.calibration_ms = elapsed(calibration_start);
                throw;
            }
            summary.calibration_ms = elapsed(calibration_start);
            cached_time = snapshot.observed;
        } else
            summary.scenarios_reused = true;
        summary.calibration = "success";
        summary.category = "portfolio";
        if (!cache.has_value())
            throw std::logic_error("Scenario cache is empty after calibration");
        summary.portfolio = portfolio.calculate(cache.value());
        summary.status = summary.portfolio->trades.empty()
                             ? "failed"
                             : (summary.portfolio->failures.empty() ? "success" : "partial");
        if (summary.status == "success")
            summary.category.clear();
        else
            summary.error = "One or more trades could not be priced; inspect failed_trades.csv";
    } catch (const MarketDataError &error) {
        data_error(summary, error);
    } catch (const std::exception &error) {
        summary.error = error.what();
    }
    summary.processing_ms = setup_ms + elapsed(start);
    return summary;
}
} // namespace
int main(int argc, char **argv) try {
    const auto start = Clock::now();
    std::map<std::string, std::string> args{{"--history", "examples/quote_history.csv"},
                                            {"--portfolio", "examples/portfolio.csv"},
                                            {"--out", "results/batch"},
                                            {"--as-of", ""},
                                            {"--replay-from", ""},
                                            {"--replay-to", ""},
                                            {"--step-seconds", "300"},
                                            {"--data-source", "unspecified"},
                                            {"--replay-basis", "provided_timestamps"},
                                            {"--max-age-seconds", "900"}};
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") {
                std::cout << "rates_batch --history history.csv --portfolio portfolio.csv --out directory\n"
                          << "  --as-of YYYY-MM-DDTHH:MM:SSZ --max-age-seconds N\n"
                          << "  or --replay-from UTC --replay-to UTC --step-seconds N\n"
                          << "Exit 0: success; 2: partial trades; 1: failed batch/data/calibration. "
                             "Use --data-source label and --replay-basis "
                             "provided_timestamps|retrospective_observation_date.\n";
                return 0;
            }
            if (!args.contains(option) || i + 1 >= argc)
                throw std::invalid_argument("Unknown or incomplete batch option: " + option);
            args[option] = argv[++i];
        }
        const InputContext context{args.at("--data-source"), args.at("--replay-basis")};
        if (context.replay_basis != "provided_timestamps" &&
            context.replay_basis != "retrospective_observation_date")
            throw std::invalid_argument("Unsupported replay basis");
        const bool replay = !args.at("--replay-from").empty();
        if (replay ? (!args.at("--as-of").empty() || args.at("--replay-to").empty())
                   : (args.at("--as-of").empty() || !args.at("--replay-to").empty()))
            throw std::invalid_argument("Specify exactly one as-of time or a complete replay range");
        const auto first = Timestamp::parse(args.at(replay ? "--replay-from" : "--as-of"));
        const auto last = replay ? Timestamp::parse(args.at("--replay-to")) : first;
        const auto step = integer(args.at("--step-seconds"), true),
                   max_age = integer(args.at("--max-age-seconds"), false);
        if (last < first || (last - first) / step >= 10000)
            throw std::invalid_argument("Replay requires ordered range with at most 10000 observations");
        const HistoricalQuoteStore history{args.at("--history")};
        const PortfolioRiskEngine portfolio{batch_io::read_portfolio(args.at("--portfolio"))};
        const double setup_ms = elapsed(start);
        std::optional<RiskScenarios> cache;
        std::optional<Timestamp> cached_time;
        const std::filesystem::path root = args.at("--out");
        std::filesystem::create_directories(root);
        std::optional<std::ofstream> events;
        if (replay)
            events.emplace(output(root / "replay.jsonl"));
        int exit_code = 0;
        for (auto current = first;; current = current.add_seconds(step)) {
            const auto summary = run(current, max_age, history, portfolio, cache, cached_time,
                                     current == first ? setup_ms : 0, context);
            std::string key = current.str();
            std::erase(key, ':');
            std::erase(key, '-');
            write_report(replay ? root / key : root, summary);
            const auto json = summary_json(summary);
            std::cout << json << '\n';
            if (events) {
                *events << json << '\n';
                events->flush();
            }
            if (summary.status == "failed")
                exit_code = 1;
            else if (summary.status == "partial" && exit_code == 0)
                exit_code = 2;
            if (last - current < step)
                break;
        }
        if (events)
            events->close();
        return exit_code;
    } catch (const std::exception &error) {
        Summary summary;
        summary.context = {args.at("--data-source"), args.at("--replay-basis")};
        summary.as_of = args.at("--as-of").empty() ? args.at("--replay-from") : args.at("--as-of");
        summary.category = "invalid_input";
        if (dynamic_cast<const std::ios_base::failure *>(&error) ||
            dynamic_cast<const std::filesystem::filesystem_error *>(&error))
            summary.category = "file_io";
        summary.error = error.what();
        summary.processing_ms = elapsed(start);
        if (const auto *data = dynamic_cast<const MarketDataError *>(&error))
            data_error(summary, *data);
        try {
            write_report(args.at("--out"), summary);
        } catch (const std::exception &io) {
            std::cerr << "Cannot write operational summary: " << io.what() << '\n';
        }
        std::cout << summary_json(summary) << '\n';
        return 1;
    }
} catch (const std::exception &error) {
    std::cerr << "Fatal batch failure: " << error.what() << '\n';
    return 1;
}
