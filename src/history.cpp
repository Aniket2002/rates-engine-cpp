#include "rates/history.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace rates {
namespace {
struct Row {
    Timestamp time;
    Date valuation;
    MarketQuote quote;
};
std::string clean(std::string line) {
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return line;
}
Row parse_row(const std::string &text) {
    std::vector<std::string> fields;
    const auto line = clean(text);
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ','))
        fields.push_back(field);
    if (!line.empty() && line.back() == ',')
        fields.emplace_back();
    if (fields.size() != 6 || text.find('"') != std::string::npos)
        throw MarketDataError(MarketDataFailure::Invalid, "History row requires six unquoted CSV fields");
    for (const auto &cell : fields)
        if (cell.empty())
            throw MarketDataError(MarketDataFailure::Missing, "History contains an empty field");
    try {
        const auto time = Timestamp::parse(fields[0]);
        const auto valuation = Date::parse(fields[1]);
        if (valuation != time.date())
            throw std::invalid_argument("Snapshot valuation must equal its UTC observation date");
        if (fields[2] != "deposit" && fields[2] != "swap")
            throw std::invalid_argument("Unknown history instrument");
        std::size_t consumed = 0;
        const double rate = std::stod(fields[5], &consumed);
        if (consumed != fields[5].size())
            throw std::invalid_argument("Invalid history rate");
        return {time,
                valuation,
                {fields[2] == "deposit" ? Instrument::Deposit : Instrument::Swap, fields[3],
                 Date::parse(fields[4]), SimpleRate{rate}}};
    } catch (const std::exception &error) {
        throw MarketDataError(MarketDataFailure::Invalid,
                              std::string{"Invalid history row: "} + error.what());
    }
}
} // namespace
Timestamp Timestamp::parse(const std::string &text) {
    if (text.size() != 20 || text[10] != 'T' || text[13] != ':' || text[16] != ':' || text[19] != 'Z')
        throw std::invalid_argument("Expected UTC timestamp YYYY-MM-DDTHH:MM:SSZ");
    const Date day = Date::parse(text.substr(0, 10));
    for (const std::size_t i : {11U, 12U, 14U, 15U, 17U, 18U})
        if (text[i] < '0' || text[i] > '9')
            throw std::invalid_argument("Invalid timestamp digit");
    const int hours = std::stoi(text.substr(11, 2)), minutes = std::stoi(text.substr(14, 2)),
              seconds = std::stoi(text.substr(17, 2));
    if (hours > 23 || minutes > 59 || seconds > 59)
        throw std::invalid_argument("Invalid timestamp clock time");
    return Timestamp{static_cast<std::int64_t>(day - Date{1970, 1, 1}) * 86400 +
                     static_cast<std::int64_t>(hours) * 3600 + static_cast<std::int64_t>(minutes) * 60 +
                     seconds};
}
Date Timestamp::date() const {
    const auto days = seconds_ >= 0 ? seconds_ / 86400 : (seconds_ - 86399) / 86400;
    return Date{1970, 1, 1}.add_days(static_cast<int>(days));
}
std::string Timestamp::str() const {
    const auto clock = (seconds_ % 86400 + 86400) % 86400;
    std::ostringstream out;
    out << date().str() << 'T' << std::setfill('0') << std::setw(2) << clock / 3600 << ':' << std::setw(2)
        << clock % 3600 / 60 << ':' << std::setw(2) << clock % 60 << 'Z';
    return out.str();
}
Timestamp Timestamp::add_seconds(std::int64_t seconds) const {
    if (seconds < -10000000000LL || seconds > 10000000000LL)
        throw std::out_of_range("Timestamp increment outside supported range");
    const auto changed = Timestamp{seconds_ + seconds};
    (void)changed.date();
    return changed;
}
MarketDataError::MarketDataError(MarketDataFailure code, const std::string &message,
                                 std::optional<Timestamp> observed, std::optional<std::int64_t> age)
    : std::runtime_error(message), code_(code), observed_(observed), age_(age) {}
HistoricalQuoteStore::HistoricalQuoteStore(std::filesystem::path path) : path_(std::move(path)) {
    std::ifstream input(path_, std::ios::binary);
    if (!input)
        throw MarketDataError(MarketDataFailure::Missing, "History file is missing or unreadable");
    file_size_ = std::filesystem::file_size(path_);
    modified_ = std::filesystem::last_write_time(path_);
    std::string line;
    if (!std::getline(input, line) || clean(line) != "timestamp,valuation_date,type,label,maturity,rate")
        throw MarketDataError(MarketDataFailure::Invalid, "Invalid history CSV header");
    std::set<std::string> expected, labels;
    const auto complete = [&] {
        if (expected.empty())
            expected = labels;
        else if (labels != expected)
            throw MarketDataError(MarketDataFailure::Missing,
                                  "History snapshot has missing or changed quote labels");
    };
    while (true) {
        const auto offset = input.tellg();
        if (!std::getline(input, line))
            break;
        const auto row = parse_row(line);
        if (index_.empty() || row.time != index_.back().observed) {
            if (!index_.empty()) {
                if (row.time < index_.back().observed)
                    throw MarketDataError(MarketDataFailure::Invalid,
                                          "History snapshots must be chronological");
                complete();
                labels.clear();
            }
            index_.push_back({row.time, row.valuation, offset, 0});
        }
        if (!labels.insert(row.quote.label).second)
            throw MarketDataError(MarketDataFailure::Invalid,
                                  "Duplicate quote label within history snapshot");
        ++index_.back().rows;
    }
    if (input.bad())
        throw MarketDataError(MarketDataFailure::Invalid, "History file read failed");
    if (index_.empty())
        throw MarketDataError(MarketDataFailure::Missing, "History has no quote snapshots");
    complete();
    verify_file();
}
void HistoricalQuoteStore::verify_file() const {
    if (!std::filesystem::exists(path_) || std::filesystem::file_size(path_) != file_size_ ||
        std::filesystem::last_write_time(path_) != modified_)
        throw MarketDataError(MarketDataFailure::Invalid,
                              "History file changed after indexing; reopen store");
}
QuoteSnapshot HistoricalQuoteStore::as_of(Timestamp query, std::int64_t max_age_seconds) const {
    if (max_age_seconds < 0)
        throw std::invalid_argument("Maximum data age must be nonnegative seconds");
    verify_file();
    auto found = std::upper_bound(index_.begin(), index_.end(), query,
                                  [](Timestamp time, const Index &entry) { return time < entry.observed; });
    if (found == index_.begin())
        throw MarketDataError(MarketDataFailure::Missing,
                              "No observation available at or before requested as-of time");
    --found;
    const auto age = query - found->observed;
    if (age > max_age_seconds)
        throw MarketDataError(MarketDataFailure::Stale, "Latest available snapshot is stale", found->observed,
                              age);
    std::ifstream input(path_, std::ios::binary);
    if (!input)
        throw MarketDataError(MarketDataFailure::Missing, "Cannot reopen indexed history file");
    input.seekg(found->offset);
    QuoteSnapshot snapshot{found->observed, found->valuation, {}, age};
    snapshot.quotes.reserve(found->rows);
    std::string line;
    for (std::size_t i = 0; i < found->rows; ++i) {
        if (!std::getline(input, line))
            throw MarketDataError(MarketDataFailure::Missing, "Truncated indexed snapshot");
        auto row = parse_row(line);
        if (row.time != found->observed || row.valuation != found->valuation)
            throw MarketDataError(MarketDataFailure::Invalid,
                                  "Indexed snapshot changed; refusing look-ahead");
        snapshot.quotes.push_back(std::move(row.quote));
    }
    verify_file();
    return snapshot;
}
} // namespace rates
