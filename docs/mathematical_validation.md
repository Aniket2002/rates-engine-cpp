# Numerical validation

## Independent pathways

The C++ engine uses chrono dates, explicit floating forward coupons and
bisection for log-discount pillars. The Python standard-library reference uses
`datetime`, independently generated schedules, a safeguarded Newton solver,
and telescoping floating PV. Neither reference function imports `pyrates`.

Eight datasets in `examples/validation_cases.json` generate quotes from known
discount curves: flat +3%, zero, -1%, upward/downward slopes, irregular pillars,
one-period and 30-year multi-coupon cases. Node-rate inputs are continuously
compounded annual decimal rates. A ninth case uses the supplied demo quotes.
Generated quote files and CLI logs are retained in each validation output
directory. Recovered discounts are checked against both the independent
bootstrap and the known source curve. Every coupon accrual and discounted
cash flow, base PV, par rate and quote/zero risk bucket is compared.

QuantLib validation uses the same valuation, WeekendsOnly calendar,
ModifiedFollowing, forward schedules, EOM preservation, European 30/360 fixed
leg, ACT/360 floating leg, zero fixing lag and an Actual365Fixed log-linear
DiscountCurve. At-par Ibor coupons match stub accrual intervals. A zero-lag
synthetic IborIndex projects from that curve and DiscountingSwapEngine discounts
on it. Inputs are the C++ node discounts, so this independently checks curve
interpolation and swap pricing, **not** external bootstrapping. The separate
Python reference validates calibration and risk.

## Hand-derived reference

Take a one-year ACT/365F fixed leg with one coupon, `N=1,000,000`, `K=0.04`,
`P(0,0)=1` and `P(0,1)=0.95`. Floating coupons can be quarterly provided they
are contiguous, projected and discounted with the same curve and paid at
accrual end.

```
A = 1 * 0.95 = 0.95
PV_fixed = 1,000,000 * 0.04 * 0.95 = 38,000
PV_float = 1,000,000 * (1-0.95) = 50,000
PV_payer = 12,000
K_par = 0.05 / 0.95 = 0.05263157894736842
```

For an alternative one-year simple ACT/365F deposit quote `r=0.05`,
`V(r)=N*(1-(1+K)/(1+r))`. Tests use exact central differences of this expression
to validate quote DV01. With fixed node DF `P`, zero-node DV01 for a 1bp shift
is `-N*(1+K)*P*sinh(0.0001)` for a one-year payer swap. This validates the sign
and distinction between quote and continuous-zero shocks.

## Tolerances

| Comparison | Absolute tolerance |
| --- | ---: |
| Bootstrap decimal quote residual | 1e-12 |
| Independent recovered DF | 5e-10 |
| Python base/leg/cash-flow PV at 1m notional | 2e-5 currency |
| Python par rate/annuity | 2e-10 |
| Python accrual fraction | 1e-15 |
| Independent finite-difference risk | 0.002 currency/bp |
| QuantLib leg/total PV on identical nodes | 1e-7 currency |
| QuantLib par rate on identical nodes | 1e-12 |

Near-zero PVs use absolute tolerances. Relative errors are left blank when the
reference magnitude is at most `1e-10`, avoiding meaningless division near
zero. Exact algebraic identities have tighter GoogleTest tolerances where
appropriate; the seeded random regression exercises 50 flat curves.

Each comparison is written to `validation.csv` with reference, C++ result,
absolute/relative error, tolerance and PASS/FAIL. `report.json` records counts,
Python/QuantLib versions and failures, including exceptions. A failed validation
exits nonzero after writing its report. The initial binding-load failure and
its resolution are recorded in [execution report](execution_report.md); the
numerical reference comparisons did not fail during that run.
