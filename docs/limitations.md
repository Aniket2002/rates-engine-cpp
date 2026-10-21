# Model and implementation boundaries

- The original CLI/portfolio replay uses single-curve projection and discounting.
  The [v2.1 extension](multicurve.md) adds OIS discounting and separately calibrated
  projection curves, but no basis-swap instruments, multi-currency support or XVA.
  Real Fed fitted Treasury curves
  provide a historical valuation proxy; the swap portfolios are hypothetical.
- Vanilla deterministic deposits and fixed/floating swaps only. No options,
  caps, floors, convexity adjustment, stochastic models or credit risk.
- No historical fixings in swap valuation, accrued trade support, floating spread/gearing,
  settlement lag, payment lag, ex-coupon treatment or principal exchanges.
  A separate SOFR utility compounds realized fixings; OIS pricing supports fully
  future annual ACT/360 coupons only. No lookback, observation shift or lockout.
- Gregorian dates restricted to 1900..2199; month-based forward schedules with
  short final stubs. No long/front stubs, IMM rolls or named holiday databases.
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

This is a reproducible quantitative engineering project, not a complete
institutional derivatives platform or a claim of production readiness.

The separate [ETF simulator](trading-simulation.md) is single-instrument long/cash,
with fixed modeled spreads/fees and next-original-open fills. It has no liquidity,
market-impact, corporate-action, settlement or interest model. Its first public
TLT example covers an action-free interval and is explicitly retrospective.
Availability checks protect the signal from future inputs under the supplied
timestamps; they cannot certify the historical provenance of those timestamps.

Next improvements: front/long stubs, payment lags, combined realized/forecast
overnight coupons, basis-swap calibration and named market calendars.
