# rates-engine-cpp

A C++20 interest-rate curve, swap pricing and risk library with independent
numerical validation. Built to demonstrate financial mathematics, numerical
engineering and maintainable C++ through a reproducible, auditable project.

The engine calibrates synthetic deposit/swap quotes, prices vanilla swaps,
and calculates signed parallel quote DV01, quote-bucket risk and zero-node
risk. It includes a command-line application, optional Python bindings,
GoogleTest tests, a separate Python reference, QuantLib comparisons and measured
benchmarks. All supplied quotes are hypothetical; this is a single-curve model.

The [validation/reuse update](docs/validation-upgrade.md) adds measured
[coverage](docs/coverage.md), independent QuantLib rate-helper bootstrapping,
nondefault convention checks and reusable portfolio scenarios. The initial
execution report and benchmark results below are retained as baseline evidence.

## Features

- Gregorian dates, leap years, month/year tenors, month-end schedules and short
  final stubs; weekend/explicit-holiday calendars and business-day adjustment.
- Actual/360, Actual/365 Fixed and European 30/360 accruals; configurable legs.
- Positive discount factors, negative rates, log-linear interpolation, pillar
  diagnostics and explicit rejection of extrapolation.
- Analytic deposit calibration and bracketed swap calibration with intermediate
  coupon interpolation, residual reports and descriptive convergence errors.
- Payer/receiver PV, par rate, per-period cash flows and both leg PVs.
- Full rebootstrap for quote shocks; separate continuous-zero node shocks.
- Reusable `rates_engine` static library and installed `rates::rates_engine`
  CMake target. Financial code is separate from file handling.

See [architecture](docs/architecture.md), [mathematics](docs/methodology.md),
[conventions](docs/conventions.md), [risk](docs/risk_methodology.md),
[validation](docs/mathematical_validation.md) and [execution report](docs/execution_report.md).

## Build and run

Requires a C++20 compiler with standard chrono calendars (GCC 13+, recent Clang
with matching libstdc++/libc++, or MSVC 2022), CMake 3.24+ (3.25+ for the supplied
presets), Ninja, Git and Python 3.10+. GoogleTest 1.17.0 is fetched on first
configure unless an installed GTest package is found. Offline core-only builds
can disable tests. C++17 compilation is not supported.

From the repository root, with the compiler and Ninja available on PATH:

```sh
cmake --preset release
cmake --build --preset release --parallel 4
ctest --preset release
./build/release/rates_cli --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/demo
./build/release/rates_benchmark results/benchmark.csv 25
```

On Windows run from an x64 Visual Studio Developer shell, or use a portable
LLVM-MinGW compiler. Append `.exe` to executable names. The benchmark output's
parent directory must already exist; the preceding CLI command creates `results`.
Exact Windows commands and optional dependencies are in
[reproducibility](docs/reproducibility.md).

```sh
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug
cmake --install build/release --prefix install
cmake -S examples/consumer -B build/consumer -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/install"
cmake --build build/consumer
./build/consumer/consumer
```

To build just the library/CLI without Python or GoogleTest:

```sh
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DRATES_BUILD_BENCHMARKS=OFF
cmake --build build/core
```

## Demonstration

Default valuation is `2025-01-02`. `examples/synthetic_quotes.csv` supplies
1M/3M/6M deposits and 1Y/2Y/3Y/5Y/10Y swap par quotes. Inputs use decimal rates:

```csv
type,label,maturity,rate
deposit,1M,2025-02-02,0.031
swap,5Y,2030-01-02,0.038
```

The full file includes all eight quotes. `examples/sample_swap.csv` describes
a one-million notional 5Y payer swap with coupon 3.7%. Executed Release output:

| Metric | Value |
| --- | ---: |
| Fixed-leg PV | 167,660.057351 |
| Floating-leg PV | 172,191.410251 |
| Payer PV | 4,531.352900 |
| Par rate | 0.038000000000 |
| Parallel quote DV01 | -451.944300 currency/bp |

CSV outputs: `curve.csv`, `calibration.csv`, `cashflows.csv`, `summary.csv` and
`risk.csv`. Cash-flow amounts and leg PVs precede application of the payer or
receiver side sign. The console prints curve nodes, calibration residuals and
risk. `rates_cli --help` lists every option. The CLI accepts alternate valuation,
quote/trade paths, payment intervals, day counts, business-day rule, EOM flag,
holidays, curve identifier and positive bump size. Headers and unquoted CSV
column order are exact; malformed data exits nonzero with an error.

## Validation and optional bindings

```sh
python -m pip install QuantLib pybind11
python python/validate_results.py --cli build/release/rates_cli --output results/validation --require-quantlib
cmake --preset release -DRATES_BUILD_PYTHON=ON -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build --preset release
ctest --preset release
```

The validation script has no third-party dependency unless QuantLib comparison
is requested. It performs independent calendar, coupon, safeguarded-Newton
calibration, pricing and risk calculations in Python. QuantLib separately
prices matched swaps on matching node discounts; it does not replace the engine
or independently bootstrap the quotes. Without QuantLib the report explicitly
records its absence. CSV comparisons include absolute error, relative error
where meaningful, tolerance and status; failures are retained and return a
nonzero exit status.

Tests cover analytical one-period swaps, flat positive/zero/negative curves,
sloping curves, irregular pillars, long swaps, pricing identities, risk signs,
finite-difference stability and malformed inputs. No coverage percentage is
claimed. See the [execution report](docs/execution_report.md) for observed counts
and platform results.

For Python, add `build/release` to `sys.path`, then:

```python
import pyrates as r
v = r.Date("2025-01-02")
end = v.add_months(12)
curve = r.DiscountCurve(v, [(end, 0.95)])
trade = r.Swap(v, end, 1_000_000, 0.04)
print(r.price(curve, trade).pv)
```

`CurveBootstrapper`, `MarketQuote`, `Conventions`, `Calendar`, detailed cash
flows and `RiskEngine` are also exposed; see `python/test_bindings.py`.

## Static checks, sanitizers and benchmarks

```sh
cmake -S . -B build/tidy -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DRATES_CLANG_TIDY=ON -DBUILD_TESTING=OFF
cmake --build build/tidy
cmake --preset sanitizers -DCMAKE_CXX_COMPILER=clang++
cmake --build --preset sanitizers
ctest --preset sanitizers
```

Combined ASan/UBSan is supported on Linux/macOS GCC/Clang, and deliberately
rejected on Windows. CI defines GCC, Clang, MSVC and macOS builds, numerical
validation, bindings, formatting/static checks and a Linux sanitizer job.

Benchmarks use `steady_clock`, three warmups, repeated samples and a volatile
result sink. They record mean/median/minimum microseconds, iteration count,
workload size, compiler, configuration and platform. Workloads include 10,000
discount queries, full calibration, pricing, parallel DV01, both bucket systems
and 20-swap portfolio risk. An intentionally simple linear-scan interpolation
baseline recomputes times/logs. It is not a QuantLib comparison. See
[benchmark methodology and measurements](docs/benchmarks.md).

## Scope and next steps

The synthetic model uses one curve for projection and discounting, forward-only
coupons, zero settlement/payment lag and no historical fixings. It does not
represent modern collateralized multi-curve valuation. It has no real market
feeds, named holiday databases, stochastic options, XVA or production-readiness
claim. Full boundaries are in [limitations](docs/limitations.md).

Priorities for extension: prepared-schedule caching, portfolio scenario reuse,
broader convention-specific validation, richer schedules, then separately
validated collateral and forwarding curves. Correctness precedes performance.

MIT licensed; dependencies retain their own licenses.
