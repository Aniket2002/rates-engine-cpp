#pragma once
#include "rates/schedule.hpp"
#include <string>

namespace rates {
enum class Side : unsigned char { Payer, Receiver };
struct Swap {
    Date effective;
    Date maturity;
    double notional;
    SimpleRate fixed_rate;
    Side side = Side::Payer;
    Conventions conventions{};
    void validate(Date valuation) const;
};
enum class Instrument : unsigned char { Deposit, Swap };
struct MarketQuote {
    Instrument instrument;
    std::string label;
    Date maturity; // Unadjusted contractual maturity; quote starts at valuation.
    SimpleRate rate;
};
} // namespace rates
