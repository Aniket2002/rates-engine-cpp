# Version 2.1 verification record

Local verification on 2026-10-21 uses Windows x64, LLVM-MinGW Clang 23.1.3,
Python 3.14.0, QuantLib 1.44 and pybind11 3.1.0. Numerical assertions and
historical inputs are deterministic; wall-clock timings depend on the machine.

| Check | Result |
| --- | --- |
| Release CTest, including Python bindings | 73/73 pass |
| Debug CTest, without optional bindings | 71/71 pass |
| Instrumented coverage CTest, without benchmarks/bindings | 70/70 pass |
| Original independent numerical suite | 1,285/1,285 comparisons pass |
| Convention and independent quote-helper suite | 2,193/2,193 comparisons pass |
| OIS/multi-curve suite | 230/230 comparisons pass |
| Historical independent sample | 114 comparisons pass; dates checked exactly |
| Full pinned Fed replay | 249 dates, 24,900 valuations, zero failed trades |
| PV-change study | Base plus seven scenarios pass independent Python/QuantLib checks |
| clang-format and clang-tidy build | Pass |
| Install/export and external CMake consumer | Pass |

The [machine-readable record](results/launch-validation.json) records test
counts, names, environment and source hashes. Detailed numerical artifacts are
[original comparisons](results/validation.csv), [conventions](results/convention-comparisons.csv),
[multi-curve](results/multicurve/comparisons.csv), [PV changes](results/case-study/pv-changes.csv)
and [historical sample](results/fed-validation.json). The corrected
[historical risk export](results/historical-risk-sample.csv) preserves exact
zero-quote maturity dates, including weekends.

## Coverage and platform boundaries

The library has **1,131/1,143 executable lines covered (98.95%)** and
**573/649 branch outcomes covered (88.29%)**; all 124 emitted functions run.
See [per-file data](results/coverage-launch.json) and [coverage methodology](coverage.md).
The Windows LLVM run retains its existing unused-inline mapping warnings
([diagnostics](results/coverage-launch-diagnostics.txt)); these percentages
are execution evidence, not a proof of mathematical completeness.

Linux/macOS sanitizers and the GCC/Clang/MSVC/macOS matrix run in GitHub Actions,
not in this local Windows record. The workflow requires QuantLib for OIS and
multi-curve validation and retains the new reports as artifacts. Consult the
tag's Actions run for remote status; older CI reports are historical baselines.

## Reproduce

Use the compiler/virtual-environment setup in [reproducibility](reproducibility.md).
Append `.exe` to executable paths on Windows. Configure Release with bindings
to include the multi-curve Python/QuantLib test:

```sh
cmake --preset release -DRATES_BUILD_PYTHON=ON -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build --preset release --parallel 4
ctest --preset release --output-junit launch-tests.xml
cmake --preset debug
cmake --build --preset debug --parallel 4
ctest --preset debug --output-junit launch-tests.xml
python python/validate_results.py --cli build/release/rates_cli --output results/launch/validation --require-quantlib
python python/validate_conventions.py --cli build/release/rates_cli --output results/launch/conventions --require-quantlib
python python/validate_multicurve.py --module-dir build/release --out results/launch/multicurve --require-quantlib
python python/test_real_history.py --batch build/release/rates_batch --cli build/release/rates_cli --out results/launch/historical-validation --require-quantlib
python python/replay_fed_history.py --batch build/release/rates_batch --data data/fed-gsw --out results/launch/fed-replay
python python/generate_case_study.py --cli build/release/rates_cli --out results/launch/case-study --require-quantlib
python python/generate_charts.py --results docs/results --out results/charts
```

The tracked reports are deliberate copies of successful outputs. Chart
manifests hash input text with normalized newlines for cross-platform checkouts.
The charts use no invented observations: curve samples interpolate the recorded
discount factors, risk bars use exported buckets, and PV changes use the
independently validated scenario CSV.
