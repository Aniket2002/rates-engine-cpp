#pragma once
#include "rates/portfolio.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace rates::batch_io {
inline std::vector<std::string> split(std::string line, std::size_t columns) {
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line.find('"') != std::string::npos)
        throw std::invalid_argument("Input CSV does not support quoted fields");
    std::stringstream input(line);
    std::string field;
    std::vector<std::string> fields;
    while (std::getline(input, field, ','))
        fields.push_back(field);
    if (!line.empty() && line.back() == ',')
        fields.emplace_back();
    if (fields.size() != columns)
        throw std::invalid_argument("Unexpected CSV column count");
    for (const auto &value : fields)
        if (value.empty())
            throw std::invalid_argument("Empty CSV field");
    return fields;
}
inline double number(const std::string &text) {
    std::size_t used = 0;
    const double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value))
        throw std::invalid_argument("Expected a finite decimal number");
    return value;
}
inline std::vector<PortfolioTrade> read_portfolio(const std::string &path) {
    std::ifstream input(path);
    if (!input)
        throw std::invalid_argument("Portfolio file is missing or unreadable");
    std::string line;
    if (!std::getline(input, line) ||
        split(line, 6) !=
            std::vector<std::string>{"id", "effective", "maturity", "notional", "fixed_rate", "side"})
        throw std::invalid_argument("Invalid portfolio CSV header");
    std::vector<PortfolioTrade> trades;
    while (std::getline(input, line)) {
        const auto row = split(line, 6);
        if (row[5] != "payer" && row[5] != "receiver")
            throw std::invalid_argument("Unknown portfolio side");
        trades.push_back({row[0],
                          {Date::parse(row[1]), Date::parse(row[2]), number(row[3]),
                           SimpleRate{number(row[4])}, row[5] == "payer" ? Side::Payer : Side::Receiver}});
    }
    if (input.bad())
        throw std::runtime_error("Portfolio read failed");
    return trades;
}
inline std::string json_string(const std::string &text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\')
            out << '\\' << ch;
        else if (ch < 32)
            out << "\\u" << std::hex << std::setfill('0') << std::setw(4) << static_cast<unsigned>(ch);
        else
            out << ch;
    }
    out << '"';
    return out.str();
}
inline std::string csv_string(const std::string &text) {
    std::string result = "\"";
    for (const char ch : text) {
        if (ch == '"')
            result += '"';
        result += ch;
    }
    return result + '"';
}
} // namespace rates::batch_io
