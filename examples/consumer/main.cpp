#include <cmath>
#include <iostream>
#include <rates/basis.hpp>
#include <rates/lifecycle.hpp>
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
    rates::SofrConventions overnight;
    overnight.payment_lag_days = 0;
    const rates::BasisSwap basis{valuation,          maturity,  1e6,      rates::SimpleRate{0},
                                 rates::Side::Payer, overnight, overnight};
    const auto asof = rates::Timestamp::parse("2025-01-02T16:00:00Z");
    const auto basis_pv =
        rates::price_basis(curve, curve, curve, basis, rates::FixingStore{}, rates::FixingStore{}, asof).pv;
    rates::CollateralAccount collateral{valuation};
    collateral.apply({"zero", asof, rates::CollateralEventKind::Mark, "", 0});
    collateral.apply({"close", asof, rates::CollateralEventKind::Close, "", 0});
    return std::abs(pv - 12000) < 1e-8 && std::abs(basis_pv) < 1e-8 && collateral.closed() ? 0 : 1;
}
