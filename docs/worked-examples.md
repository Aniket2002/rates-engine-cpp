# Worked examples and historical validation

These detailed examples were moved out of the README to keep its results table
focused on the current release. Each example retains its own inputs, valuation
date and validation scope. Comparison counts below belong to individual suites;
they are not the current release's CTest total. Historical coverage and build
counts are recorded in [coverage](coverage.md), [v2 validation](v2.md) and the
[v2.1 verification record](launch-validation.md).

Run commands from the repository root. Python workflows require the
[compiled bindings](reproducibility.md#bindings). The CSV CLI example uses one
curve; [OIS discounting and separate projection](multicurve.md) are available
through the C++ and Python APIs.

## Seasoned SOFR swap

At **21 October 2026, 18:17:09 UTC**, the captured example values a **USD 1 million
payer, 3.75% fixed**, running from 1 October 2026 to 1 October 2028. Quarterly
coupons use two-day lookback, observation shift and two-day payment lag.

| Output | Value |
| --- | ---: |
| Dirty PV | **−4,448.43 USD** |
| Signed accrued | 144.94 USD |
| Clean PV | **−4,593.36 USD** |
| Daily observations | **15 realized / 484 projected** |

**Actual captured NY Fed fixings + synthetic OIS quotes.** These are illustrative
valuations, not market-calibrated marks. Missing required history is rejected.

After building the [Python bindings](reproducibility.md#bindings):

```sh
python python/run_sofr_valuation.py --module-dir build/release --manifest data/sofr/manifest.json --out results/sofr-demo
python python/validate_sofr.py --module-dir build/release --out results/sofr-validation --require-quantlib
```

![SOFR cash flows and realized versus projected daily observations](charts/sofr-cashflows.png)

The [lifecycle guide](sofr-lifecycle.md) explains the cash flows, quote-risk
API and auditable inputs. [Expanded validation](sofr-validation.md) covers
30,524 independent comparisons, including unusual stubs, distinct calendars and
full two-curve quote recalibration.

## Extend to basis swaps and contractual lifecycle

The [basis and lifecycle extension](basis-clearing-collateral.md) adds
same-currency compounded overnight basis-swap calibration, three-curve quote risk,
clearing acceptance/payment events and a generic cash-collateral agreement.
Margin calls, actual transfers, interest and final returns have separate audit trails.

```sh
python python/run_basis_lifecycle.py --module-dir build/release --out results/basis-lifecycle
python python/validate_basis.py --module-dir build/release --out results/basis-validation --require-quantlib
```

This separate example uses **synthetic quotes and synthetic fixings**. It follows
a seasoned basis swap through both leg payments and zero collateral at closure.
The implementation models generic contractual rules; live CCP connectivity,
initial-margin models and settlement variation are outside its scope.

The original example below takes eight synthetic quotes through calibration,
cash flows, PV and two risk coordinates.

## Build and price one swap

Requires CMake 3.25+, Ninja, a C++20 compiler and Python 3.10+. GoogleTest 1.17.0
is fetched on the first test build. On Windows use an x64 Visual Studio Developer
shell or the [portable toolchain](reproducibility.md), and append `.exe` to
executable names.

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
./build/release/rates_cli --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/demo
```

At **2 January 2025**, the [inputs](../examples/synthetic_quotes.csv) contain
1M/3M/6M deposits and 1Y/2Y/3Y/5Y/10Y par swaps, in decimal rate units.
The [trade](../examples/sample_swap.csv) pays **3.7% fixed** on **1,000,000 notional**
until **2 January 2030**, receiving quarterly floating payments. Fixed payments
are semiannual with 30E/360 accrual; floating/deposit accrual is ACT/360.
The example uses weekend-only modified-following adjustment and one curve for
both projection and discounting.

| Output | Value |
| --- | ---: |
| Fixed-leg PV | 167,660.057351 |
| Floating-leg PV | 172,191.410251 |
| Payer PV | **4,531.352900** |
| Par fixed rate | **3.800000%** |
| Signed parallel quote DV01 | **-451.944300 currency/bp** |

The market par rate exceeds the contractual coupon, so the payer has a positive
PV. The engine reprices every input within a `1e-12` decimal-rate tolerance,
then interpolates log discount factors. [Recorded outputs](results/demo)
include the calibrated curve, residuals, cash flows, summary and risk.

![Calibrated continuous zero-yield curve](charts/curve.png)

## Read the risk correctly

Risk uses **`(PV_down - PV_up) / (2 * bump_bps)`**. A negative number for this
payer means PV rises when rates rise. A +1 bp move predicts about **+451.94**
of PV change before nonlinear effects.

Quote buckets bump one market quote and rebuild the curve. Zero-node buckets
bump one continuous node yield directly. They answer different questions:

![Market-quote and zero-node risk buckets](charts/risk-buckets.png)

The CLI writes `curve.csv`, `calibration.csv`, `cashflows.csv`, `summary.csv`
and `risk.csv`. Risk maturity identifies the **actual calibrated pillar**:
adjusted for deposit/swap contracts, exact for continuous-zero observations.
See [risk definitions](risk_methodology.md) and [conventions](conventions.md).

## Move the quotes and explain the PV change

```sh
python -m pip install QuantLib
python python/generate_case_study.py --cli build/release/rates_cli --out results/case-study --require-quantlib
```

A **+10 bp parallel shock** changes PV by **+4,507.58** after recalibration.
The first-order bucket estimate is **+4,519.44**, leaving a **-11.86** residual.
At +50 bp the residual grows to **-294.32**. All seven scenarios, including a
steepener, pass independent Python and QuantLib quote-helper checks. These are
same-date hypothetical PV changes, not realized trading P&L.

![Full repricing compared with linear risk estimates](charts/pv-change.png)

The [model-validation case study](model-validation-case-study.md) gives
hypotheses, tolerances and results. [Charts](charts) include PNG and SVG
exports, input hashes and a [reproduction script](../python/generate_charts.py).

## Validate and extend the example

```sh
python -m pip install QuantLib pybind11
python python/validate_results.py --cli build/release/rates_cli --output results/validation --require-quantlib
python python/validate_conventions.py --cli build/release/rates_cli --output results/conventions --require-quantlib
cmake --preset release -DRATES_BUILD_PYTHON=ON -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build --preset release --parallel 4
python python/validate_multicurve.py --module-dir build/release --out results/multicurve --require-quantlib
```

The original numerical suite has **1,285 comparisons**. Convention validation
includes independent rate-helper calibration. The OIS/multi-curve suite adds
**230 comparisons** for positive/negative rates, explicit holidays, cash flows
and discount/forwarding quote risk. See the [v2.1 verification record](launch-validation.md)
for measured checks, coverage and platform boundaries.

The [SOFR/OIS and multi-curve guide](multicurve.md) documents:

- `compound_sofr`: ACT/360 compounding with weekend/holiday accrual.
- `OisCurveBootstrapper` and `price_ois`: annual compounded overnight swaps.
- `build_forward` and `price(discount, projection, swap)`: separate curves.
- `multi_curve_risk`: full recalibration for discount and forwarding quote shocks.

For seasoned SOFR trades, use the new `SofrSwap`, `FixingStore`,
`SofrCurveBootstrapper` and `price_sofr` APIs. The
[SOFR lifecycle and market-data guide](sofr-lifecycle.md) documents the
cash-flow conventions, imported quote schema and remaining boundaries.
The original `price_ois` and CLI/replay interfaces retain their earlier scope.

```sh
python python/run_sofr_valuation.py --module-dir build/release --manifest data/sofr/manifest.json --out results/sofr-demo
python python/validate_sofr.py --module-dir build/release --out results/sofr-validation --require-quantlib
python python/test_sofr_replay.py --module-dir build/release --out results/sofr-replay --require-quantlib
```

This example combines **actual captured NY Fed fixings with explicitly synthetic
OIS quotes**. It values a seasoned 1m payer at **-4,448.43** dirty PV and
**-4,593.36** clean PV, with 15 realized and 484 forecast daily observations.
The expanded lifecycle suite passes **30,524** independent comparisons; real-fixing replay
adds **40** cash-flow and dual-curve node/quote-risk comparisons. See the
[v2.2 verification record](sofr-validation.md).

## Other demonstrations

The [retrospective ETF simulator](trading-simulation.md) is a separate experiment
retained in this repository. Its strategy results are outside the rates-engine
pricing, risk and hedge-validation evidence.
