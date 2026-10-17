#include "rates/risk_engine.hpp"
#include <algorithm>
#include <utility>

namespace rates {
namespace {
void check_bump(BasisPoints bump) {
    if (!std::isfinite(bump.value) || bump.value <= 0)
        throw std::invalid_argument("Risk bump must be finite and positive in basis points");
}
double shocked_pv(const CurveBootstrapper &b, Date v, const std::vector<MarketQuote> &quotes,
                  const Swap &trade, double shift, std::size_t bucket) {
    auto shocked = quotes;
    for (std::size_t i = 0; i < shocked.size(); ++i)
        if (bucket == quotes.size() || bucket == i)
            shocked[i].rate = SimpleRate{shocked[i].rate.value + shift};
    try {
        return price(b.build(v, std::move(shocked)).curve, trade).pv;
    } catch (const std::exception &e) {
        throw std::runtime_error("Risk shock failed (bucket " +
                                 (bucket == quotes.size() ? std::string{"parallel"} : quotes[bucket].label) +
                                 ", decimal shift " + std::to_string(shift) + "): " + e.what());
    }
}
} // namespace
RiskEngine::RiskEngine(CurveBootstrapper b) : bootstrapper_(std::move(b)) {}
double RiskEngine::parallel_quote_dv01(Date v, const std::vector<MarketQuote> &q, const Swap &t,
                                       BasisPoints bump) const {
    check_bump(bump);
    const double h = bump.value * 1e-4;
    return (shocked_pv(bootstrapper_, v, q, t, -h, q.size()) -
            shocked_pv(bootstrapper_, v, q, t, h, q.size())) /
           (2 * bump.value);
}
std::vector<RiskBucket> RiskEngine::quote_risk(Date v, const std::vector<MarketQuote> &q, const Swap &t,
                                               BasisPoints bump) const {
    check_bump(bump);
    if (q.empty())
        throw std::invalid_argument("Quote risk requires a nonempty calibration quote set");
    std::vector<RiskBucket> result;
    for (std::size_t i = 0; i < q.size(); ++i) {
        const double risk = (shocked_pv(bootstrapper_, v, q, t, -bump.value * 1e-4, i) -
                             shocked_pv(bootstrapper_, v, q, t, bump.value * 1e-4, i)) /
                            (2 * bump.value);
        result.push_back({q[i].label,
                          bootstrapper_.conventions().calendar.adjust(
                              q[i].maturity, bootstrapper_.conventions().business_day),
                          risk});
    }
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b) { return a.maturity < b.maturity; });
    return result;
}
std::vector<RiskBucket> RiskEngine::zero_risk(const DiscountCurve &curve, const Swap &trade,
                                              BasisPoints bump) const {
    check_bump(bump);
    std::vector<RiskBucket> result;
    for (std::size_t i = 0; i + 1 < curve.nodes().size(); ++i) {
        const double down = price(curve.shift_zero_node(i, BasisPoints{-bump.value}), trade).pv;
        const double up = price(curve.shift_zero_node(i, bump), trade).pv;
        const auto date = curve.nodes()[i + 1].date;
        result.push_back({date.str(), date, (down - up) / (2 * bump.value)});
    }
    return result;
}
RiskReport RiskEngine::report(Date v, const std::vector<MarketQuote> &quotes, const Swap &trade,
                              std::string id, BasisPoints bump) const {
    const auto base = bootstrapper_.build(v, quotes);
    return {v,
            std::move(id),
            trade,
            price(base.curve, trade),
            parallel_quote_dv01(v, quotes, trade, bump),
            quote_risk(v, quotes, trade, bump),
            zero_risk(base.curve, trade, bump),
            bump.value,
            bootstrapper_.settings().quote_tolerance};
}
} // namespace rates
