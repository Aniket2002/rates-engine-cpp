# Model-validation and PV-change case study

## Question and setup

Can the curve recover its quotes, produce independently verified cash flows,
and explain the change in swap PV under rate shocks? Use the README's 1m,
5Y payer at 3.7%, valued on 2025-01-02, with [eight synthetic quotes](../examples/synthetic_quotes.csv).
Hold valuation date, notional, coupon and schedule fixed; rebootstrap every shock.

| Hypothesis | Evidence | Acceptance |
| --- | --- | --- |
| Input instruments reprice | Calibration residuals | Decimal rate error ≤ 1e-12 |
| Independent cash flows and PV agree | 1,285 numerical comparisons plus convention suite | PV error ≤ 2e-5 currency; dates exact |
| Risk metadata identifies the calibrated pillar | Weekend regression, direct/shared risk, CLI/batch exports | Exact date equality |
| Shocked valuations agree independently | Seven scenarios plus base; Python and QuantLib helper bootstraps | PV error ≤ 2e-5 currency |
| Separate discount/forwarding curves are consistent | 230 analytical/QuantLib comparisons | [Per-metric tolerances](multicurve.md) |

## Maturity metadata finding

Continuous-zero calibration used exact dates while quote-risk metadata applied
business-day adjustment. For example, the 2025-01-02 curve's 730-day pillar is
**2027-01-02 (Saturday)**, not 2027-01-04. Valuations and shocks were correct;
the metadata could misidentify or merge exposure buckets. Calibration and risk
now share `quote_pillar`, with tests checking dates as well as sensitivities.
Deposit/swap contract pillars still use business-day adjustment.

## First-order attribution

Stored risk is `DV01_i = (PV_down - PV_up)/(2*bump_bps)`. For quote changes
`delta_i` in bp, predict `-sum(DV01_i * delta_i)`. The residual is full repricing
minus this prediction. It includes higher-order and cross-quote effects, plus
small finite-difference/calibration errors.

Base PV is **4,531.352900**. Parallel DV01 is **−451.944300**; the quote-bucket
sum is **−451.944279**. The tiny difference reflects separate finite differences.

| Shock | Full PV change | Linear estimate | Residual |
| --- | ---: | ---: | ---: |
| −50 bp parallel | −22,897.29 | −22,597.21 | −300.08 |
| −10 bp parallel | −4,531.35 | −4,519.44 | −11.91 |
| −1 bp parallel | −452.06 | −451.94 | −0.12 |
| +1 bp parallel | +451.83 | +451.94 | −0.12 |
| +10 bp parallel | +4,507.58 | +4,519.44 | −11.86 |
| +50 bp parallel | +22,302.89 | +22,597.21 | −294.32 |
| Steepener | +4,517.53 | +4,524.43 | −6.90 |

The steepener shifts 1M/3M/6M by −10 bp, 1Y by −5, 2Y by zero, 3Y by +5,
5Y by +10 and 10Y by +15. A −10 bp parallel move brings the 5Y par quote to
the 3.7% coupon and returns PV to approximately zero. The nonlinear residual
scales approximately quadratically for small parallel moves.

Maximum independent PV disagreement is **4.43e-6 currency**, below the `2e-5`
threshold, for both Python and QuantLib. [Exact rows](results/case-study/pv-changes.csv)
and [report](results/case-study/report.json) are retained.

![PV-change attribution](charts/pv-change.png)

## Reproduction and boundaries

```sh
python python/generate_case_study.py --cli build/release/rates_cli --out results/case-study --require-quantlib
python -m pip install matplotlib
python python/generate_charts.py --results docs/results --out results/charts
```

No time passes: there is no carry, cash settlement, realized trading P&L,
transaction cost or hedge execution. The Fed replay is a separate retrospective
Treasury-proxy experiment. Numerical agreement under matched assumptions does
not establish their suitability for a market trade. See [limitations](limitations.md).
