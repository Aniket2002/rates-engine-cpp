# Measured LLVM line and branch coverage

Scope: compiled executable lines and branch outcomes in `src/*.cpp` and
`include/rates/*.hpp`. Tests, CLI, Python bindings and third-party dependencies
are excluded from the library denominator. These numbers measure execution,
not numerical correctness or exhaustive input coverage.

On Windows LLVM-MinGW Clang 23.1.3, Debug with `-O0`, the initial suite measured
430/454 lines (94.71%) and 209/262 branch outcomes (79.77%). After the validation
and scenario-reuse update, it measured 549/553 lines (99.28%) and 253/282 branch
outcomes (89.72%). The denominator changed because scenario APIs were added.
35 GoogleTest cases plus independent Python, convention validation and CLI
integration ran under coverage; optional bindings are excluded in this build.

Artifacts: [baseline](results/coverage-baseline.json),
[post-update](results/coverage-upgrade.json), and [baseline text](results/coverage-baseline.txt).
Each JSON includes per-file totals and uncovered branch locations/counts.
No uncovered branch has been excluded just to raise the percentage. Remaining
gaps include defensive checks for non-finite mutated quantities, some compound
condition outcomes and redundant date validation already enforced by prepared
trades. They remain visible for review.

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
