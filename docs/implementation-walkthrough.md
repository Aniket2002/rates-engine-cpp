# Explaining the implementation

Use these notes alongside the source and reproduce the numerical results before
presenting the project. They are explanations of this implementation's scope.

## Basis calibration

`BasisCurveBootstrapper::build` in `src/basis.cpp` treats discount and base-index
projection curves as inputs. It sorts basis instruments and solves each new
other-index log-discount node so the quoted basis swap has zero PV. Paying base
plus a simple spread and receiving the other compounded index sets the sign.
Each leg can have its own observation/payment conventions. Payment pillars must
cover the actual contractual dates; a tenor label alone is not the pillar.
Interpolation is log-linear, and root bracketing/convergence failures are errors.

Explain why the spread is applied to the base-leg accrual, why a par spread is a
ratio involving its annuity, and why fitting quoted instruments does not certify
arbitrage freedom for every possible trade or interpolation choice.

## Why discount shocks rebuild other curves

Changing a discount quote changes discounted calibration cash flows. Holding
the old projection curve fixed would generally stop the unchanged projection
quotes from repricing. Two-curve risk rebuilds discount and then projection;
three-curve basis risk rebuilds discount, then base projection, then the other
projection. A base quote shock holds discount fixed but rebuilds base and other.
A basis quote shock changes only the other projection. See `src/sofr_curve.cpp`
and `basis_quote_risk` in `src/basis.cpp`.

This is market-quote risk. Directly perturbing a continuous zero node answers a
different question. Risk is signed as `(PV_down-PV_up)/(2*bump_bps)`, so a payer
can have negative DV01 even though its PV rises under an upward rate shock.

## Realized and forecast SOFR

`src/sofr.cpp` constructs daily observation intervals from adjusted coupon dates.
Available fixing revisions are selected as of the valuation timestamp. Missing
past observations fail rather than being forecast. Future intervals use the
projection curve. `log1p`/`expm1` accumulate compounded factors while rejecting
nonpositive daily growth factors.

Lookback changes fixing observation dates. Observation shift also changes the
weights used in compounding; shifting a rate alone is not the same operation.
The implementation annualizes over the observation period and applies the
contractual interest-period ACT/360 accrual. Lockout repeats the chosen earlier
observation. Accrual truncates the interest interval and prorates partial weights.
Payment lag can leave a matured coupon unpaid: that coupon is still part of PV.

Trace a weekend and holiday coupon in `tests/test_sofr.cpp`, then inspect its
observations in the expanded independent validation results. Know which calendar
belongs to accrual observations and which belongs to payment dates.

## Derivatives value versus collateral cash

Dirty PV values unpaid contractual cash flows. Signed accrued is undiscounted;
clean PV here is defined as dirty PV minus signed accrued. None is a cash balance.

`src/lifecycle.cpp` tracks separate clearing cash and collateral principal.
A margin call is a pending request; only a settled transfer changes principal.
Received principal is positive, and ordinary positive remuneration creates
interest payable. Pending transfers prevent duplicate calls but do not reduce
current unsecured exposure. Returning principal is not trading profit.
The generic agreement models collateralized-to-market cash collateral, not
settled-to-market variation or a CCP-specific contract.

The Python workflow adds complete schedule reconciliation. The SQLite wrapper
adds a transaction boundary, immutable definitions and replay. Neither proves
that an external trade, quote, acceptance or cash transfer is authentic.

## What the hedge demonstrates

The study translates sensitivities into bounded, rounded notionals and tests the
result with full repricing. A lower Euclidean bucket norm can still mean a larger
loss under a particular curve twist. Explain the flattener counterexample, why
entry costs matter for small shocks, and why a static same-date exercise is not
realized trading P&L. See `python/hedge_study.py` and its reported assumptions.

No Hull-White model, option calibration or Bermudan exercise engine is implied.
Those require separate modeling, numerical and calibration evidence.
