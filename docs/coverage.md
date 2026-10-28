# Measured LLVM line and branch coverage

Scope: compiled executable lines and branch outcomes in `src/*.cpp` and
`include/rates/*.hpp`. Tests, CLI, Python bindings and third-party dependencies
are excluded from the library denominator. These numbers measure execution,
not numerical correctness or exhaustive input coverage.

## Current release: v2.3.0

The [22 October 2026 CI run](https://github.com/Aniket2002/rates-engine-cpp/actions/runs/39770829563)
for commit `00519f48d7674220cdd328d7325fc2bd6e2fadae` measured
**1,851/2,001 lines (92.50%)**, **992/1,238 branch outcomes (80.13%)** and
**188/198 emitted functions (94.95%)**. All **90 coverage CTest entries passed**.
This run used Linux Clang/LLVM 18, Debug, with benchmarks and optional Python
bindings disabled. It measures the compiled C++ library; the Python hedge solver
and SQLite journal wrapper are outside this coverage denominator.

**Basis coverage is substantially lower than the aggregate:** `src/basis.cpp`
executed **93/214 lines (43.46%)** and **44/104 branch outcomes (42.31%)** in
this run. It needs additional instrumented C++ tests for calibration and risk.
The separate QuantLib basis validator runs through optional Python bindings in
regular validation builds; those bindings are disabled in this coverage job.
Its passing comparisons do not fill the gaps in this instrumented report.

The retained [summary](results/coverage-v2.3.0.json),
[text report](results/coverage-v2.3.0-report.txt),
[CTest log](results/coverage-v2.3.0-ctest.log) and
[diagnostics](results/coverage-v2.3.0-diagnostics.txt) were downloaded from that
run's `llvm-coverage` artifact on 27 October 2026. The
[CI record](results/ci-v2.3.0.json) identifies the run and artifact. These are
measurements of the release commit, not an assertion about later revisions.

LLVM reported `warning: 31 functions have mismatched data`. The percentages above
are the report's observed totals with that diagnostic; they are not a warning-free
coverage certification. The retained diagnostics make this limitation visible.

## Historical measurements

The figures below describe earlier code versions and are not v2.3.0 coverage.
Both the code denominator and the toolchain differ from the current release;
the percentages are not a controlled before/after comparison.

Version 2.1 added OIS and multi-curve valuation and measured **1,131/1,143 lines
(98.95%)** and **573/649 branch outcomes (88.29%)**, with all 124 emitted functions
executed and 70 coverage CTest entries passing. See the
[launch summary](results/coverage-launch.json), [retained diagnostics](results/coverage-launch-diagnostics.txt)
and [verification record](launch-validation.md). The prior snapshots below
describe their original code versions and denominators.

On Windows LLVM-MinGW Clang 23.1.3, Debug with `-O0`, the initial suite measured
430/454 lines (94.71%) and 209/262 branch outcomes (79.77%). After the validation
and scenario-reuse update, it measured 549/553 lines (99.28%) and 253/282 branch
outcomes (89.72%). The denominator changed because scenario APIs were added.
35 GoogleTest cases plus independent Python, convention validation and CLI
integration ran under coverage; optional bindings are excluded in this build.

Artifacts: [baseline](results/coverage-baseline.json),
[post-update](results/coverage-upgrade.json), and [baseline text](results/coverage-baseline.txt).
Each JSON includes per-file totals and uncovered branch locations/counts.

Version 2, including portfolio aggregation and historical replay, measured
**821/830 lines (98.92%)** and **372/419 branch outcomes (88.78%)** on the same
Windows Clang toolchain. All 102 instrumented functions executed. The coverage
run passed 48 CTest entries (44 GoogleTest cases and four integration/independent
validation checks); benchmarks and optional bindings are excluded from this build.
See [v2 summary](results/coverage-v2.json). This measures a larger denominator
than v1, rather than a directly comparable fixed-code coverage percentage.

No uncovered branch has been excluded just to raise the percentage. Remaining
gaps include defensive checks for non-finite mutated quantities, some compound
condition outcomes and redundant date validation already enforced by prepared
trades. They remain visible for review.
New replay gaps include low-level file-read/reopen races and defensive index
consistency failures; portfolio aggregate overflow and partial trade failures
are exercised. The complete uncovered locations remain in the JSON/HTML report.

After adding real Fed historical data and direct continuous-zero inputs, the
same toolchain measured **835/844 lines (98.93%)** and **382/429 branch outcomes
(89.04%)**. All 51 coverage CTest entries passed (46 GoogleTest cases plus five
integration/independent checks). The original and v2 snapshots above remain
baseline evidence; [historical-data coverage](results/coverage-historical.json)
contains that stage's per-file totals and uncovered branches.

With the separate ETF trading module, the same toolchain measures
**997/1008 lines (98.91%)** and **515/583 branch outcomes (88.34%)**, with all
109 emitted functions executed. All 62 coverage CTest entries pass (56 GoogleTest
cases and six integration/independent checks); Release additionally runs the
performance contract and Python bindings, for 64 checks. See
[trading-module coverage](results/coverage-trading.json). The new denominator
includes the trading library; its CLI and Python importer remain outside scope.
Remaining trading gaps include defensive arithmetic/statistics failures and
compound validation condition outcomes, recorded without exclusions.

This Windows LLVM run also reports zero-hash unused-inline mapping warnings when
combining executable objects. The [diagnostic dump](results/coverage-trading-diagnostics.txt)
lists them. They are unused mappings of header accessors/helpers; emitted versions
have measured records. Including the simulator object does not change line totals
versus the tests/CLI/batch objects alone. The coverage tool now preserves LLVM's
diagnostics alongside every report so these warnings remain auditable.

```sh
cmake -S . -B build/coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DRATES_COVERAGE=ON -DRATES_BUILD_BENCHMARKS=OFF
cmake --build build/coverage --target coverage --parallel 4
```

Use matching `llvm-cov` and `llvm-profdata` on PATH, or set
`LLVM_COV_EXECUTABLE` / `LLVM_PROFDATA_EXECUTABLE` at configure time. Windows
uses the same portable compiler PATH setup as `reproducibility.md`. The script
creates a fresh profile directory for each run (no stale profile accumulation),
runs CTest, merges all test/CLI subprocess profiles and writes
`build/coverage/coverage/{summary.json,report.txt,coverage.json,uncovered.txt}`
plus navigable HTML at `build/coverage/coverage/html/index.html`. Test failures
are logged and return nonzero even when a coverage report can be produced.
GitHub Actions uploads the HTML and machine-readable reports.

Branch coverage is LLVM's true/false source-condition outcome coverage, not
MC/DC or a count of all possible exception propagation paths. See
[LLVM's source-based coverage documentation](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html).
