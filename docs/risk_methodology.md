# Risk coordinates and finite differences

Let `V(q)` be the PV of the original trade after rebuilding a curve from quote
vector `q`. The coupon, notional, side and schedule stay fixed in every run.
For a positive bump `h` measured in basis points, set `epsilon=h*0.0001`.

```
parallel_quote_DV01 = [V(q-epsilon*1)-V(q+epsilon*1)]/(2*h)
quote_bucket_i     = [V(q-epsilon*e_i)-V(q+epsilon*e_i)]/(2*h)
```

`1` is the all-ones quote vector and `e_i` changes only quote i. Every bumped
curve is fully recalibrated from a fresh copy of the original quotes. Results
are signed currency units per basis point. A typical payer swap has negative
DV01 under this down-minus-up convention. Receiver risk is its negative.

For zero-node risk, hold all other node discounts fixed. At node `t_i`, replace
`P_i` by `P_i*exp(-epsilon*t_i)` for the up shock, and
`P_i*exp(+epsilon*t_i)` for the down shock. Rebuild interpolation without
recalibrating quotes, then apply the same central difference. The time-zero
anchor is excluded. Node indices in the C++ shock API are zero-based among
future pillars; `nodes()` includes the anchor at index zero.

Quote and zero-node sensitivities describe different coordinates. A quote
shock can change later calibrated pillars; a zero-node shock changes only the
specified node before interpolation. These vectors should not generally agree.
The sum of quote buckets approaches parallel quote risk as `h` tends to zero,
but finite-difference nonlinear cross effects prevent exact equality.

Central difference truncation error is order `h^2` for smooth valuations;
roundoff and calibration residual error grow relative to the difference for
very small bumps. Tests compare 0.5bp and 1bp results and permit 0.002 currency
units/bp for nonlinear/calibration effects at a one-million notional. This
absolute tolerance is specific to the fixtures, not a universal risk tolerance.

Shocks must be finite and positive. A shocked quote set that cannot calibrate
raises an exception identifying the quote bucket and decimal shift. The
engine does not drop failing buckets or return zero as a failure value.
`prepare_scenarios` builds a base curve, two parallel shocks and two shocks per
quote: `3 + 2*K` calibrations. Zero shocks alter the base curve without calibration.
The immutable scenario object is reused across trades. `PortfolioRiskEngine`
uses compensated summation and reports failed trades separately; its aggregates
cover only successfully priced trades. Batch output explicitly marks partial results.

`RiskReport` contains valuation date, curve identifier, trade, base valuation,
par rate, leg PVs, both risk coordinate vectors, parallel quote DV01, bump size
and calibration tolerance. CLI `summary.csv` and `risk.csv` serialize it.

For `continuous_zero` inputs, the quotes are exact-date, ACT/365F continuous zero
yields. Quote shocks rebuild those discount nodes analytically, and quote buckets
coincide with zero-node buckets. Historical Fed replay therefore reports fitted
Treasury zero-yield sensitivities, not swap-par-quote hedge exposures. See the
[historical-data guide](real-historical-data.md) for source and timing assumptions.

## SOFR lifecycle with two quote sets

`sofr_multi_curve_quote_risk` applies the same central differences to signed dirty
PV with availability-aware realized fixings held fixed. Write discount calibration
as `D(q_d)` and projection calibration under that discount curve as `P(q_p; D)`:

```
V(q_d, q_p) = price_sofr(D(q_d), P(q_p; D(q_d)), trade, fixings, as_of).pv
```

A discount-quote shock changes both calibrated curves; projection quotes remain
unchanged. A projection-quote shock holds the base discount curve fixed. Each
coordinate has its own parallel shock and buckets, labelled by the actual
calibration payment pillars. The function accepts separate builders/conventions,
reports the bump size and supports payment-date inclusion explicitly. The replay
uses end-of-day payment exclusion.

Fully realized unpaid coupons have zero projection risk but can retain discount
risk until payment; settled trades have zero trade PV and risk. The quote sets
must still calibrate with sufficient discount coverage for projection instruments.
`sofr_curve_risk` provides a different diagnostic by shifting zero nodes directly
and freezing the other curve. The [validation record](sofr-validation.md) checks
both against independent QuantLib calculations. Two supplied OIS quote sets do
not constitute a basis-swap or collateral-agreement calibration.

`basis_quote_risk` adds a separate basis-instrument coordinate. Its dependency
chain is `D(discount quotes) → B(base OIS quotes; D) → O(basis quotes; D, B)`.
Discount shocks rebuild all three curves, base shocks rebuild `B` and `O`, and
basis shocks rebuild only `O`. Independent discount/base/other fixing stores
remain unchanged. The [basis guide](basis-clearing-collateral.md) documents the
spread sign, instruments and QuantLib helper validation. Cash-collateral balances
are accounting state and do not change these pricing curves automatically.
