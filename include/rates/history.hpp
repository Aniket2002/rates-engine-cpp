#pragma once
#include "rates/swap.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>

namespace rates {
// UTC only, second precision. Input must be YYYY-MM-DDTHH:MM:SSZ (no leap seconds).
class Timestamp {
  public:
    static Timestamp parse(const std::string &iso);
    std::string str() const;
    Date date() const;
    Timestamp add_seconds(std::int64_t seconds) const;
    std::int64_t operator-(Timestamp other) const { return seconds_ - other.seconds_; }
    auto operator<=>(const Timestamp &) const = default;

  private:
    explicit Timestamp(std::int64_t seconds) : seconds_(seconds) {}
    std::int64_t seconds_;
};
enum class MarketDataFailure : unsigned char { Missing, Stale, Invalid };
class MarketDataError : public std::runtime_error {
  public:
    MarketDataError(MarketDataFailure code, const std::string &message,
                    std::optional<Timestamp> observed = {}, std::optional<std::int64_t> age = {});
    MarketDataFailure code() const { return code_; }
    const std::optional<Timestamp> &observed() const { return observed_; }
    const std::optional<std::int64_t> &age_seconds() const { return age_; }

  private:
    MarketDataFailure code_;
    std::optional<Timestamp> observed_;
    std::optional<std::int64_t> age_;
};
struct QuoteSnapshot {
    Timestamp observed;
    Date valuation;
    std::vector<MarketQuote> quotes;
    std::int64_t age_seconds;
};
// File-backed full snapshots, indexed once by timestamp/byte offset. Input rows
// must be chronological and have the same quote-label universe at each snapshot.
// Timestamp is the producer's replay time. Operational inputs use availability
// times; retrospective date labels must be identified as such by the caller.
class HistoricalQuoteStore {
  public:
    explicit HistoricalQuoteStore(std::filesystem::path path);
    QuoteSnapshot as_of(Timestamp query, std::int64_t max_age_seconds) const;
    std::size_t snapshot_count() const { return index_.size(); }

  private:
    struct Index {
        Timestamp observed;
        Date valuation;
        std::streamoff offset;
        std::size_t rows;
    };
    std::filesystem::path path_;
    std::vector<Index> index_;
    std::uintmax_t file_size_;
    std::filesystem::file_time_type modified_;
    void verify_file() const;
};
} // namespace rates
