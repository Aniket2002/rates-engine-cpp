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
Parallel risk and every bucket are computed independently; shared scenario
curve caching across a portfolio is a future performance improvement.

`RiskReport` contains valuation date, curve identifier, trade, base valuation,
par rate, leg PVs, both risk coordinate vectors, parallel quote DV01, bump size
and calibration tolerance. CLI `summary.csv` and `risk.csv` serialize it.
