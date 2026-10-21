# rates-engine-cpp

A C++20 interest-rate curve, swap valuation and risk library, checked against an
independent Python implementation and QuantLib. Start with one question:
**what is a five-year payer swap worth, and how does its value change when rates move?**

The worked example below takes eight quotes through calibration, cash flows, PV
and two risk coordinates. The library also provides portfolio replay, SOFR
compounding, OIS calibration and separate discount and forwarding curves.
[Model boundaries](docs/limitations.md) are explicit.

## Build and price one swap

Requires CMake 3.25+, Ninja, a C++20 compiler and Python 3.10+. GoogleTest 1.17.0
is fetched on the first test build. On Windows use an x64 Visual Studio Developer
shell or the [portable toolchain](docs/reproducibility.md), and append `.exe` to
executable names.

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
./build/release/rates_cli --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/demo
```

At **2 January 2025**, the [inputs](examples/synthetic_quotes.csv) contain
1M/3M/6M deposits and 1Y/2Y/3Y/5Y/10Y par swaps, in decimal rate units.
The [trade](examples/sample_swap.csv) pays **3.7% fixed** on **1,000,000 notional**
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
| Signed parallel quote DV01 | **−451.944300 currency/bp** |

The market par rate exceeds the contractual coupon, so the payer has a positive
PV. The engine reprices every input within a `1e-12` decimal-rate tolerance,
then interpolates log discount factors. [Recorded outputs](docs/results/demo)
include the calibrated curve, residuals, cash flows, summary and risk.

![Calibrated continuous zero-yield curve](docs/charts/curve.png)

## Read the risk correctly

Risk uses **`(PV_down − PV_up) / (2 × bump_bps)`**. A negative number for this
payer means PV rises when rates rise. A +1 bp move predicts about **+451.94**
of PV change before nonlinear effects.

Quote buckets bump one market quote and rebuild the curve. Zero-node buckets
bump one continuous node yield directly. They answer different questions:

![Market-quote and zero-node risk buckets](docs/charts/risk-buckets.png)

The CLI writes `curve.csv`, `calibration.csv`, `cashflows.csv`, `summary.csv`
and `risk.csv`. Risk maturity identifies the **actual calibrated pillar**:
adjusted for deposit/swap contracts, exact for continuous-zero observations.
See [risk definitions](docs/risk_methodology.md) and [conventions](docs/conventions.md).

## Move the quotes and explain the PV change

```sh
python -m pip install QuantLib
python python/generate_case_study.py --cli build/release/rates_cli --out results/case-study --require-quantlib
```

A **+10 bp parallel shock** changes PV by **+4,507.58** after recalibration.
The first-order bucket estimate is **+4,519.44**, leaving a **−11.86** residual.
At +50 bp the residual grows to **−294.32**. All seven scenarios, including a
steepener, pass independent Python and QuantLib quote-helper checks. These are
same-date hypothetical PV changes, not realized trading P&L.

![Full repricing compared with linear risk estimates](docs/charts/pv-change.png)

The [model-validation case study](docs/model-validation-case-study.md) gives
hypotheses, tolerances and results. [Charts](docs/charts) include PNG and SVG
exports, input hashes and a [reproduction script](python/generate_charts.py).

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
and discount/forwarding quote risk. See the [v2.1 verification record](docs/launch-validation.md)
for measured checks, coverage and platform boundaries.

The [SOFR/OIS and multi-curve guide](docs/multicurve.md) documents:

- `compound_sofr`: ACT/360 compounding with weekend/holiday accrual.
- `OisCurveBootstrapper` and `price_ois`: annual compounded overnight swaps.
- `build_forward` and `price(discount, projection, swap)`: separate curves.
- `multi_curve_risk`: full recalibration for discount and forwarding quote shocks.

OIS pricing supports fully future coupons and zero payment/settlement lag.
Existing CLI/replay tools retain their single-curve interpretation.

## Explore the rest of the library

| Area | Entry point |
| --- | --- |
| Historical curves | [249 Fed curves and 24,900 trade valuations](docs/real-historical-data.md) |
| Portfolio aggregation and shared scenarios | [v2 design and scaling measurements](docs/v2.md) |
| Mathematics | [Methodology](docs/methodology.md), [validation](docs/mathematical_validation.md) |
| Performance | [Benchmarks](docs/benchmarks.md), [coverage](docs/coverage.md) |
| Builds, bindings, static analysis and sanitizers | [Reproducibility](docs/reproducibility.md) |
| Installed C++ library | [Consumer example](examples/consumer), [architecture](docs/architecture.md) |
| Batch operations | [Runbook](ops/runbook.md) |
| Separate ETF simulator | [Retrospective TLT demonstration](docs/trading-simulation.md) |

Fed data are fitted Treasury estimates from a downloaded vintage, used as a
proxy for hypothetical swaps. Original publication times are unavailable.
No live feeds, options, XVA or production-readiness claim.
MIT licensed; dependencies retain their own licenses.
