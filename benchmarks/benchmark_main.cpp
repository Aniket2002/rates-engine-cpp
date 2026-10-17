#include "rates/risk_engine.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>

using namespace rates;
namespace {
volatile double sink = 0;
template <class Function>
void measure(std::ostream &out, const char *name, int samples, int workload, Function function) {
    for (int i = 0; i < 3; ++i)
        sink = function();
    std::vector<double> elapsed;
    elapsed.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        const auto begin = std::chrono::steady_clock::now();
        const double value = function();
        const auto end = std::chrono::steady_clock::now();
        sink = value;
        elapsed.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    std::sort(elapsed.begin(), elapsed.end());
    const auto size = elapsed.size();
    const double median = size % 2 == 0 ? (elapsed[size / 2 - 1] + elapsed[size / 2]) / 2 : elapsed[size / 2];
    out << name << ',' << std::accumulate(elapsed.begin(), elapsed.end(), 0.0) / samples << ',' << median
        << ',' << elapsed.front() << ',' << samples << ',' << workload << ',' << RATES_COMPILER << ','
        << RATES_BUILD_CONFIG << ',' << RATES_PLATFORM << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc > 3)
            throw std::invalid_argument("Usage: rates_benchmark [output.csv] [samples>=3]");
        int samples = 25;
        if (argc == 3) {
            std::size_t used = 0;
            samples = std::stoi(argv[2], &used);
            if (used != std::string(argv[2]).size() || samples < 3 || samples > 10000)
                throw std::invalid_argument("Samples must be 3..10000");
        }
        std::ofstream file;
        if (argc >= 2) {
            file.open(argv[1]);
            file.exceptions(std::ios::badbit | std::ios::failbit);
        }
        std::ostream &out = argc >= 2 ? file : std::cout;
        out << std::setprecision(12)
            << "workload,mean_us,median_us,min_us,iterations,workload_size,compiler,configuration,platform\n";
        const Date v{2025, 1, 2};
        std::vector<MarketQuote> quotes;
        const std::vector<int> months{1, 3, 6, 12, 24, 36, 60, 120};
        const std::vector<double> rates{0.031, 0.032, 0.033, 0.034, 0.035, 0.036, 0.038, 0.04};
        quotes.reserve(months.size());
        for (std::size_t i = 0; i < months.size(); ++i)
            quotes.push_back({i < 3 ? Instrument::Deposit : Instrument::Swap, std::to_string(months[i]) + "M",
                              v.add_months(months[i]), SimpleRate{rates[i]}});
        const CurveBootstrapper bootstrapper;
        const auto curve = bootstrapper.build(v, quotes).curve;
        const Swap trade{v, v.add_months(60), 1e6, SimpleRate{0.037}};
        const RiskEngine risk;
        const PreparedSwap prepared{trade};
        std::vector<Swap> portfolio;
        portfolio.reserve(20);
        for (int i = 0; i < 20; ++i) {
            auto t = trade;
            t.maturity = v.add_months(12 + i * 3);
            portfolio.push_back(t);
        }
        const auto shared_risk = risk.portfolio_parallel_dv01(v, quotes, portfolio);
        for (std::size_t i = 0; i < portfolio.size(); ++i)
            if (std::abs(shared_risk[i] - risk.parallel_quote_dv01(v, quotes, portfolio[i])) > 1e-8)
                throw std::runtime_error("Shared and independent risk disagree before timing");
        std::vector<YearFraction> queries;
        queries.reserve(10000);
        for (int i = 0; i < 10000; ++i)
            queries.emplace_back(static_cast<double>((i * 37) % 3650) / 365);
        measure(out, "discount_10000", samples, 10000, [&] {
            double total = 0;
            for (const auto t : queries)
                total += curve.discount(t).value;
            return total;
        });
        // Independent linear-scan baseline: recomputes node times/logs per query.
        measure(out, "discount_linear_scan_baseline_10000", samples, 10000, [&] {
            double total = 0;
            for (const auto t : queries) {
                const auto &nodes = curve.nodes();
                for (std::size_t j = 1; j < nodes.size(); ++j) {
                    const double right = (nodes[j].date - v) / 365.0;
                    if (t.value <= right) {
                        const double left = (nodes[j - 1].date - v) / 365.0;
                        const double w = (t.value - left) / (right - left);
                        total += std::exp((1 - w) * std::log(nodes[j - 1].discount.value) +
                                          w * std::log(nodes[j].discount.value));
                        break;
                    }
                }
            }
            return total;
        });
        measure(out, "calibration_8_quotes", samples, 8,
                [&] { return bootstrapper.build(v, quotes).curve.nodes().back().discount.value; });
        measure(out, "swap_pricing", samples, 1, [&] { return price(curve, trade).pv; });
        measure(out, "swap_pricing_prepared", samples, 1, [&] { return price(curve, prepared).pv; });
        measure(out, "parallel_quote_dv01", samples, 1,
                [&] { return risk.parallel_quote_dv01(v, quotes, trade); });
        measure(out, "quote_key_rates_8", samples, 8, [&] {
            double total = 0;
            for (const auto &b : risk.quote_risk(v, quotes, trade))
                total += b.dv01;
            return total;
        });
        measure(out, "zero_node_risk_8", samples, 8, [&] {
            double total = 0;
            for (const auto &b : risk.zero_risk(curve, trade))
                total += b.dv01;
            return total;
        });
        measure(out, "portfolio_parallel_dv01_20", samples, 20, [&] {
            double total = 0;
            for (const auto &t : portfolio) {
                total += risk.parallel_quote_dv01(v, quotes, t);
            }
            return total;
        });
        measure(out, "portfolio_parallel_dv01_shared_20", samples, 20, [&] {
            const auto values = risk.portfolio_parallel_dv01(v, quotes, portfolio);
            return std::accumulate(values.begin(), values.end(), 0.0);
        });
        if (file.is_open())
            file.close();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
