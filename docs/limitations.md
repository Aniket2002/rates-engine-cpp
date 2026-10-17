# Model and implementation boundaries

- Synthetic single-curve projection and discounting. No collateral or OIS
  discount curve, tenor basis, multi-currency support, XVA or real-market data.
- Vanilla deterministic deposits and fixed/floating swaps only. No options,
  caps, floors, convexity adjustment, stochastic models or credit risk.
- No historical fixings, accrued trade support, floating spread/gearing,
  settlement lag, payment lag, ex-coupon treatment or principal exchanges.
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
- Historical replay uses synthetic full snapshots, an in-memory timestamp/offset
  index and immutable local files. The first snapshot defines the quote-label
  universe; there is no independent feed completeness manifest. File size and
  modification time detect ordinary mutation, not adversarial tampering.
- Replay values at the selected snapshot's valuation date. It does not roll
  curves to a later query date or supply missing past fixings. The example is
  intraday; trades with already-started coupons fail explicitly.

This is a reproducible quantitative engineering project, not a complete
institutional derivatives platform or a claim of production readiness.

Next improvements: add explicit front/long stubs and payment lags, expand convention-specific
external validation, then introduce distinct forwarding and collateral curves
with independently validated instruments and data assumptions.
