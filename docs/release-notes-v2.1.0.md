# v2.1.0 — OIS, multi-curve valuation and validated risk reporting

Continuous-zero quote risk now reports the exact calibrated maturity, including
weekends and holidays. Calibration and reporting share one pillar-date rule;
regressions check direct/shared risk and CLI/batch exports.

The library and Python bindings add ACT/360 SOFR compounding, annual OIS
calibration/pricing, separate discount and forwarding curve calibration, and
quote risk that recalibrates both curves after discount shocks. Existing
single-curve calls retain their behavior. Independent QuantLib OIS/swap helpers
validate 230 new comparisons across three synthetic cases.

The README follows one worked swap from quotes to PV and risk, with three
reproducible PNG/SVG charts. A model-validation case study explains seven PV
shock scenarios and their nonlinear residuals.

Verification: 73 Release tests, 71 Debug tests, 1,285 original numerical
comparisons, 2,193 convention comparisons and all new multi-curve comparisons
pass locally. The full Fed replay completes 24,900 valuations without failed
trades. Library line coverage is 98.95%; formatting, static analysis and the
installed consumer pass. See [the verification record](launch-validation.md).

OIS scope: fully future annual ACT/360 coupons, explicit calendars and zero
payment/settlement lag. No observation shifts, lookbacks, lockouts or mixed
realized/forecast coupons. Existing CSV tools remain single-curve.
See [the extension guide](multicurve.md) for assumptions and reproduction.
