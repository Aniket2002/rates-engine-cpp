# Benchmark method and observed measurements

The [27 October solver review](solver-review.md) measures the same calibration
and risk workloads before and after safeguarded secant and trial-curve reuse.
Its source hashes, numerical checks and absolute timings are retained separately.

This page records the original v1 baseline. Current portfolio p50/p95/p99,
shared-scenario comparisons and Linux profiling are described in [v2](v2.md).

Recorded 2026-10-17 on Windows AMD64, Intel Core i9-11900H @ 2.50GHz
(8 cores / 16 logical processors), portable LLVM-MinGW Clang 23.1.3, Release
`-O3 -DNDEBUG`. Single process/thread; no CPU affinity, frequency lock or
background-load isolation. These observations are machine/workload specific.

Each workload has three untimed warmups and 25 timed repetitions. Timing uses
`std::chrono::steady_clock`; computed results are written to a volatile sink.
Reported microseconds are per **entire workload**, not per element. Timing
includes schedules, allocations and result-vector construction where the API
performs those operations. Quote risk includes rebootstrap. Output/file I/O
is outside each measured region.

| Workload | Mean us | Median us | Minimum us | Size |
| --- | ---: | ---: | ---: | ---: |
| Discount queries | 150.768 | 140.2 | 138.0 | 10,000 |
| Linear-scan discount baseline | 238.328 | 238.3 | 231.9 | 10,000 |
| Full calibration | 1192.616 | 1180.7 | 1119.1 | 8 quotes |
| Swap price | 5.772 | 5.7 | 5.5 | 1 swap |
| Parallel quote DV01 | 2179.908 | 2172.6 | 2113.4 | 1 swap |
| Quote key rates | 17023.608 | 17220.5 | 15050.3 | 8 buckets |
| Zero-node risk | 92.348 | 92.6 | 88.2 | 8 nodes |
| Portfolio parallel quote DV01 | 43560.132 | 43376.5 | 37909.6 | 20 swaps |

The discount-query median corresponds to about 71.3 million queries/second
for this in-memory batch, or 14.02ns/query amortized over 10,000 queries. This
is not a separately measured isolated-query latency. Resolution/noise matters
especially for the 5.7us pricing workload.

The baseline is independently coded linear search with per-query time/log
recomputation; the library caches times/logs and performs binary search. It is
deliberately simple, not an optimized competing library. No claim is made about
QuantLib performance or performance changes between engine revisions.

The portfolio workload repeats full parallel DV01 for each of 20 maturities.
It does not reuse shocked curves, and should not be interpreted as an optimized
portfolio-risk implementation. Synthetic input quotes match the demo; the
single-swap benchmark is its 5Y trade. Discount query times span the calibrated
10Y interval deterministically.

Raw data: [benchmark.csv](results/benchmark.csv). Reproduce:

```sh
cmake --preset release
cmake --build --preset release
./build/release/rates_cli --out results/demo
./build/release/rates_benchmark results/benchmark.csv 25
```

Append `.exe` on Windows. Since v2.3, default builds embed compiler runtimes;
the compiler directory is only needed on `PATH` for building. Builds configured
with `RATES_STATIC_RUNTIME=OFF` can still require runtime DLLs when executing.
The optional second benchmark argument is the repetition count (3..10000).
