# Basis swaps, clearing and cash collateral

The extension adds same-currency **compounded overnight/overnight basis swaps**
and a **generic contractual clearing and cash-collateral model**, with C++ and
Python APIs. All inputs in its worked example are synthetic.

```sh
python python/run_basis_lifecycle.py --module-dir build/release --out results/basis-lifecycle
python python/validate_basis.py --module-dir build/release --out results/basis-validation --require-quantlib
python python/test_contractual_lifecycle.py --module-dir build/release --out results/contractual-lifecycle
```

Build the [bindings](reproducibility.md#bindings) first. The
[recorded example](results/basis/lifecycle-example.json) contains daily valuations,
payment cash, margin calls and event journals. It starts with a seasoned USD basis
swap, recalibrates three curves daily, settles each leg only when all observations
are realized, returns collateral, pays interest and closes both accounts. Its
calendar excludes weekends only; this is an illustrative contract, not a named
clearinghouse's rulebook or a market mark.

## Basis pricing and calibration

`BasisSwap` pays the base overnight index **plus a simple spread**, receiving the
other overnight index for `Side::Payer`; `Receiver` reverses signed PV. Indices
have separate projection curves, fixing stores, schedules, payment calendars/lags,
lookbacks, observation shifts and lockouts. Discounting uses a third curve. The
spread is added after compounding using the base leg's `fixed_day_count`.

For base overnight PV `B`, other overnight PV `O`, base spread annuity `A`
(including notional), spread `s` and payer sign `z`:

```
dirty PV = z * (O - B - s*A)
par spread = (O - B) / A
clean PV = dirty PV - signed accrued
```

Accrued uses the [SOFR partial-observation convention](sofr-lifecycle.md), with
base spread accrued subtracted for the payer. Settled coupons are excluded;
payment-date inclusion is explicit. Par spread is absent when the base spread
annuity is zero. The leg results retain `SofrResult` coupon containers: base
`fixed_amount` stores spread, while `floating_amount` stores overnight interest.
Use `BasisResult.pv` for the basis trade's signed value.

`BasisCurveBootstrapper(base_conventions, other_conventions).build(...)` calibrates
the **other projection to basis quote instruments**, with discount/base curves
held exogenous. Spot dates and raw maturity follow the base settlement calendar
and end-of-month rule; effective date must also be an other-index business day.
Pillars are the later final leg payments. All quotes reprice after bootstrap with
`1e-12` decimal-spread tolerance. Missing history or curve coverage fails explicitly.

```python
basis_builder = r.BasisCurveBootstrapper(base_conventions, other_conventions)
other = basis_builder.build(discount, base_projection, as_of, basis_quotes,
                            base_fixings, other_fixings)
value = r.price_basis(discount, base_projection, other.curve, trade,
                      base_fixings, other_fixings, as_of)
```

`basis_quote_risk` accepts discount OIS, base OIS and basis quote sets, three
builders and separate discount/base/other fixing stores. Discount shocks rebuild
all three curves; base quote shocks rebuild both projections; basis shocks
rebuild only the other projection. Parallel and pillar risk use
`(PV_down-PV_up)/(2*bump_bps)`, with fixings frozen.

The [independent report](results/basis/validation-report.json) and
[rows](results/basis/comparisons.csv) use QuantLib's
[overnight/overnight basis helper](https://github.com/lballabio/QuantLib/blob/v1.44/ql/experimental/termstructures/basisswapratehelpers.hpp)
without importing C++ curve factors. Coupon comparisons cover unequal leg
frequencies/lags, short final stubs, lookback/shift/lockout, negative rates,
distinct histories and pricing lifecycle states. Risk checks rebuild helper
curves at 0.5 and 1 bp; risk tolerance is `2e-5` currency/bp.

## Clearing events

`ClearingLedger` represents one currency and one contractual netting set. The
caller supplies eligibility decisions, final dates and confirmed payment amounts;
the ledger does not connect to a CCP or make those decisions itself.

| Event | Required state and effect |
| --- | --- |
| `Submit` | Create a unique submitted trade |
| `Accept` | Submitted → accepted; record final payment date in `due` |
| `Reject` | Submitted → rejected; retain reason in `reference` |
| `RegisterPayment` | Accepted; create a unique dated obligation with signed amount |
| `SettlePayment` | Accepted/terminated; settle an unresolved obligation on/after its due date |
| `Terminate` | Accepted → terminated; replace unpaid obligations with a caller-supplied full closeout payment |
| `Close` | Accepted/terminated → closed after final date and all obligations resolved |

Positive amounts are cash received. Delayed payments remain outstanding until
settlement. Termination cancels unpaid original obligations, retains settled cash
and creates a closeout obligation; its amount must include everything replaced.
Registration cannot backdate an obligation or exceed the final payment date.
The ledger does not derive a coupon schedule on acceptance: the caller must
register every contractual obligation before closing.

`net_exposure({trade_id: dirty_pv, ...})` requires exactly one finite valuation
for every accepted/terminated trade. Other states cannot contribute. For terminated
trades, supply the remaining closeout claim's value. Use consistent as-of dates
and payment cut-offs; the ledger validates coverage, not timestamps of plain PVs.

## Cash collateral contract

`CollateralAccount` models **collateralized-to-market cash variation margin**:
principal is separate while the derivative retains its dirty PV. Variation
transfers are not final settlement of derivative value. The distinction is
discussed in the [ISDA settlement paper](https://www.isda.org/a/1SDDE/isda-vm-settlement-whitepaper-final.pdf);
this implementation claims no conformity to a particular CCP or CSA.

For exposure `V`, receipt threshold `H_r` and posting threshold `H_p`:

```
target = max(V-H_r, 0) if V >= 0 else min(V+H_p, 0)
rounded target = sign(target) * ceil(abs(target)/unit) * unit
call = rounded target - settled principal - pending signed transfers
```

Zero rounding disables rounding. Issue a nonzero call when its magnitude is at
least MTA, after rounding. Rounding the target keeps unchanged marks stable with
zero MTA. Positive principal/calls mean received/to receive; negative means posted/
to post. Unsecured exposure is `V - settled principal`; pending calls prevent
duplicate requests but do not reduce current exposure.

| Event | Effect |
| --- | --- |
| `Mark` | Update exposure; create any call keyed by the event ID |
| `SettleTransfer` | Resolve a referenced due call; change principal |
| `CancelTransfer` | Resolve without changing principal; a subsequent mark may replace it |
| `AccrueInterest` | Accrue through the event date at `value`, a decimal annual rate |
| `PayInterest` | Settle all accrued interest separately from principal |
| `ReturnCollateral` | At zero exposure, return all principal, ignoring MTA/rounding; prior calls must be resolved |
| `Close` | Require zero exposure/principal/interest, resolved calls and accrual through closure |

Calls settle after the configured business-day lag; zero lag applies following
adjustment. Transfers and interest payments require business days. Accrue through
the transfer date before changing principal. Simple interest is
`-principal * rate * year_fraction`, using ACT/360 or ACT/365F. Received cash
normally creates interest payable; negative rates reverse it. Split accrual
intervals when rates change. Intraday interest and interest-on-interest are not
modeled. Coupons, collateral principal and interest cash remain separate accounts.

## Audit, validation and boundaries

Both ledgers retain append-only journals. Events are chronological; equal timestamps
preserve insertion order. Identical ID/payload replay is a no-op, including retries
after later events. Conflicting IDs, invalid transitions, nonfinite inputs and
overflow fail without mutation. State is in memory; JSON journals are replayed
into fresh C++ objects by the [accounting checks](results/basis/accounting-report.json).
This is not a durable database, authenticated feed or concurrent service.

The accounting reference independently compounds final cash with Python Decimal
and integrates interest over actual settled balances. Hand-calculated fixtures
and C++ tests cover thresholds, MTA, stable rounding, sign reversal, cancellation,
holidays, negative interest, termination, premature settlement/closure, duplicate
events and failure atomicity. The [verification record](results/basis/verification.json)
records measured checks.

| Local check (22 October 2026) | Result |
| --- | --- |
| Release CTest, bindings enabled | 96/96 passed |
| Debug CTest | 90/90 passed; seven affected tests rechecked after static-analysis cleanup |
| Independent QuantLib basis comparisons | 768/768 passed |
| Independent accounting and JSON replay checks | 14/14 passed |
| Replayed journals | Seven clearing events, 58 collateral events |
| Formatting, clang-tidy, installed consumer | Passed |

These are Windows x64 LLVM-MinGW/Python 3.14/QuantLib 1.44 measurements. CI now
requires the independent basis checks on its platform matrix. Coverage was not
remeasured and a workflow definition is not evidence of a successful remote run.

Scope is constant-notional same-currency compounded overnight basis, one netting
set and cash collateral. IBOR/arithmetic-average/cross-currency basis, principal
exchanges, CCP novation/membership, regulatory eligibility, settlement variation/
PAI, initial-margin computation, noncash collateral/haircuts, optimization,
default waterfalls, disputes and live CCP/payment connectivity remain outside it.
Discount curves are supplied/calibrated explicitly; collateral balances do not
automatically determine them. See [complete limitations](limitations.md).
