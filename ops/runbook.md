# Synthetic replay operational runbook

The weekly Monday workflow and manual `replay-and-profile` dispatch build and run
a historical simulation. They publish files as GitHub Actions artifacts; there
is no live feed, trading connection or external monitoring endpoint.

## Run a batch

```sh
python python/generate_replay.py --out results/input --trades 1000
./build/release/rates_batch --history results/input/quote_history.csv --portfolio results/input/portfolio.csv --as-of 2025-01-02T10:02:00Z --max-age-seconds 300 --out results/batch
./build/release/rates_batch --history results/input/quote_history.csv --portfolio results/input/portfolio.csv --replay-from 2025-01-02T10:00:00Z --replay-to 2025-01-02T10:15:00Z --step-seconds 120 --max-age-seconds 300 --out results/replay
```

Use a fresh output directory per job. The replay grid includes the start and
each step no later than the end; the end appears only if aligned. Maximum 10,000
query times per invocation. Portfolio CSV has the exact header
`id,effective,maturity,notional,fixed_rate,side`, unique IDs and `payer`/`receiver`
sides. Rates are decimal, risk is signed currency/bp, PV is currency. Batch CSV
uses default pricing conventions; the C++ portfolio API supports per-trade
conventions. Input CSV is intentionally unquoted and rejects malformed rows.

Each batch emits `summary.json`, `trades.csv`, `failed_trades.csv`, `risk.csv`.
Replay stores each timestamp in its own folder and appends one JSON object per
line to `replay.jsonl`. JSON is also written to stdout. Consumers should require
successful exit and summary status before accepting results. Reports are not
atomically committed as a group; disk failure may leave partial files.

## Status and response

| Condition | Exit / summary | Operator action |
| --- | --- | --- |
| All trades priced | 0 / success | Archive inputs, outputs and code revision |
| Some trades fail | 2 / partial | Inspect failed_trades.csv; repair or explicitly exclude trades and rerun |
| All trades fail | 1 / failed | No usable aggregate; inspect trade terms and curve horizon |
| No eligible snapshot | 1 / missing_market_data | Check history path and availability timestamps; never forward-fill from future |
| Latest snapshot too old | 1 / stale_market_data | Investigate missing observations; change age policy only deliberately |
| Bad ordering/schema/changed file | 1 / invalid_market_data | Validate immutable input and regenerate index by restarting |
| Calibration failure | 1 / calibration | Inspect quote units, maturities and model compatibility; no fallback curve |
| Invalid portfolio/options | 1 / invalid_input | Correct the supplied contract and rerun |
| Output I/O failure | 1 / file_io or stderr | Check writable path and disk space; reject incomplete artifacts |

Replay continues after per-timestamp failures. Any failed step makes the overall
exit 1; otherwise any partial step makes it 2. Setup failure emits a root summary
where possible. If disk output is impossible, inspect stdout/stderr and exit.

Schema version 1 includes observation timestamp, age, calibration status,
calibration duration, setup and processing milliseconds, failed/priced/input
counts, schedule cache entries and calibrations. `processing_ms` covers input
setup on the first step and lookup/calibration/pricing, excluding report I/O.
`calibrations_this_run` is zero on reuse and null if a completed count is unknown;
`scenario_calibrations` describes the cached scenario set. Failed-trade count is
null when pricing was not attempted. Aggregate fields are null if no trades price.
Partial aggregates explicitly cover **successful trades only**, never the full
portfolio. No failures are replaced by zero risk.

For a monitoring adapter, alert on nonzero exit, status other than success,
calibration failure, any failed trades, and age above the chosen policy. Track
processing distributions against a baseline on the same hardware; no universal
latency threshold is asserted here. CI artifacts provide a sample publication
channel, not a production alerting service. Retain inputs and commit hashes to
reproduce incidents. Replaying across an already-started coupon needs fixings,
which this library deliberately does not synthesize.
