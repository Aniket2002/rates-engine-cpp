"""Reproducible synthetic SOFR hedge sizing and full-repricing stress study.

Amounts are USD, rates decimal, signed DV01 = (PV_down - PV_up)/(2*bump_bps).
No time passes: scenario changes exclude carry, financing and rebalancing.
"""
import argparse
import csv
import hashlib
import itertools
import json
import math
from pathlib import Path
import sys


def solve(matrix, rhs):
    """Small pivoted linear system; reject degenerate hedge universes."""
    n = len(rhs)
    a = [list(row) + [value] for row, value in zip(matrix, rhs)]
    scale = max((abs(x) for row in matrix for x in row), default=0)
    for col in range(n):
        pivot = max(range(col, n), key=lambda i: abs(a[i][col]))
        if abs(a[pivot][col]) <= max(1e-14 * scale, 1e-30):
            raise ValueError("Degenerate hedge risk matrix")
        a[col], a[pivot] = a[pivot], a[col]
        factor = a[col][col]
        a[col] = [x / factor for x in a[col]]
        for row in range(n):
            if row != col:
                factor = a[row][col]
                a[row] = [x - factor*y for x, y in zip(a[row], a[col])]
    return [row[-1] for row in a]


def bounded_hedge(target, columns, bound):
    """Exhaustive box active sets for a small, full-rank hedge universe.

    Minimize ||target + H*x||^2, with -bound <= x_i <= bound.
    The best feasible stationary point over every face is the global minimum.
    """
    if not columns or len(columns) > 6 or not target or bound <= 0:
        raise ValueError("Expected 1..6 hedges, nonempty risk and a positive bound")
    if any(len(c) != len(target) for c in columns):
        raise ValueError("Risk dimensions differ")
    if not all(math.isfinite(x) for x in [bound, *target, *(v for c in columns for v in c)]):
        raise ValueError("Nonfinite hedge input")
    dot = lambda a, b: math.fsum(x*y for x, y in zip(a, b))
    gram = [[dot(a, b) for b in columns] for a in columns]
    solve(gram, [0.] * len(columns))  # Fail explicitly on rank deficiency.
    best = None
    for state in itertools.product((-1, 0, 1), repeat=len(columns)):
        x = [s * bound for s in state]
        free = [i for i, s in enumerate(state) if s == 0]
        residual = [t + sum(c[j]*v for c, v in zip(columns, x)) for j, t in enumerate(target)]
        if free:
            values = solve([[gram[i][j] for j in free] for i in free],
                           [-dot(columns[i], residual) for i in free])
            for i, value in zip(free, values):
                x[i] = value
        if any(abs(v) > bound + 1e-10 for v in x):
            continue
        residual = [t + sum(c[j]*v for c, v in zip(columns, x)) for j, t in enumerate(target)]
        objective = dot(residual, residual)
        if best is None or objective < best[0]:
            best = (objective, x, residual)
    return best[1], best[2]


def run(module_dir, require_quantlib=False):
    sys.path.insert(0, str(Path(module_dir).resolve()))
    import pyrates as r
    terms = [6, 12, 24, 36, 60, 84, 120]
    rates = [.039, .038, .0365, .0355, .034, .0335, .033]
    hedge_terms = [24, 48, 72]
    inputs = dict(as_of="2025-01-02T16:00:00Z", terms_months=terms, rates=rates,
                  portfolio_months=60, portfolio_notional=1e6, portfolio_coupon=.0375,
                  hedge_months=hedge_terms, maximum_abs_notional_per_hedge=1.5e6,
                  half_spread_bps=.5, ticket_fee_usd=25., notional_lot=1000., synthetic=True,
                  calendar="weekends only", payment_months=12, payment_lag_days=2,
                  settlement_days=2, end_of_month=False)
    c = r.SofrConventions()
    c.end_of_month = False
    ts, day, fixings = r.Timestamp(inputs["as_of"]), r.Date("2025-01-02"), r.FixingStore()
    builder = r.SofrCurveBootstrapper(c)

    def quotes(values):
        return [r.SofrQuote(f"{m}M", m, v) for m, v in zip(terms, values)]

    def curve(values):
        result = builder.build(ts, quotes(values))
        if max(abs(d.residual) for d in result.diagnostics) > 1e-11:
            raise AssertionError("Stress curve failed quote repricing")
        if not all(math.isfinite(df) and df > 0 for _, df in result.curve.nodes):
            raise AssertionError("Invalid discount factors")
        return result.curve

    base = curve(rates)
    make_trade = lambda m, coupon: r.spot_sofr_swap(day, m, 1e6, coupon, r.Side.Payer, c)
    price = lambda d, t: r.price_sofr(d, d, t, fixings, ts)
    original = make_trade(60, inputs["portfolio_coupon"])
    coupons = [price(base, make_trade(m, .03)).par_rate for m in hedge_terms]
    trades = [original] + [make_trade(m, coupon) for m, coupon in zip(hedge_terms, coupons)]
    base_values = [price(base, t) for t in trades]
    risks = [r.sofr_quote_risk(builder, quotes(rates), t, fixings, ts) for t in trades]
    vectors = [[b.dv01 for b in risk.quote_buckets] for risk in risks]
    notionals, residual = bounded_hedge(vectors[0], vectors[1:], 1.5)
    # Executable-size illustrative lots; report the actual rounded hedge risk.
    continuous_notionals = list(notionals)
    notionals = [round(x * 1e6 / inputs["notional_lot"]) * inputs["notional_lot"] / 1e6 for x in notionals]
    residual = [u + sum(x*c[i] for x, c in zip(notionals, vectors[1:])) for i, u in enumerate(vectors[0])]
    costs = [abs(x) * (v.fixed_pv / coupon) * inputs["half_spread_bps"] * 1e-4
             + (inputs["ticket_fee_usd"] if abs(x) > 1e-10 else 0)
             for x, v, coupon in zip(notionals, base_values[1:], coupons)]
    cost = sum(costs)
    shapes = {
        "parallel_up_1bp": [1.] * len(terms),
        "parallel_down_1bp": [-1.] * len(terms),
        "parallel_up_25bp": [25.] * len(terms),
        "parallel_down_25bp": [-25.] * len(terms),
        "steepener": [-25 + 50*(m-terms[0])/(terms[-1]-terms[0]) for m in terms],
        "flattener": [25 - 50*(m-terms[0])/(terms[-1]-terms[0]) for m in terms],
        "butterfly": [-20., -10., 10., 25., 10., -10., -20.],
        "isolated_5y_up_25bp": [25. if m == 60 else 0. for m in terms],
        "parallel_up_200bp": [200.] * len(terms),
        "parallel_down_400bp": [-400.] * len(terms),
    }
    independent = []
    independent_risk = []
    try:
        import QuantLib as q
    except ImportError:
        if require_quantlib:
            raise
        q = None
    if q is not None:
        cal = q.WeekendsOnly()
        q.Settings.instance().evaluationDate = q.Date(2, 1, 2025)

        def reference(values):
            idx = q.OvernightIndex("hedge-study", 0, q.USDCurrency(), cal, q.Actual360())
            helpers = [q.OISRateHelper(2, q.Period(m, q.Months), v, idx, paymentLag=2,
                       paymentCalendar=cal, paymentConvention=q.Following, endOfMonth=False,
                       paymentFrequency=q.Annual, rule=q.DateGeneration.Forward,
                       fixedDayCount=q.Actual360()) for m, v in zip(terms, values)]
            d = q.PiecewiseLogLinearDiscount(q.Date(2, 1, 2025), helpers, q.Actual365Fixed(),
                                          q.IterativeBootstrap(1e-14))
            handle = q.YieldTermStructureHandle(d)
            index = q.OvernightIndex("hedge-study", 0, q.USDCurrency(), cal, q.Actual360(), handle)
            results = []
            for trade, coupon in zip(trades, [inputs["portfolio_coupon"], *coupons]):
                dates = [q.DateParser.parseISO(str(v)) for v in [trade.effective, trade.maturity]]
                schedule = q.Schedule(*dates, q.Period(12, q.Months), cal, q.ModifiedFollowing,
                                      q.ModifiedFollowing, q.DateGeneration.Forward, False)
                swap = q.OvernightIndexedSwap(q.Swap.Payer, 1e6, schedule, coupon, q.Actual360(),
                                             index, 0., 2, q.Following, cal)
                swap.setPricingEngine(q.DiscountingSwapEngine(handle))
                results.append(swap.NPV())
            return results

        def compare(name, values, actual):
            expected = reference(values)
            error = max(abs(a-b) for a, b in zip(expected, actual))
            if error > 2e-5:
                raise AssertionError(f"QuantLib {name}: max error {error}")
            independent.append(dict(scenario=name, maximum_pv_error=error, comparisons=len(actual)))
        compare("base", rates, [v.pv for v in base_values])
        for bucket, term in enumerate(terms):
            up, down = list(rates), list(rates)
            up[bucket] += 1e-4
            down[bucket] -= 1e-4
            expected = [(d-u)/2 for d, u in zip(reference(down), reference(up))]
            error = max(abs(value-vector[bucket]) for value, vector in zip(expected, vectors))
            if error > 2e-5:
                raise AssertionError(f"Independent quote risk at {term}M: error {error}")
            independent_risk.append(dict(tenor_months=term, maximum_dv01_error=error, comparisons=len(expected)))

    scenarios = []
    for name, shock in shapes.items():
        values = [v + b*1e-4 for v, b in zip(rates, shock)]
        shifted = curve(values)
        marks = [price(shifted, t).pv for t in trades]
        if q is not None:
            compare(name, values, marks)
        changes = [v - b.pv for v, b in zip(marks, base_values)]
        hedged = changes[0] + sum(x*v for x, v in zip(notionals, changes[1:]))
        linear = -sum(b*v for b, v in zip(shock, residual))
        scenarios.append(dict(scenario=name, unhedged_change=changes[0], hedged_change=hedged,
                              linear_hedged_change=linear, nonlinear_residual=hedged-linear,
                              hedged_change_after_entry_cost=hedged-cost, shock_bps=shock))
    convergence = []
    for bump in [.1, .5, 1., 2.]:
        measured = [r.sofr_quote_risk(builder, quotes(rates), t, fixings, ts, bump) for t in trades]
        residual_parallel = measured[0].parallel_dv01 + sum(x*v.parallel_dv01 for x, v in zip(notionals, measured[1:]))
        convergence.append(dict(bump_bps=bump, unhedged_parallel_dv01=measured[0].parallel_dv01,
                                hedged_parallel_dv01=residual_parallel))
    norm = lambda v: math.sqrt(sum(x*x for x in v))
    if norm(residual) >= norm(vectors[0]):
        raise AssertionError("Constrained hedge did not reduce bucket norm")
    if max(x["unhedged_parallel_dv01"] for x in convergence) - min(x["unhedged_parallel_dv01"] for x in convergence) > .05:
        raise AssertionError("Unstable bump-size convergence")
    return dict(inputs=inputs, inputs_sha256=hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest(),
                objective="Minimize unweighted sum of squared market-quote DV01 residuals; box limits, then round to USD 1,000 lots (not a discrete optimum)",
                continuous_signed_payer_notionals=[x*1e6 for x in continuous_notionals],
                risk_sign="(PV_down-PV_up)/(2*bump_bps); predicted change = -sum(DV01 * shock_bps)",
                initial_pv=base_values[0].pv,
                hedge_entry_mid_pv=sum(x*v.pv for x, v in zip(notionals, base_values[1:])),
                hedges=[dict(months=m, signed_payer_notional=x*1e6, coupon=k, entry_cost_usd=fee)
                        for m, x, k, fee in zip(hedge_terms, notionals, coupons, costs)],
                entry_cost_usd=cost, unhedged_bucket_norm=norm(vectors[0]), hedged_bucket_norm=norm(residual),
                bucket_norm_reduction=1-norm(residual)/norm(vectors[0]),
                quote_key_rates=[dict(label=b.label, maturity=str(b.maturity), unhedged_dv01=u, hedged_dv01=h)
                                for b, u, h in zip(risks[0].quote_buckets, vectors[0], residual)],
                scenarios=scenarios, bump_convergence=convergence, independent_quantlib=independent,
                independent_quote_risk=independent_risk,
                quantlib_version=q.__version__ if q is not None else None,
                limitations="Synthetic quotes and cost assumptions; static same-date hedge; no realized P&L, funding, margin, basis risk or liquidity model")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    report = run(args.module_dir, args.require_quantlib)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    for name in ["scenarios", "quote_key_rates", "hedges", "bump_convergence"]:
        rows = report[name]
        with (args.out / (name + ".csv")).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
    print(json.dumps({k: report[k] for k in ["bucket_norm_reduction", "entry_cost_usd", "hedges"]}, indent=2))


if __name__ == "__main__":
    main()
