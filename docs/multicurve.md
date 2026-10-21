# SOFR/OIS and separate discount and forwarding curves

Version 2.1 adds deterministic C++/Python APIs through `rates/multicurve.hpp`
and `pyrates`. Existing single-curve calls retain their behavior; existing CSV
CLIs remain single-curve tools.

## Overnight accrual and OIS

For business-day observations `r_i`, each held for `n_i` calendar days, realized
accrual is `product(1 + r_i * n_i / 360) - 1`. `compound_sofr` returns this times
`360 / total_days`, following the ACT/360 compounding basis described by the
[New York Fed](https://www.newyorkfed.org/markets/reference-rates/additional-information-about-reference-rates).
It requires business-day endpoints and a complete ordered sequence on
`[start,end)`. Missing, repeated, extra or non-finite observations and
nonpositive daily factors fail explicitly. Weekends and supplied holidays
accrue at the preceding business day's rate. Negative rates are supported.

`sofr_ois_conventions(calendar)` selects annual fixed/floating ACT/360 legs,
modified following and calendar month-end preservation. `OisCurveBootstrapper`
accepts deposits and `Instrument::Swap` quotes, interpreting swaps as compounded
OIS. For entirely future coupons with zero payment lag, projected overnight
compounding telescopes to `P(start)/P(end) - 1`. `price_ois` uses this identity
and rejects incompatible leg conventions and already-started trades.

The realized compounding utility does **not** add mixed realized/forecast
coupons to swap valuation. There is no observation shift, lookback, lockout,
payment/settlement lag, automatic US holiday database, futures convexity
adjustment or live SOFR feed. The default calendar has weekends only; supply
holidays explicitly. Examples are synthetic, not executable market quotes.

## Two curves and quote risk

For projection factors `F` and discount factors `D`, a floating coupon has PV
`N * (F(start)/F(end) - 1) * D(payment)`. Fixed PV is
`N * coupon * sum(accrual * D(payment))`. The entire floating leg cannot use
the single-curve telescoping shortcut when `D` and `F` differ.

`CurveBootstrapper::build_forward(discount, quotes)` solves projection factors
from deposits and par swaps using an exogenous discount curve. Continuous-zero
inputs directly constrain projection factors. Both curves must share a
valuation date and cover all required dates; extrapolation is rejected.
Projection factors use the immutable `DiscountCurve` container but are not
collateral discount factors.

```cpp
#include "rates/multicurve.hpp"
using namespace rates;
// ois_quotes and forward_quotes are caller-supplied MarketQuote vectors.
const Date valuation{2025, 1, 2};
const auto overnight = OisCurveBootstrapper{}.build(valuation, ois_quotes);
const CurveBootstrapper forward_builder;
const auto forwarding = forward_builder.build_forward(overnight.curve, forward_quotes);
const Swap trade{valuation, valuation.add_months(60), 1e6, SimpleRate{0.035}};
const auto value = price(overnight.curve, forwarding.curve, trade);
const auto risk = multi_curve_risk(CurveBootstrapper{sofr_ois_conventions()},
    forward_builder, valuation, ois_quotes, forward_quotes, trade);
```

Discount quote shocks rebuild discounting and **recalibrate forwarding to
unchanged forward market quotes**. Forwarding quote shocks hold discounting
fixed. Both use `(PV_down - PV_up)/(2*bump_bps)`. These are market-quote
sensitivities, not frozen-curve zero-node shifts.

## Reproduce the independent validation

```sh
python python/validate_multicurve.py --module-dir build/release --out results/multicurve --require-quantlib
```

[Three synthetic cases](../examples/multicurve_cases.json) cover upward,
negative and holiday-adjusted curves. QuantLib `OISRateHelper` and
`SwapRateHelper` bootstrap **from quotes only**; `OvernightIndexedSwap` and
`VanillaSwap` independently price them. Every quote shock is repeated.
No C++ factors are inputs to the external bootstrap. See the upstream
[OIS helper interface](https://github.com/lballabio/QuantLib/blob/master/ql/termstructures/yield/oisratehelper.hpp).
Calendars are deliberately matched; the custom overnight index uses SOFR's
compounding basis without claiming complete market-convention support.

All **230 comparisons pass** with QuantLib 1.44. Tolerances: `5e-10` for factors,
`2e-5` currency for PV, `2e-10` for par rates, `1e-12` for C++ residuals and
`0.002` currency/bp for risk. [Report](results/multicurve/report.json) and
[rows](results/multicurve/comparisons.csv) retain exact errors. Missing QuantLib
is reported as skipped unless required, in which case validation fails.
CI explicitly requires it.

The upward example values the 1m, 5Y, 3.5% payer at **28,509.36**, with a 4.12%
par rate, **+7.53** discount quote DV01 and **−459.83** forwarding quote DV01.
Using the discount curve in both roles gives **−16,545.34**. The projection
assumption materially changes valuation; this is not a trading-return claim.
