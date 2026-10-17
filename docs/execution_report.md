# Implementation and execution report

Date: 2026-10-17. This report records observed local results; CI status is
reported separately below. All financial inputs are hypothetical.

## 1. Implementation summary

Implemented a reusable C++20 library, CLI, optional pybind11 module, independent
Python reference, QuantLib comparison, GoogleTest suite, benchmark executable,
CMake installation/export and cross-platform GitHub Actions workflow. The
project occupies its own `rates-engine-cpp` directory; adjacent projects were
not modified.

## 2. Directory tree

```text
rates-engine-cpp/
  CMakeLists.txt, CMakePresets.json, README.md, LICENSE
  .clang-format, .clang-tidy, .gitignore
  include/rates/
    date.hpp, conventions.hpp, schedule.hpp, discount_curve.hpp
    swap.hpp, swap_pricer.hpp, curve_bootstrapper.hpp, risk_engine.hpp
  src/
    date.cpp, conventions.cpp, schedule.cpp, discount_curve.cpp
    swap_pricer.cpp, curve_bootstrapper.cpp, risk_engine.cpp
  apps/rates_cli.cpp
  tests/test_engine.cpp
  python/
    reference_pricer.py, validate_results.py, bindings.cpp
    test_bindings.py, test_cli.py
  benchmarks/benchmark_main.cpp
  cmake/rates_engineConfig.cmake.in
  examples/
    synthetic_quotes.csv, sample_swap.csv, validation_cases.json
    consumer/CMakeLists.txt, consumer/main.cpp
  docs/
    methodology.md, conventions.md, risk_methodology.md, architecture.md
    mathematical_validation.md, limitations.md, reproducibility.md
    benchmarks.md, execution_report.md, results/
  .github/workflows/ci.yml
```

## 3. Completed features

Calendar dates/tenors, leap/month-end handling, short final stubs, explicit
holidays, three day counts, typed rate units, deposit and sparse-swap bootstrap,
log-linear discount curves, negative rates, cash-flow pricing, payer/receiver
valuation, par rate, signed parallel quote risk, quote buckets, zero-node risk,
structured output, diagnostics and explicit failure handling are implemented.
Python bindings expose compiled curve/calibration/pricing/risk functionality.

## 4. Mathematical conventions

Synthetic single-curve model, valuation-date deposit/swap quotes, six-month
30E/360 fixed leg, three-month ACT/360 floating leg, ACT/365F curve time,
Modified Following, weekends plus explicit holidays, no payment lag. Rates
are decimals and risk is currency/bp. `DV01=(PV_down-PV_up)/2` for 1bp shocks.
See [methodology](methodology.md) and [conventions](conventions.md).

## 5. Local build results

- Windows AMD64, portable LLVM-MinGW Clang 23.1.3: Release and Debug built.
- Release pybind11 3.1.0 / Python 3.14.0 extension built and imported.
- `-Wall -Wextra -Wpedantic`: no unexplained project compiler warnings.
- clang-tidy from the matching LLVM-MinGW toolchain: library, CLI and benchmark
  checks passed after corrections described below.
- Installation/export consumed successfully by `examples/consumer`:
  independently configured executable printed one-year swap PV `12000`.
- Local combined ASan/UBSan was not run: this Windows build does not support
  the project's combined sanitizer configuration. CI contains a Linux job.

The corrected [GitHub Actions run 39007088486](https://github.com/Aniket2002/rates-engine-cpp/actions/runs/39007088486)
completed successfully for implementation commit `3148d00`. All six jobs passed:

| CI job | Observed result |
| --- | --- |
| Linux GCC Release | Build, 32 CTest checks, QuantLib validation, installed consumer passed |
| Linux Clang Debug | Build, 32 CTest checks, QuantLib validation, installed consumer passed |
| Windows MSVC Release | Build, 32 CTest checks, QuantLib validation, installed consumer passed |
| macOS AppleClang Release | Build, 32 CTest checks, QuantLib validation, installed consumer passed |
| Linux Clang ASan + UBSan | Build and 31 CTest checks passed |
| Static | Formatting and clang-tidy compilation passed |

The CI summary is retained in [ci-report.json](results/ci-report.json). The
subsequent documentation-only commit records these results and skips a redundant
CI rerun; it does not change the tested implementation or workflow.

## 6. Unit/integration results

29 GoogleTest cases passed, including 50 deterministic randomized flat-curve
trials within one case. Release CTest: 32/32 (29 GoogleTest, independent Python,
CLI contract and Python bindings). Debug CTest: 31/31 (bindings disabled in the
local Debug configuration). Coverage instrumentation was not run; no percentage
is reported. CLI tests exercise invalid input and an alternate day-count,
payment-frequency and explicit-holiday scenario.

## 7. Python and QuantLib results

1,285/1,285 numerical comparisons passed across nine scenarios: 1,190 independent
Python checks, 59 analytical target-node checks and 36 QuantLib checks. QuantLib 1.44
was available and external comparisons executed. Python independently
bootstraps, prices and shocks curves. QuantLib uses matching C++ node discounts
and independently constructs/prices matched vanilla swaps. It does not
independently bootstrap calibration quotes. See the
[raw validation report](results/validation-report.json) and
[comparison table](results/validation.csv).

Demo: fixed PV `167660.05735082837`, floating PV `172191.41025098207`,
payer PV `4531.3529001536954`, par rate `0.037999999999730749`,
parallel quote DV01 `-451.94430030423973`. Largest absolute demo calibration
residual was approximately `6.275e-13` decimal rate, below `1e-12`.

## 8. Benchmark results

25 measured iterations after three warmups, on an i9-11900H, Release Clang
23.1.3. Median times: 10,000 discount queries 140.2us, eight-quote calibration
1180.7us, one swap price 5.7us, parallel quote DV01 2172.6us, eight quote buckets
17220.5us, eight zero nodes 92.6us, 20-swap parallel risk 43376.5us. Full
mean/median/minimum data and caveats are in [benchmarks](benchmarks.md).

## 9. Known limitations and review findings

See [limitations](limitations.md). A final numerical review checked date/time
units, adjusted maturity ordering, interpolation consistency during calibration,
coupon signs, telescoping assumptions, bump normalization and quote-copy
semantics. Tests and independent references substantiate those checks; this is
not a claim of an external human model-validation audit.

Observed development failures and corrections:

1. Initial optional Python binding import failed with `ImportError: DLL load
   failed while importing pyrates: The specified module could not be found.`
   Dependencies were identified as MinGW libc++/libunwind DLLs. Static runtime
   linking for the MinGW Python module fixed it; the binding CTest then passed.
2. An unrelated pip clang-tidy could not locate the portable compiler's chrono
   headers. The matching toolchain clang-tidy resolved this environment issue.
3. Static checks identified unnecessarily wide enums, a by-value pillar input,
   obsolete moves after changing that input to const-reference, and missing
   benchmark vector reservations. These were corrected without suppressing
   the findings. Static compilation subsequently passed.
4. Review found that empty quote-bucket requests could return an empty result.
   They now raise a descriptive error, with a regression test.
5. The first CI run passed all engine tests on all four compiler/platform jobs,
   plus Linux sanitizers/static checks, but the Windows installed-consumer link
   failed with `LNK2038` runtime/iterator ABI mismatches. The consumer's build
   type had been omitted, so MSVC selected Debug against the Release library.
   The CI and reproduction commands now explicitly match the consumer build
   type to the installed library. The original failure remains available in
   [CI run 39006939138](https://github.com/Aniket2002/rates-engine-cpp/actions/runs/39006939138).

No numerical validation failures were observed in the executed reference
comparisons. Reports preserve any failure encountered by future runs rather
than skipping it.

## 10. Reproduction

Exact native and Windows commands, optional dependencies, installation checks
and sanitizer commands: [reproducibility.md](reproducibility.md). All recorded
CSV results come from executed programs, not illustrative values.

## 11. Unimplemented items

No required core engine feature remains as a placeholder. Optional extrapolation
was intentionally not added (unsupported requests throw). C++17 compatibility,
coverage measurement, independent QuantLib rate-helper bootstrapping and the
model extensions in the limitations document are not implemented. Unix sanitizer,
MSVC and macOS checks were executed successfully on GitHub-hosted runners as
reported above; they were not executed on the local Windows-MinGW environment.

## 12. Next improvements

Prepare/cache schedules; reuse shocked curves across a portfolio; extend
independent validation for nondefault calendars and conventions; add richer
stub/payment conventions; introduce separate forwarding/discount curves only
with matched data and validation. Preserve the current analytical and numerical
regressions throughout.
