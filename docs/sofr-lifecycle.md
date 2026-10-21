# SOFR lifecycle, conventions and market data

Version 2.2 adds `rates/sofr.hpp`, `rates/sofr_curve.hpp`, Python bindings and a
public-data pipeline. Legacy `price_ois`, vanilla pricing and CSV replay keep
their earlier behavior. This extension covers the pricing lifecycle of constant
notional USD SOFR swaps; it is not a clearing or trade-event processing system.

## Pricing contract

`SofrSwap` supplies effective/maturity dates, positive constant notional, fixed
coupon, payer/receiver side and `SofrConventions`. `price_sofr` accepts separate
discount and projection curves, a `FixingStore`, and an explicit UTC as-of time.
Both curves must be anchored on its date. Curves reject extrapolation.

| Setting | Definition/default |
| --- | --- |
| Payment interval | Annual; configurable positive month interval |
| Fixed accrual | ACT/360; C++ also accepts existing day counts |
| Overnight accrual | Compounded daily ACT/360 |
| Date adjustment | Modified following, forward schedule, short final stub |
| Spot settlement | Two index business days for `spot_sofr_swap` and quote calibration |
| Payment lag | Two payment-calendar business days after adjusted accrual end |
| Lookback | Zero by default; shifts each fixing date back by index business days |
| Observation shift | Optional; also shifts the overnight day weights |
| Lockout | Optional; final k observations reuse the fixing before the lockout |
| End-of-month | C++ defaults true; quote-import examples explicitly use false |
| Payment on as-of date | Excluded by default; pricing API can include it explicitly |

Index and payment calendars can differ in C++; the import/replay schema uses the
same hashed US Government Bond calendar for both. Its conventions require
ACT/360, modified following and explicit values for every supported field.
Default C++ calendars only exclude weekends: supply appropriate holidays.

For observation weights `w_i` and rates `r_i`, the overnight growth is
`G = product(1 + r_i*w_i) - 1`. The coupon amount is
`N * G * contractual_ACT360 / sum(w_i)`. Observation shifts can change total
observation-period length, so the normalization matters. Lockout changes rates,
not day weights. The implementation uses `log1p`/`expm1` and rejects nonpositive
growth factors and nonfinite values. This normalization agrees with the
[QuantLib overnight coupon pricer](https://github.com/lballabio/QuantLib/blob/master/ql/cashflows/overnightindexedcouponpricer.cpp).

`FixingStore` selects the latest revision available at or before the as-of time.
Past observations require fixings; future observations use projection forwards.
A missing past fixing fails even if its rate is expected to be published later
that morning. There is no silent estimate for unpublished history.

Accrued value is a defined reporting convention: compound elapsed observations
within each unpaid coupon, prorate an observation's weight inside its original
interest interval, and retain the coupon's full-period normalization. Subtract
fixed accrued and apply the trade side. Clean PV is dirty PV minus this signed,
undiscounted accrued value. This shifted partial-accrual convention is checked
independently and is not claimed to equal every vendor's accrued calculation.

Matured coupons remain payable until the lagged payment date. Fully settled
trades return zero PV and no par rate without requiring historical fixings.
`SofrCurveBootstrapper` calibrates spot-starting OIS quotes with final-payment
pillars and verifies all residuals. `build_forward` uses exogenous discounting.
`sofr_quote_risk` rebuilds the single overnight curve for quote shocks;
`sofr_curve_risk` shocks discount/projection zero nodes separately while freezing
the other curve and realized fixings. Risk is `(PV_down-PV_up)/(2*bump_bps)`.

## Reproduce the captured example

Build the optional bindings as described in the README, then run:

```sh
python python/run_sofr_valuation.py --module-dir build/release --manifest data/sofr/manifest.json --out results/sofr-demo
```

The manifest references immutable fixing and quote snapshots, a calendar and a
trade. Outputs contain input hashes, calibration residuals, cash flows, every
daily observation, accrued/clean/dirty PV and risk. The complete report is
published atomically under its content hash; CSVs are disposable views of it.
An optional `projection_quotes` snapshot calibrates a separate projection curve
and reports independent curve-node risks. Those are not basis-swap calibrations
or recalibrated multi-curve quote sensitivities.

At the committed as-of time, the 1m notional, 3.75% fixed payer runs from
1 October 2026 to 1 October 2028, with quarterly coupons, two-day lookback,
observation shift and two-day payment lag. It has 15 realized observations and
484 forecasts. Dirty PV is **−4,448.43**, accrued **144.94**, and clean PV
**−4,593.36**. Fixings are actual NY Fed data; all example curve quotes are
synthetic. These values are not executable market marks or trading returns.

## Capture current public fixings

The adapter uses the [official API](https://markets.newyorkfed.org/static/docs/markets-api.html).
The [New York Fed reference-rate methodology](https://www.newyorkfed.org/markets/reference-rates/additional-information-about-reference-rates)
describes publication on the following business day and possible revisions.
The historical API does not provide exact publication timestamps, so this
adapter records **first local capture** as availability.

```sh
python python/sofr_market_data.py calendar --start 2018-01-01 --end 2040-12-31 --out results/calendar.json
python python/sofr_market_data.py fixings --calendar results/calendar.json --start 2026-09-01 --end 2026-10-20 --out results/fixings
```

Choose an end date whose observation has already been published. Pass
`--previous results/fixings/<hash>.json` on subsequent captures to preserve
unchanged first-capture times and append detected revisions. Retain the prior
snapshot files; the successor records its parent's digest. Concurrent captures
can form separate branches; there is no mutable "latest" pointer or automatic
branch merge. The caller explicitly selects the approved snapshot.

HTTPS requests have a 15-second timeout, three attempts for transient failures,
bounded backoff and an 8 MiB limit. Parsing rejects duplicate JSON keys,
nonfinite rates, unexpected date/type rows and missing business dates. Rates
convert from NY Fed percentage units to decimals. Capture rejects dates outside
the configured calendar or at/after the current capture date. Raw bytes and
normalized records are retained with hashes and request provenance. A failed
capture leaves existing snapshots intact.

## Import provider quotes

Start from [the schema example](../data/sofr/example-quotes.json) and
[quality policy](../data/sofr/example-policy.json). Replace the synthetic rates
and provenance with your provider's observations; set `synthetic` and
`allow_synthetic` appropriately. Update timestamps from the source, not merely
to bypass freshness checks. The quote source must provide a coherent snapshot.

```sh
python python/sofr_market_data.py quotes --calendar results/calendar.json --input provider-quotes.json --policy quote-policy.json --out results/quotes
```

Each snapshot specifies USD/SOFR/OIS, decimal units, source, observation and
availability times, synthetic status, calendar hash and exact conventions.
Each tenor has a unique label, tenor in months, rate, bid and ask. The policy
defines the required tenor universe, maximum age/spread and allowed rate range.
Import rejects incomplete/duplicate tenors, crossed or wide markets, out-of-range
rates, future timestamps and stale data. Replay rechecks freshness and cannot
use a snapshot captured after its as-of time. Quotes must be from the same
valuation business date. Required tenor coverage must extend through all trade
payments and forecast observations; otherwise the curve rejects valuation.

The public fixing feed does not supply tradable OIS curves. Production use still
requires a quote provider, source entitlements/authentication, monitored capture,
calendar governance, contract conventions and operational reconciliation.
The [limitations](limitations.md) distinguish supported pricing behavior from
those outstanding responsibilities.
