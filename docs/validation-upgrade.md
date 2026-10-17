# Independent validation and reusable risk scenarios

The initial implementation report is retained as historical evidence. This
update adds 2,193 successful comparisons: 1,206 independent Python checks,
612 QuantLib schedule/day-count checks, 36 QuantLib cash-flow valuations and
339 independent QuantLib helper-calibration checks. These supplement the
original 1,285 comparisons. Inputs and output tables are retained in
`examples/convention_cases.json` and `docs/results/convention-*`.

Nine cases cover ACT/365F and ACT/360 alternate legs, European 30/360 floating
and deposit accruals, annual/quarterly frequencies, explicit holidays, all three
business-day rules, leap-day/month-end behavior, short final stubs and a
forward-starting receiver. The Python reference now accepts an immutable
convention set. It generates expected accrual dates from the contractual input,
rather than accepting C++ cash-flow dates as the expected schedule.

The engine defines EOM as calendar month-end followed by adjustment. QuantLib
schedule checks therefore first generate an unadjusted NullCalendar schedule,
then apply the configured bespoke calendar and rule. This explicitly matches
the contract instead of silently relying on QuantLib's business-EOM behavior.
Day counts and forward coupon rates are independently evaluated with QuantLib.

Six scenarios additionally construct `DepositRateHelper` / `SwapRateHelper`
objects and a `PiecewiseLogLinearDiscount` curve directly from market quotes.
This path never receives C++ discount nodes. It compares pillar and coupon-date
discounts, implied helper quotes, trade PV/par rates and every quote DV01 bucket
after independent shocked recalibration. It includes negative/zero quotes,
irregularly spaced pillars, alternative day counts and explicit holidays.

Helpers require positive month tenors and regular calibration swaps. QuantLib's
helper-generated backward schedules do not generally match the engine's short
final-stub calibration schedules; those are validated by independent Python.
Forward/stub **trades** are independently priced on the helper-calibrated curve.
Calendar-EOM edge cases use explicit schedule comparisons rather than claiming
unmatched helper conventions are equivalent. The report names the six helper
cases and preserves all failures.

```sh
python python/validate_conventions.py --cli build/release/rates_cli --output results/conventions --require-quantlib
```

`PreparedSwap` owns validated schedules and trade terms. `RiskScenarios` owns
one base curve, two parallel curves, two curves per quote and two curves per
zero node. `RiskEngine::prepare_scenarios` performs `3+2*n_quotes` calibrations;
`RiskScenarios::portfolio` reuses these curves for all trades. The parallel-only
`portfolio_parallel_dv01` path performs two calibrations for the entire portfolio.
Snapshots are immutable values, not a global cache; changed quotes/conventions
or bump sizes require a fresh snapshot. Tests verify exact agreement with
independent trade-by-trade calls, source-input isolation, heterogeneous trade
conventions and failure safety.

Measured local comparison in one Release executable (25 samples; background
machine activity was not isolated): 20 independent parallel-risk calculations
had median 26,524.2us, versus 1,292.0us with shared curves. Plain swap pricing
was 6.3us versus 2.9us with a prebuilt schedule. These are workload-specific
observations, not real-time trading latency claims. Raw data:
[scenario-reuse-benchmark.csv](results/scenario-reuse-benchmark.csv).

Release passed 39/39 CTest checks (35 GoogleTest plus four integrations).
Measured coverage and remaining uncovered branches are described in
[coverage.md](coverage.md).
