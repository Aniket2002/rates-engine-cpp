#include "rates/risk_engine.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

using namespace rates;
namespace {
using Rows = std::vector<std::vector<std::string>>;
Rows read_csv(const std::string &path, const std::string &header, std::size_t columns) {
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("Cannot open input: " + path);
    std::string line;
    if (!std::getline(input, line))
        throw std::invalid_argument("Empty CSV: " + path);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line != header)
        throw std::invalid_argument("Unexpected CSV header in " + path + "; expected " + header);
    Rows rows;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (line.find('"') != std::string::npos)
            throw std::invalid_argument("Quoted CSV fields are unsupported");
        std::vector<std::string> row;
        std::stringstream stream(line);
        std::string field;
        while (std::getline(stream, field, ','))
            row.push_back(field);
        if (row.size() != columns)
            throw std::invalid_argument("Wrong CSV field count: " + line);
        for (const auto &cell : row)
            if (cell.empty())
                throw std::invalid_argument("Empty CSV field");
        rows.push_back(std::move(row));
    }
    if (input.bad())
        throw std::runtime_error("CSV read failure: " + path);
    return rows;
}
double number(const std::string &text) {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Invalid finite number: " + text);
    return value;
}
int interval(const std::string &text) {
    const double n = number(text);
    if (n < 1 || n > 120 || std::floor(n) != n)
        throw std::invalid_argument("Payment interval must be an integer in 1..120");
    return static_cast<int>(n);
}
DayCount day_count(const std::string &text) {
    if (text == "ACT360")
        return DayCount::Actual360;
    if (text == "ACT365F")
        return DayCount::Actual365Fixed;
    if (text == "30E360")
        return DayCount::ThirtyE360;
    throw std::invalid_argument("Unknown day count: " + text);
}
std::ofstream output(const std::filesystem::path &directory, const std::string &name) {
    std::ofstream out(directory / name);
    out.exceptions(std::ios::badbit | std::ios::failbit);
    out << std::setprecision(17);
    return out;
}
void write_flows(std::ofstream &out, const char *leg, const std::vector<CashFlow> &flows) {
    for (const auto &cf : flows)
        out << leg << ',' << cf.period.start.str() << ',' << cf.period.end.str() << ','
            << cf.period.payment.str() << ',' << cf.period.accrual.value << ',' << cf.decimal_rate << ','
            << cf.amount << ',' << cf.discount << ',' << cf.present_value << '\n';
}
} // namespace
int main(int argc, char **argv) {
    try {
        std::map<std::string, std::string> args{{"--valuation", "2025-01-02"},
                                                {"--quotes", "examples/synthetic_quotes.csv"},
                                                {"--swap", "examples/sample_swap.csv"},
                                                {"--out", "results/demo"},
                                                {"--curve-id", "synthetic-single-curve"},
                                                {"--fixed-months", "6"},
                                                {"--floating-months", "3"},
                                                {"--fixed-dc", "30E360"},
                                                {"--floating-dc", "ACT360"},
                                                {"--deposit-dc", "ACT360"},
                                                {"--adjustment", "modified-following"},
                                                {"--eom", "true"},
                                                {"--holidays", ""},
                                                {"--bump-bps", "1"}};
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help") {
                std::cout << "rates_cli [--valuation YYYY-MM-DD] [--quotes file.csv] [--swap file.csv] "
                             "[--out directory]\n"
                          << "  --curve-id text --fixed-months N --floating-months N\n"
                          << "  --fixed-dc ACT360|ACT365F|30E360 --floating-dc ... --deposit-dc ...\n"
                          << "  --adjustment unadjusted|following|modified-following --eom true|false\n"
                          << "  --holidays YYYY-MM-DD,YYYY-MM-DD --bump-bps positive-number\n"
                          << "Rates are decimals; notionals and PVs use unspecified currency units. "
                             "Quote types: deposit, swap, continuous_zero (ACT/365F exact-date pillars).\n";
                return 0;
            }
            if (!args.contains(key) || i + 1 >= argc)
                throw std::invalid_argument("Unknown or incomplete argument: " + key);
            args[key] = argv[++i];
        }
        Conventions conventions;
        conventions.fixed_months = interval(args.at("--fixed-months"));
        conventions.floating_months = interval(args.at("--floating-months"));
        conventions.fixed_day_count = day_count(args.at("--fixed-dc"));
        conventions.floating_day_count = day_count(args.at("--floating-dc"));
        conventions.deposit_day_count = day_count(args.at("--deposit-dc"));
        const auto &adjustment = args.at("--adjustment");
        if (adjustment == "unadjusted")
            conventions.business_day = BusinessDay::Unadjusted;
        else if (adjustment == "following")
            conventions.business_day = BusinessDay::Following;
        else if (adjustment != "modified-following")
            throw std::invalid_argument("Unknown business-day adjustment");
        if (args.at("--eom") != "true" && args.at("--eom") != "false")
            throw std::invalid_argument("--eom expects true or false");
        conventions.end_of_month = args.at("--eom") == "true";
        std::set<Date> holidays;
        std::stringstream holiday_text(args.at("--holidays"));
        std::string holiday;
        while (std::getline(holiday_text, holiday, ','))
            holidays.insert(Date::parse(holiday));
        conventions.calendar = Calendar{std::move(holidays)};
        conventions.validate();
        const Date valuation = Date::parse(args.at("--valuation"));
        std::vector<MarketQuote> quotes;
        for (const auto &row : read_csv(args.at("--quotes"), "type,label,maturity,rate", 4)) {
            if (row[0] != "deposit" && row[0] != "swap" && row[0] != "continuous_zero")
                throw std::invalid_argument("Unknown quote instrument: " + row[0]);
            quotes.push_back({row[0] == "deposit"
                                  ? Instrument::Deposit
                                  : (row[0] == "swap" ? Instrument::Swap : Instrument::ContinuousZero),
                              row[1], Date::parse(row[2]), SimpleRate{number(row[3])}});
        }
        const auto trades = read_csv(args.at("--swap"), "effective,maturity,notional,fixed_rate,side", 5);
        if (trades.size() != 1)
            throw std::invalid_argument("Swap CSV must contain exactly one trade");
        const auto &t = trades.front();
        if (t[4] != "payer" && t[4] != "receiver")
            throw std::invalid_argument("Swap side must be payer or receiver");
        const Swap trade{Date::parse(t[0]),
                         Date::parse(t[1]),
                         number(t[2]),
                         SimpleRate{number(t[3])},
                         t[4] == "payer" ? Side::Payer : Side::Receiver,
                         conventions};
        const CurveBootstrapper bootstrapper{conventions};
        const auto calibration = bootstrapper.build(valuation, quotes);
        const auto risk = RiskEngine{bootstrapper}.report(valuation, quotes, trade, args.at("--curve-id"),
                                                          BasisPoints{number(args.at("--bump-bps"))});
        if (risk.curve_id.find_first_of(",\r\n\"") != std::string::npos)
            throw std::invalid_argument("Curve id cannot contain CSV delimiters");
        const std::filesystem::path directory(args.at("--out"));
        std::filesystem::create_directories(directory);
        auto nodes = output(directory, "curve.csv");
        nodes << "date,time,discount,zero,segment_forward\n";
        nodes << valuation.str() << ",0,1,,\n";
        std::cout << std::setprecision(12) << "Single-curve calibration (rates in decimals)\n";
        for (const auto &d : calibration.curve.diagnostics()) {
            nodes << d.date.str() << ',' << d.time << ',' << d.discount << ',' << d.zero << ','
                  << d.segment_forward << '\n';
            std::cout << d.date.str() << " DF=" << d.discount << " zero=" << d.zero << '\n';
        }
        auto residuals = output(directory, "calibration.csv");
        residuals << "instrument,label,maturity,market_quote,model_quote,residual,iterations,status\n";
        for (const auto &row : calibration.diagnostics) {
            residuals << (row.quote.instrument == Instrument::Deposit
                              ? "deposit"
                              : (row.quote.instrument == Instrument::Swap ? "swap" : "continuous_zero"))
                      << ',' << row.quote.label << ',' << row.pillar.str() << ',' << row.quote.rate.value
                      << ',' << row.model_quote << ',' << row.residual << ',' << row.iterations
                      << ",converged\n";
            std::cout << row.quote.label << " residual=" << row.residual << " iterations=" << row.iterations
                      << '\n';
        }
        auto cashflows = output(directory, "cashflows.csv");
        cashflows << "leg,start,end,payment,accrual,rate,amount,discount,pv\n";
        write_flows(cashflows, "fixed", risk.base.fixed_flows);
        write_flows(cashflows, "floating", risk.base.floating_flows);
        auto summary = output(directory, "summary.csv");
        summary << "metric,value\nvaluation_date," << valuation.str() << "\ncurve_id," << risk.curve_id
                << "\neffective," << trade.effective.str() << "\nmaturity," << trade.maturity.str()
                << "\nside," << t[4] << "\nnotional," << trade.notional << "\nfixed_coupon,"
                << trade.fixed_rate.value << "\nfixed_pv," << risk.base.fixed_pv << "\nfloating_pv,"
                << risk.base.floating_pv << "\npv," << risk.base.pv << "\npar_rate," << risk.base.par_rate
                << "\nannuity," << risk.base.annuity << "\nparallel_quote_dv01," << risk.parallel_quote_dv01
                << "\nbump_bps," << risk.bump_bps << "\ncalibration_tolerance," << risk.calibration_tolerance
                << "\nshock_definition,(PV_down-PV_up)/(2*bump_bps)\n";
        auto buckets = output(directory, "risk.csv");
        buckets << "coordinate,label,maturity,dv01\n";
        for (const auto &bucket : risk.quote_buckets) {
            buckets << "market_quote," << bucket.label << ',' << bucket.maturity.str() << ',' << bucket.dv01
                    << '\n';
            std::cout << "Quote " << bucket.label << " DV01=" << bucket.dv01 << '\n';
        }
        for (const auto &bucket : risk.zero_nodes) {
            buckets << "zero_node," << bucket.label << ',' << bucket.maturity.str() << ',' << bucket.dv01
                    << '\n';
            std::cout << "Zero node " << bucket.label << " DV01=" << bucket.dv01 << '\n';
        }
        std::cout << "Payer/receiver PV=" << risk.base.pv << " fixed=" << risk.base.fixed_pv
                  << " floating=" << risk.base.floating_pv << " par=" << risk.base.par_rate
                  << " parallel quote DV01=" << risk.parallel_quote_dv01
                  << "\nResults: " << directory.string() << '\n';
        nodes.close();
        residuals.close();
        cashflows.close();
        summary.close();
        buckets.close();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "rates_cli: " << error.what() << '\n';
        return 1;
    }
}
