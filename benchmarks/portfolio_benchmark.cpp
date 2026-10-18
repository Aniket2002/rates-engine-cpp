#include "rates/portfolio.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

using namespace rates;
namespace {
volatile double sink = 0;
struct Totals {
    double pv = 0, dv01 = 0;
    std::vector<double> buckets = std::vector<double>(16, 0);
    void add(const RiskReport &r) {
        pv += r.base.pv;
        dv01 += r.parallel_quote_dv01;
        for (std::size_t i = 0; i < 8; ++i) {
            buckets[i] += r.quote_buckets.at(i).dv01;
            buckets[i + 8] += r.zero_nodes.at(i).dv01;
        }
    }
};
void check(const Totals &a, const PortfolioResult &b) {
    const auto close = [](double x, double y) {
        if (std::abs(x - y) > 1e-6 + 1e-10 * std::max(std::abs(x), std::abs(y)))
            throw std::runtime_error("Portfolio benchmark numerical regression");
    };
    close(a.pv, b.pv);
    close(a.dv01, b.parallel_quote_dv01);
    for (std::size_t i = 0; i < 8; ++i) {
        close(a.buckets[i], b.quote_buckets.at(i).dv01);
        close(a.buckets[i + 8], b.zero_nodes.at(i).dv01);
    }
}
template <class F>
void measure(std::ostream &out, const char *name, int n, int samples, std::size_t calibrations,
             std::size_t schedules, F function) {
    sink = function(); // One untimed warm-up; construction boundaries are described in docs/v2.md.
    std::vector<double> times;
    times.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = function();
        const auto end = std::chrono::steady_clock::now();
        sink = result;
        times.push_back(std::chrono::duration<double, std::micro>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    const auto percentile = [&](double p) {
        return times.at(static_cast<std::size_t>(std::ceil(p * samples)) - 1);
    };
    const double mean = std::accumulate(times.begin(), times.end(), 0.0) / samples;
    out << name << ',' << n << ',' << samples << ',' << mean << ',' << percentile(.5) << ','
        << percentile(.95) << ',' << percentile(.99) << ',' << n * 1e6 / mean << ',' << calibrations << ','
        << schedules << ',' << RATES_COMPILER << ',' << RATES_BUILD_CONFIG << ',' << RATES_PLATFORM << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc < 2 || argc > 5)
            throw std::invalid_argument("Usage: rates_portfolio_benchmark output.csv [samples=21] "
                                        "[max_trades=10000] [all|cached|uncached|independent]");
        const auto integer = [](const char *s) {
            std::size_t used = 0;
            const int n = std::stoi(s, &used);
            if (used != std::string(s).size())
                throw std::invalid_argument("Invalid integer");
            return n;
        };
        const int samples = argc > 2 ? integer(argv[2]) : 21;
        const int maximum = argc > 3 ? integer(argv[3]) : 10000;
        const std::string mode = argc > 4 ? argv[4] : "all";
        if (samples < 1 || samples > 10000 || (maximum != 100 && maximum != 1000 && maximum != 10000) ||
            (mode != "all" && mode != "cached" && mode != "uncached" && mode != "independent"))
            throw std::invalid_argument("Invalid benchmark configuration");
        std::ofstream out(argv[1]);
        out.exceptions(std::ios::badbit | std::ios::failbit);
        out << std::setprecision(12)
            << "workload,trades,samples,mean_us,p50_us,p95_us,p99_us,trades_per_second,calibrations_per_"
               "iteration,schedule_builds_per_iteration,compiler,configuration,platform\n";
        const Date valuation{2025, 1, 2};
        const std::vector<int> months{1, 3, 6, 12, 24, 36, 60, 120};
        std::vector<MarketQuote> quotes;
        quotes.reserve(months.size());
        for (std::size_t i = 0; i < months.size(); ++i)
            quotes.push_back({i < 3 ? Instrument::Deposit : Instrument::Swap, std::to_string(months[i]),
                              valuation.add_months(months[i]),
                              SimpleRate{.025 + .0015 * static_cast<double>(i)}});
        const RiskEngine risk;
        const auto scenarios = risk.prepare_scenarios(valuation, quotes);
        if (scenarios.calibration_count() != 19)
            throw std::runtime_error("Scenario calibration count regression");
        for (const int n : {100, 1000, 10000}) {
            if (n > maximum || (mode == "independent" && n > 100))
                break;
            std::vector<PortfolioTrade> trades;
            trades.reserve(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
                trades.push_back(
                    {std::to_string(i), Swap{valuation.add_days(4), valuation.add_months(24 + 12 * (i % 5)),
                                             100000.0 * (1 + i % 7), SimpleRate{.028 + .0002 * (i % 9)},
                                             i % 3 == 0 ? Side::Receiver : Side::Payer}});
            const PortfolioRiskEngine portfolio{trades};
            const auto cached = portfolio.calculate(scenarios);
            if (!cached.failures.empty() || cached.trades.size() != static_cast<std::size_t>(n) ||
                portfolio.schedule_cache_entries() != 5 || cached.calibration_count != 19)
                throw std::runtime_error("Portfolio work-count regression");
            const auto uncached = [&] {
                Totals totals;
                for (const auto &trade : trades)
                    totals.add(scenarios.report(trade.trade, "benchmark"));
                return totals;
            };
            const auto independent = [&] {
                Totals totals;
                for (const auto &trade : trades)
                    totals.add(risk.report(valuation, quotes, trade.trade, "benchmark"));
                return totals;
            };
            // Profile modes isolate one path; default mode verifies all coordinates before timing.
            if (mode == "all") {
                check(uncached(), cached);
                if (n == 100)
                    check(independent(), cached);
            }
            if (mode == "all" || mode == "cached")
                measure(out, "cached", n, samples, 0, 0, [&] { return portfolio.calculate(scenarios).pv; });
            if (mode == "all" || mode == "uncached")
                measure(out, "uncached_schedules", n, samples, 0, n, [&] { return uncached().pv; });
            if (mode == "all")
                measure(out, "end_to_end", n, samples, 19, 5, [&] {
                    const auto fresh = risk.prepare_scenarios(valuation, quotes);
                    return PortfolioRiskEngine{trades}.calculate(fresh).pv;
                });
            if (n == 100 && (mode == "all" || mode == "independent"))
                measure(out, "independent", n, samples, 19 * static_cast<std::size_t>(n), n,
                        [&] { return independent().pv; });
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
