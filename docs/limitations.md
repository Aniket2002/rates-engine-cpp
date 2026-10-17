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
- Quote risk uses expensive full rebootstrap per scenario. No adjoints,
  automatic differentiation, parallel execution or portfolio scenario reuse.
- CLI handles one trade, strict unquoted CSV and one common convention set.
  Export files are written after computation; disk failure can leave partial
  files and is reported with nonzero exit status.
- Python reference scenarios use the documented default conventions. Explicit
  holiday and alternate day-count functionality is covered in C++ tests; the
  external comparison does not validate every configurable combination.
- QuantLib comparison validates an independently constructed QuantLib swap on
  matching node discounts. Independent curve calibration is checked by Python,
  not QuantLib rate helpers. No QuantLib performance comparison is claimed.
- Benchmarks describe one machine and workload. They are not service latency
  guarantees. Coverage percentages have not been measured.

This is a reproducible quantitative engineering project, not a complete
institutional derivatives platform or a claim of production readiness.

Next improvements: cache prepared schedules, reuse bumped curves across trades,
add explicit front/long stubs and payment lags, expand convention-specific
external validation, then introduce distinct forwarding and collateral curves
with independently validated instruments and data assumptions.
