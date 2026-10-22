# Model and implementation boundaries

- The original CLI/portfolio replay uses single-curve projection and discounting.
  The [v2.1 extension](multicurve.md) adds OIS discounting and separately calibrated
  projection curves. The [basis extension](basis-clearing-collateral.md) calibrates
  same-currency compounded overnight/overnight basis swaps, with no multi-currency
  support or XVA.
  Real Fed fitted Treasury curves
  provide a historical valuation proxy; the swap portfolios are hypothetical.
- Deterministic deposits, fixed/floating and compounded overnight basis swaps. No options,
  caps, floors, convexity adjustment, stochastic models or credit risk.
- The [v2.2 SOFR APIs](sofr-lifecycle.md) value future, seasoned, matured/unpaid
  and settled coupons with availability-aware realized fixings, accrued value,
  lookback, observation shift, lockout and spot/payment lags. Legacy vanilla
  pricing APIs still require future coupons. Neither API supports floating
  spread/gearing, amortization, ex-coupon treatment or principal exchanges.
  The basis API adds a simple spread on its base leg. A generic clearing ledger
  models acceptance/rejection, explicit payment settlement and termination
  closeout; a cash-collateral account models calls, transfers, interest and return.
  Neither connects to a CCP or implements novation, initial-margin calculation,
  settlement variation, noncash collateral, default management or external
  settlement reconciliation. Full trade amendment workflows remain unsupported.
- Gregorian dates restricted to 1900..2199; month-based forward schedules with
  short final stubs. No long/front stubs or IMM rolls. C++ calendars take explicit
  holidays; the SOFR pipeline exports a versioned QuantLib US Government Bond
  calendar with finite coverage. Operators must review calendar updates.
- Log-linear discount interpolation only, with discontinuous instantaneous
  forwards. No extrapolation, smoothing, global fit or quote bid/ask uncertainty.
- Bootstrap solves within log-discount bounds [-50,50]. Pathological or
  inconsistent inputs may fail; failure is surfaced. No claim is made that
  arbitrary mixed quotes satisfy a complete arbitrage analysis.
- Quote risk uses full rebootstrap per scenario, shared across a portfolio.
  No adjoints, automatic differentiation or parallel execution.
- `rates_cli` handles one trade with configurable conventions; `rates_batch`
  supports portfolios using default conventions and strict unquoted CSV.
  Export files are written after computation; disk failure can leave partial
  files and is reported with nonzero exit status.
- Independent Python and QuantLib tests cover selected alternate day counts,
  holidays, business-day rules and irregular schedules, not every combination.
  Expanded SOFR checks cross five schedule families with lookback/shift/lockout,
  distinct payment calendars and lifecycle states; two-curve market-quote risk
  is checked by independent recalibration. This does not extend the supported
  schedule model to front/long stubs or establish exhaustive convention coverage.
- QuantLib rate helpers independently calibrate six compatible cases. Irregular
  forward-stub calibration contracts are excluded from helper comparisons where
  QuantLib's schedule conventions differ; irregular trade schedules are checked
  separately. No QuantLib performance comparison is claimed.
- Benchmarks describe one machine and workload. They are not service latency
  guarantees. Measured coverage identifies remaining unexecuted paths; it does
  not establish mathematical completeness.
- Historical replay supports synthetic snapshots and real fitted zero curves with
  an in-memory timestamp/offset
  index and immutable local files. The first snapshot defines the quote-label
  universe; there is no independent feed completeness manifest. File size and
  modification time detect ordinary mutation, not adversarial tampering.
- Replay values at the selected snapshot's valuation date. It does not roll
  curves to a later query date or supply missing past fixings. The example is
  intraday in the original example; trades with already-started coupons fail explicitly.
- Fed GSW historical data are fitted Treasury estimates from the downloaded,
  possibly revised vintage. Original publication times are unavailable. The
  observation-date labels support retrospective analysis, not point-in-time
  strategy backtesting. Source zero yields are not executable swap quotes.
- The SOFR data adapter retrieves public NY Fed observations over HTTPS with
  bounded retries, validates completeness and archives raw responses with hashes.
  Exact historical publication timestamps are unavailable: a downloaded fixing
  becomes usable only at its first local capture time. Changed values become
  new immutable local revisions. This cannot reconstruct an uncaptured historic
  information set. Missing required past fixings fail, including before their
  publication; they are never replaced with forecasts.
- OIS quote import checks source, units, timestamps, freshness, calendar and
  conventions, required tenors, bid/ask bounds and configured rate/spread limits.
  It does not supply a licensed tradable quote feed, source authentication,
  market-wide arbitrage certification, operational monitoring or a service SLA.
  Stored hashes detect corruption, not maliciously regenerated snapshots.
  The committed real-fixing demonstration uses synthetic OIS quotes.

This is a reproducible quantitative engineering project, not a complete
institutional derivatives platform or a claim of production readiness.

The separate [ETF simulator](trading-simulation.md) is single-instrument long/cash,
with fixed modeled spreads/fees and next-original-open fills. It has no liquidity,
market-impact, corporate-action, settlement or interest model. Its first public
TLT example covers an action-free interval and is explicitly retrospective.
Availability checks protect the signal from future inputs under the supplied
timestamps; they cannot certify the historical provenance of those timestamps.

Next improvements: front/long stubs, IBOR/arithmetic-average/cross-currency basis,
spread/gearing support beyond the basis leg, provider/CCP/payment connectors,
initial-margin models and durable contract-event storage.
