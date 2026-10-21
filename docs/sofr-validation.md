# v2.2 SOFR verification record

The extension is checked at three levels: C++ lifecycle invariants, independent
QuantLib contracts, and an offline replay of captured public fixings. The
[evidence manifest](results/sofr/verification.json) records source hashes and
local checks; the earlier [v2.1 report](launch-validation.md) is historical.

| Check | Result |
| --- | --- |
| Release CTest, Python bindings enabled | 86/86 passed |
| Debug CTest, bindings disabled | 82/82 passed |
| Coverage CTest, benchmarks/bindings disabled | 81/81 passed |
| Independent lifecycle/calibration comparisons | 6,892/6,892 passed |
| Captured-fixing replay and dual-curve node risk | 28/28 passed |
| Replay rejection scenarios | 4/4 rejected |
| Market-data transport/schema/quality tests | 6/6 passed, with multiple rejection cases |
| C++ formatting / clang-tidy | Passed |
| Installed package consumer | Built and ran |

Local builds use Windows x64 LLVM-MinGW and Python 3.14 with QuantLib 1.44.
CI requires QuantLib on Linux GCC/Clang, Windows MSVC and macOS builds, and
separately runs Linux ASan/UBSan, formatting/static analysis and coverage.
CI outcomes belong to the linked commit's Actions run, not this local record.

## Independent models

[Lifecycle report](results/sofr/lifecycle/report.json) and
[comparison rows](results/sofr/lifecycle/comparisons.csv) cover future, seasoned,
matured/unpaid and settled states, payment-date inclusion/exclusion, two-day
lookback with/without observation shift, two-day lockout, explicit government
bond holidays, daily fixing dates/weights and spot/payment-lag calibration.

QuantLib independently generates schedules, coupons and OIS helper curves.
No C++ curve factors are inputs to its bootstrap. Cash-flow/PV tolerance is
`2e-7` currency on 1m notional; clean PV uses `3e-7`, curve factors `5e-10`,
and input repricing `1e-12` decimal rate. Dates and counts require exact matches.
Accrued is independently computed using this project's documented prorated
observation convention; it is not asserted equal to QuantLib's shifted accrued.

[Captured-fixing replay](results/sofr/replay/report.json) checks actual NY Fed
observations with **synthetic OIS quotes**. QuantLib independently calibrates
single and separate projection curves, prices the seasoned swap, and repeats
discount/projection zero-node shocks. Tolerance is `2e-5` currency for cash flows
and PV, and `2e-5` currency/bp for risk. Missing history, corrupt snapshots,
unavailable capture and stale quotes are rejected. The
[full example output](results/sofr/example) includes cash flows, observations,
calibration, risk and input provenance.

The suite caught a real normalization defect: a shifted observation period
can have a different number of calendar days from its contractual interest
period. Normalizing the compound rate before applying contractual accrual
corrected both coupon amounts and calibration; tolerances were not relaxed.

## Coverage and scope

The local [LLVM coverage report](results/sofr/coverage-report.txt) measures
**98.48% lines** and **86.12% branches** across library source/headers. It excludes
Python, bindings, command-line programs and dependencies. LLVM emitted the
same 32 function-profile mismatch warnings seen in earlier local reports;
the raw diagnostics are retained. Coverage is useful path evidence, not proof
of mathematical completeness. Remaining branches include validation failures
and defensive checks for impossible calibration states.

```sh
ctest --preset release
python python/validate_sofr.py --module-dir build/release --out results/sofr-validation --require-quantlib
python python/test_sofr_replay.py --module-dir build/release --out results/sofr-replay --require-quantlib
python python/test_sofr_market_data.py
```

Production data operations, lifecycle events outside pricing and unsupported
contract types remain listed in [limitations](limitations.md).
