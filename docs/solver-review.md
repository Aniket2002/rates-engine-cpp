# Calibration solver review (27 October 2026)

Vanilla, OIS and separate-projection calibration now use bracket-preserving
secant steps with bisection fallback. The quote residual must still meet `1e-12`;
a small interval alone cannot report convergence. After two steps that each
fail to halve the bracket, the next step bisects. Rounded endpoint proposals
also fall back to a midpoint. Finite-objective and iteration-limit checks remain.

The bootstrapper constructs one private trial curve per swap pillar and replaces
only its final discount factor and cached log during objective evaluation. Node
dates, times, earlier factors/logs and prepared schedules are reused. Final
repricing constructs the completed curve normally and checks every input quote.
The public curve interface remains immutable. The separate seasoned SOFR and
basis calibrators continue to use bisection; the Python reference uses
safeguarded Newton and independent valuation formulas.

This optimization deliberately targets `CurveBootstrapper`, including its OIS
wrapper and separate-projection calibration. The seasoned SOFR and basis paths
price different cash flows with fixing histories, observation/payment lags and
additional curve dependencies. Extending trial-curve reuse there needs its own
implementation and before/after measurements. Bisection continues to enforce
their residual checks. Retaining it limits the scope of this change; it does not
mean that secant methods would be invalid for those contracts. The timings below
describe only the optimized vanilla/OIS path on the measured Windows machine.

Bracket expansion is inside the instrument-context handler. A pricing error there
now raises a `runtime_error` containing, for example, `Calibration failed for 5Y`
and the original diagnostic. Previously a bracket-phase coverage error escaped as
an unlabeled `out_of_range`. Shared zero-node shock construction also serves both
the direct risk API and prepared scenarios.

## Local measurements

The initial 27 October measurements below used the same eight-quote synthetic
workload before and after the change on
Windows x64, Intel Core i9-11900H (8 cores / 16 logical processors), LLVM-MinGW
Clang 23.1.3, Release `-O3 -DNDEBUG`. Each workload had three warmups and 101 timed
repetitions in one process/thread. No CPU affinity, frequency locking or machine
isolation was used. The benchmark runs were sequential and did not overlap our
builds or tests; ordinary workstation background activity was uncontrolled.

| Workload | Before median (microseconds) | After median (microseconds) |
| --- | ---: | ---: |
| Calibrate eight quotes | 690.9 | 196.0 |
| Parallel quote DV01 | 1,376.4 | 383.6 |
| Eight quote-risk buckets | 11,124.8 | 3,226.6 |
| Twenty independent parallel-DV01 reports | 26,816.6 | 8,396.1 |
| Twenty trades sharing parallel scenarios | 1,496.1 | 539.1 |

These are observed timings for identical engine workloads, including calibration
setup and pricing, with the solver and trial-curve reuse changed together. They
do not isolate the contribution of either change or establish general production
throughput. Raw [before](results/solver-review/before.csv) and
[after](results/solver-review/after.csv) CSVs include all measured workloads.

The [before](results/solver-review/before-calibration.csv) and
[after](results/solver-review/after-calibration.csv) demo calibration diagnostics
show iterations falling from **33/37/36/34/35** to **3/10/11/12/13** for
1Y/2Y/3Y/5Y/10Y swap quotes. These counts exclude bracket endpoint evaluations.
Every final quote residual remains within `1e-12`.

Reproduce the timing workload on each source revision after a Release build:

```powershell
.\build\release\rates_benchmark.exe results/solver-benchmark.csv 101
.\build\release\rates_cli.exe --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/solver-demo
```

Create the output directory first. The
[verification record](results/solver-review/verification.json) identifies source
hashes, executable hashes and the retained validation reports for this change.

## Numerical and failure checks

Regression tests cover a known analytical root, fewer objective evaluations than
bisection, false-position stagnation, reversed objective signs, endpoint roots,
extreme finite brackets, non-finite objectives, invalid settings, iteration
exhaustion, a discontinuity that must fail residual convergence, positive and
negative quote sets, and instrument context for bracket-pricing failures.

The zero-rate test accepts a unit discount within numerical tolerance; it does
not require the solver to land on exactly zero log discount. Negative-rate
discounts must still exceed one. Existing scenario-reuse tests compare direct
and shared zero-risk results and check snapshot isolation.

The initial 27 October Release suite passed **105/105** tests, including the independent Python,
QuantLib, multi-curve, SOFR lifecycle, replay, basis and hedge validations. The
[multi-curve report](results/solver-review/multicurve-report.json) passes 230
comparisons and the [SOFR report](results/solver-review/sofr-report.json) passes
30,524. Acceptance tolerances remain those in the respective validation guides.
See the verification record for additional build configurations and checks.

The [methodology](methodology.md#bootstrap-algorithm) gives the monotonicity
argument for vanilla swaps. The [forward plot](methodology.md#interpolation)
shows the jumps implied by log-linear discount interpolation.

## Cross-check (28 October 2026)

A further audit reproduced an avoidable solver failure: when the initial bracket
was already narrower than the interval tolerance, the solver rejected it before
trying an exactly representable interior root. The residual is now checked at the
interior candidate first. New regressions cover both an ordinary narrow interval
and subnormal endpoints, plus 200 deterministic increasing/decreasing exponential
objectives checked against their known roots and the independent bisection solver.

The retained text artifacts were also normalized to UTF-8/LF before hashing,
matching `.gitattributes`. Their numerical data are unchanged. This prevents
Git's line-ending conversion from invalidating evidence hashes on another system.
The original verification record identifies the initial benchmark implementation;
the [cross-check record](results/solver-review/cross-check.json) identifies the
final source hashes, Release and fresh Debug test logs, static analysis and
documentation checks. Both local suites passed **107/107** tests. The solver
commit `b66a91c` also passed all seven jobs in the
[cross-platform CI run](https://github.com/Aniket2002/rates-engine-cpp/actions/runs/40567221798):
Linux GCC Release, Linux Clang Debug, Windows MSVC Release, macOS Release,
sanitizers, static analysis and coverage. The
[retained CI record](results/solver-review/cross-check-ci.json) identifies those
jobs. These checks belong to the solver commit; the Windows-only timing
observations above are not cross-platform performance measurements.
