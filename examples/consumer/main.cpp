#include <cmath>
#include <iostream>
#include <rates/swap_pricer.hpp>

int main() {
    const rates::Date valuation{2025, 1, 2};
    const auto maturity = valuation.add_months(12);
    const rates::DiscountCurve curve{valuation, {{maturity, rates::DiscountFactor{0.95}}}};
    rates::Conventions conventions;
    conventions.fixed_months = 12;
    conventions.fixed_day_count = rates::DayCount::Actual365Fixed;
    const rates::Swap trade{valuation,          maturity,   1e6, rates::SimpleRate{0.04},
                            rates::Side::Payer, conventions};
    const auto pv = rates::price(curve, trade).pv;
    std::cout << "Installed library one-year swap PV: " << pv << '\n';
    return std::abs(pv - 12000) < 1e-8 ? 0 : 1;
}
