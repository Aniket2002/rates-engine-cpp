"""Attainable valuation ranges under quote intervals and explicit forward bounds.

C++ creates the cash-flow LP. SciPy/HiGHS solves it. Exported primal/dual
witnesses are checked here and independently repriced with QuantLib.
This is numerical evidence, not a formal interval-arithmetic certificate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import sys
from datetime import date
from pathlib import Path

import numpy as np
import scipy
from scipy.optimize import linprog


def solve_range(problem):
    """Solve both extrema; fail closed on infeasibility or a failed numerical audit."""
    a = np.asarray([row.coefficients for row in problem.constraints], dtype=float)
    b = np.asarray([row.upper for row in problem.constraints], dtype=float)
    c = np.asarray(problem.objective, dtype=float)
    if not all(np.isfinite(x).all() for x in (a, b, c)):
        raise ValueError("LP coefficients must be finite")
    scale = max(1.0, float(np.max(np.abs(c))))
    anchor = np.zeros((1, len(c)))
    anchor[0, 0] = 1.0
    results = {}
    for name, direction in (("minimum", 1.0), ("maximum", -1.0)):
        objective = direction * c / scale
        result = linprog(objective, A_ub=a, b_ub=b, A_eq=anchor, b_eq=[1.0],
                         bounds=(None, None), method="highs-ds",
                         options={"primal_feasibility_tolerance": 1e-10,
                                  "dual_feasibility_tolerance": 1e-10})
        if not result.success:
            raise ValueError(f"Ambiguity {name} failed (status {result.status}): {result.message}")
        x = result.x
        y = np.asarray(result.ineqlin.marginals)
        v = float(result.eqlin.marginals[0])
        violation = max(0.0, float(np.max(a @ x - b)), abs(float(x[0]) - 1.0))
        stationarity = float(np.max(np.abs(objective - a.T @ y - anchor[0] * v)))
        dual_sign = max(0.0, float(np.max(y)))
        gap = abs(float(objective @ x - b @ y - v)) * scale
        if (violation > 1e-9 or stationarity > 1e-9 or dual_sign > 1e-10
                or gap > 1e-6 + scale * 1e-10):
            raise RuntimeError(f"LP numerical audit failed: primal={violation}, "
                               f"stationarity={stationarity}, dual_sign={dual_sign}, gap={gap}")
        # Also checks positivity and every original C++ constraint before creating a curve.
        problem.witness(x.tolist(), 1e-9)
        results[name] = {
            "pv": float(c @ x), "discounts": x.tolist(),
            "audit": {"max_constraint_violation": violation,
                      "scaled_stationarity_residual": stationarity,
                      "dual_sign_violation": dual_sign, "duality_gap_currency": gap},
            "dual_inequality": y.tolist(), "dual_anchor": v,
            "objective_direction": direction, "objective_scale": scale,
            "active_constraints": [row.label for row, slack in zip(problem.constraints, b - a @ x)
                                   if abs(float(slack)) < 1e-9],
        }
    results["width"] = results["maximum"]["pv"] - results["minimum"]["pv"]
    return results


def native_inputs(r, data):
    if data.get("schema_version") != 1:
        raise ValueError("Expected ambiguity input schema_version=1")
    valuation = r.Date(data["valuation"])
    kinds = {"deposit": r.Instrument.Deposit, "swap": r.Instrument.Swap,
             "zero": r.Instrument.ContinuousZero}
    bands, mids = [], []
    for q in data["quotes"]:
        mid = (q["bid"] + q["ask"]) / 2
        quote = r.MarketQuote(kinds[q["type"]], q["label"], r.Date(q["maturity"]), mid)
        bands.append(r.QuoteBand(quote, q["bid"], q["ask"]))
        mids.append(quote)
    trades = []
    for t in data["portfolio"]:
        side = {"payer": r.Side.Payer, "receiver": r.Side.Receiver}[t["side"]]
        trades.append(r.Swap(r.Date(t["effective"]), r.Date(t["maturity"]),
                             t["notional"], t["coupon"], side))
    return valuation, bands, mids, trades


def check_witness(r, data, problem, result, require_quantlib=False):
    """Reprice original quote contracts and target cash flows, not just LP rows."""
    from reference_pricer import DEFAULT, adjust, fraction

    valuation, _, _, trades = native_inputs(r, data)
    curve = problem.witness(result["discounts"])
    native_pv = sum(r.price(curve, t).pv for t in trades)
    if abs(native_pv - result["pv"]) > 2e-6:
        raise AssertionError("C++ witness repricing differs from LP objective")
    qcurve = None
    try:
        import QuantLib as ql
        from quantlib_reference import qdate, price_on_curve
        qcurve = ql.DiscountCurve([qdate(date.fromisoformat(str(d))) for d in problem.dates],
                                 result["discounts"], ql.Actual365Fixed())
    except ImportError:
        if require_quantlib:
            raise
    comparisons, max_ql_error, quote_checks = 0, 0.0, []
    if qcurve is not None:
        ql_pv = 0.0
        for spec, trade in zip(data["portfolio"], trades):
            independent = dict(spec, effective=date.fromisoformat(spec["effective"]),
                               maturity=date.fromisoformat(spec["maturity"]))
            pv = price_on_curve(qcurve, date.fromisoformat(data["valuation"]), independent)["pv"]
            error = abs(pv - r.price(curve, trade).pv)
            if error > 2e-7:
                raise AssertionError(f"QuantLib target PV mismatch: {error}")
            max_ql_error = max(max_ql_error, error)
            comparisons += 1
            ql_pv += pv
        result["quantlib_pv"] = ql_pv
    for q in data["quotes"]:
        maturity = date.fromisoformat(q["maturity"])
        end = maturity if q["type"] == "zero" else adjust(maturity, DEFAULT)
        if q["type"] == "swap":
            swap = r.Swap(valuation, r.Date(q["maturity"]), 1.0, 0.0)
            rate = r.price(curve, swap).par_rate
            if qcurve is not None:
                qrate = price_on_curve(qcurve, date.fromisoformat(data["valuation"]),
                                      {"effective": date.fromisoformat(data["valuation"]),
                                       "maturity": maturity, "notional": 1.0, "coupon": 0.0})["par_rate"]
        else:
            time = fraction(date.fromisoformat(data["valuation"]), end,
                      "ACT365F" if q["type"] == "zero" else DEFAULT.deposit_dc)
            df = curve.discount(r.Date(end.isoformat()))
            rate = -math.log(df) / time if q["type"] == "zero" else (1 / df - 1) / time
            if qcurve is not None:
                qdf = qcurve.discount(qdate(end))
                qrate = -math.log(qdf) / time if q["type"] == "zero" else (1 / qdf - 1) / time
        if rate < q["bid"] - 1e-9 or rate > q["ask"] + 1e-9:
            raise AssertionError(f"Witness violates quote band: {q['label']}")
        if qcurve is not None:
            if abs(qrate - rate) > 1e-10:
                raise AssertionError("QuantLib quote repricing mismatch")
            comparisons += 1
        quote_checks.append({"label": q["label"], "bid": q["bid"], "ask": q["ask"], "model": rate})
    result["native_pv"] = native_pv
    result["quote_repricing"] = quote_checks
    result["quantlib_comparisons"] = comparisons
    result["max_quantlib_pv_error"] = max_ql_error if qcurve is not None else None
    return result


def quote_hedge_experiment(r, data, baseline, original_width, require_quantlib):
    """A deliberately narrow demonstration of risk invisible to calibration quotes.

    Par hedges have zero PV on every exact-fit curve, so cannot remove ambiguity.
    The deposit hedge is a one-fixed-coupon swap with the same endpoint DF.
    """
    valuation, bands, mids, trades = native_inputs(r, data)
    engine = r.RiskEngine()
    labels = [row.label for row in engine.quote_risk(valuation, mids, trades[0])]

    def risk(trade):
        buckets = {row.label: row.dv01 for row in engine.quote_risk(valuation, mids, trade)}
        return np.array([buckets[label] for label in labels])

    target = sum((risk(t) for t in trades), start=np.zeros(len(labels)))
    hedge_specs, hedge_risks = [], []
    for quote in data["quotes"]:
        if quote["bid"] != quote["ask"] or quote["type"] not in ("swap", "deposit"):
            raise ValueError("Quote hedge experiment requires exact deposit/swap quotes")
        maturity = r.Date(quote["maturity"])
        if quote["type"] == "deposit" and quote["maturity"] > str(valuation.add_months(6)):
            raise ValueError("Deposit hedge experiment supports one fixed coupon (up to 6M)")
        coupon = r.price(baseline, r.Swap(valuation, maturity, 1.0, 0.0)).par_rate
        hedge = r.Swap(valuation, maturity, 1e6, coupon)
        hedge_risks.append(risk(hedge))
        hedge_specs.append({"effective": data["valuation"], "maturity": quote["maturity"],
                            "notional": 1e6, "coupon": coupon, "side": "payer"})
    matrix = np.column_stack(hedge_risks)
    weights = np.linalg.solve(matrix, -target)
    actual_hedges = [dict(t, notional=abs(float(w)) * t["notional"], side="payer" if w > 0 else "receiver")
                     for t, w in zip(hedge_specs, weights) if abs(w) > 1e-12]
    augmented = dict(data, portfolio=data["portfolio"] + actual_hedges)
    v, qb, _, ts = native_inputs(r, augmented)
    problem = r.curve_ambiguity_problem(v, qb, ts, r.ForwardBounds(*data["forward_bounds"]))
    interval = solve_range(problem)
    for name in ("minimum", "maximum"):
        check_witness(r, augmented, problem, interval[name], require_quantlib)
    residual = sum((risk(t) for t in ts), start=np.zeros(len(labels)))
    if np.linalg.norm(residual) > 1e-6:
        raise AssertionError("Quote hedge did not neutralize the quoted sensitivities")
    if abs(interval["width"] - original_width) > 2e-4:
        raise AssertionError("Exact-par hedges unexpectedly changed the ambiguity width")
    return {"description": "Frictionless fractional par hedges; no lots, costs or joint-curve risk.",
            "labels": labels, "before_quote_dv01": target.tolist(), "after_quote_dv01": residual.tolist(),
            "before_norm": float(np.linalg.norm(target)), "after_norm": float(np.linalg.norm(residual)),
            "risk_matrix_condition_number": float(np.linalg.cond(matrix)), "hedges": actual_hedges,
            "dates": [str(d) for d in problem.dates], "bounds": interval}


def run_study(r, data, require_quantlib=False):
    valuation, bands, mids, trades = native_inputs(r, data)
    forwards = r.ForwardBounds(*data["forward_bounds"])
    baseline = r.CurveBootstrapper().build(valuation, mids).curve
    problem = r.curve_ambiguity_problem(valuation, bands, trades, forwards)
    bounds = solve_range(problem)
    base_dfs = [baseline.discount(d) for d in problem.dates]
    problem.witness(base_dfs)  # A conditional baseline must itself satisfy the assumptions.
    baseline_pv = sum(r.price(baseline, t).pv for t in trades)
    if not bounds["minimum"]["pv"] - 2e-6 <= baseline_pv <= bounds["maximum"]["pv"] + 2e-6:
        raise AssertionError("Baseline lies outside attainable range")
    for name in ("minimum", "maximum"):
        check_witness(r, data, problem, bounds[name], require_quantlib)
    candidates = []
    half_spread = data.get("candidate_half_spread_bps", 0.5) * 1e-4
    if not math.isfinite(half_spread) or half_spread < 0:
        raise ValueError("Candidate half spread must be finite and nonnegative")
    for candidate in data.get("candidate_quotes", []):
        maturity = r.Date(candidate["maturity"])
        # This is a conditional experiment at a synthetic baseline quote, not a forecast.
        assumed = r.price(baseline, r.Swap(valuation, maturity, 1.0, 0.0)).par_rate
        spec = {"type": "swap", **candidate, "bid": assumed - half_spread, "ask": assumed + half_spread}
        augmented = dict(data, quotes=data["quotes"] + [spec])
        v, qb, _, ts = native_inputs(r, augmented)
        expanded = r.curve_ambiguity_problem(v, qb, ts, forwards)
        interval = solve_range(expanded)
        for name in ("minimum", "maximum"):
            check_witness(r, augmented, expanded, interval[name], require_quantlib)
        if (interval["minimum"]["pv"] < bounds["minimum"]["pv"] - 2e-5
                or interval["maximum"]["pv"] > bounds["maximum"]["pv"] + 2e-5):
            raise AssertionError("An additional quote expanded the feasible PV range")
        candidates.append({"label": candidate["label"], "assumed_mid": assumed,
                           "half_spread_bps": half_spread * 1e4, "width": interval["width"],
                           "width_reduction": max(0.0, bounds["width"] - interval["width"]),
                           "dates": [str(d) for d in expanded.dates], "bounds": interval})
    candidates.sort(key=lambda item: (-item["width_reduction"], item["label"]))
    # An input spread ablation separates quote uncertainty from curve-shape uncertainty.
    spread_sweep = []
    for spread_bps in (0.0, 0.5, 1.0, 2.0):
        widened = dict(data, quotes=[dict(q, bid=q["bid"] - spread_bps * 1e-4,
                                         ask=q["ask"] + spread_bps * 1e-4) for q in data["quotes"]])
        v, qb, _, ts = native_inputs(r, widened)
        p = r.curve_ambiguity_problem(v, qb, ts, forwards)
        interval = solve_range(p)
        spread_sweep.append({"added_half_spread_bps": spread_bps,
                             "minimum_pv": interval["minimum"]["pv"],
                             "maximum_pv": interval["maximum"]["pv"], "width": interval["width"]})
    forward_sweep = []
    for lower, upper in data.get("forward_bound_sweep", []):
        p = r.curve_ambiguity_problem(valuation, bands, trades, r.ForwardBounds(lower, upper))
        interval = solve_range(p)
        forward_sweep.append({"lower_forward": lower, "upper_forward": upper,
                              "minimum_pv": interval["minimum"]["pv"],
                              "maximum_pv": interval["maximum"]["pv"], "width": interval["width"]})
    hedge = (quote_hedge_experiment(r, data, baseline, bounds["width"], require_quantlib)
             if data.get("quote_hedge_experiment", False) else None)
    repriced = [bounds[name] for name in ("minimum", "maximum")]
    for candidate in candidates:
        repriced.extend(candidate["bounds"][name] for name in ("minimum", "maximum"))
    if hedge:
        repriced.extend(hedge["bounds"][name] for name in ("minimum", "maximum"))
    validation = {"quantlib_comparisons": sum(w["quantlib_comparisons"] for w in repriced),
                  "repriced_witnesses": len(repriced),
                  "max_native_objective_error": max(abs(w["native_pv"] - w["pv"]) for w in repriced),
                  "max_quantlib_trade_pv_error": max((w["max_quantlib_pv_error"] for w in repriced
                                                      if w["max_quantlib_pv_error"] is not None), default=None)}
    return {"schema_version": 1, "input": data, "baseline_pv": baseline_pv,
            "baseline_discounts": base_dfs, "dates": [str(d) for d in problem.dates],
            "bounds": bounds, "candidate_quotes": candidates, "spread_sweep": spread_sweep,
            "forward_bound_sweep": forward_sweep, "quote_hedge": hedge, "validation": validation,
            "lp": {"objective": problem.objective,
                   "constraints": [{"label": row.label, "coefficients": row.coefficients,
                                    "upper": row.upper} for row in problem.constraints]},
            "interpretation": "Conditional attainable range, not a confidence interval or a market forecast.",
            "scope": "Unseasoned vanilla single-curve swaps; explicit forward bounds; synthetic quotes.",
            "versions": {"python": platform.python_version(), "numpy": np.__version__,
                         "scipy": scipy.__version__}}


def plot_report(report, out):
    import matplotlib
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.hashsalt"] = "rates-curve-ambiguity-v1"
    import matplotlib.pyplot as plt
    dates = [date.fromisoformat(d) for d in report["dates"]]
    time = np.asarray([(d - dates[0]).days / 365 for d in dates])
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.7), layout="constrained")
    colors = {"minimum": "#c44e52", "maximum": "#287c8e", "baseline": "#444444"}
    for name in ("minimum", "maximum", "baseline"):
        dfs = report["baseline_discounts"] if name == "baseline" else report["bounds"][name]["discounts"]
        forward = -np.diff(np.log(dfs)) / np.diff(time) * 100
        axes[0].stairs(forward, time, label=name.capitalize(), color=colors[name], linewidth=1.6)
    axes[0].set(xlabel="Years (ACT/365F)", ylabel="Continuous forward (%)", title="Same quotes, different curves")
    axes[0].legend(frameon=False, fontsize=8)
    sweep = report["forward_bound_sweep"]
    if not sweep:
        sweep = [dict(lower_forward=report["input"]["forward_bounds"][0],
                      upper_forward=report["input"]["forward_bounds"][1],
                      minimum_pv=report["bounds"]["minimum"]["pv"],
                      maximum_pv=report["bounds"]["maximum"]["pv"])]
    for i, item in enumerate(sweep):
        axes[1].plot([item["minimum_pv"] / 1000, item["maximum_pv"] / 1000], [i, i],
                     "o-", color="#287c8e", linewidth=3, markersize=5)
    axes[1].set_yticks(range(len(sweep)), [f"{x['lower_forward']:.1%} to {x['upper_forward']:.1%}" for x in sweep])
    axes[1].invert_yaxis()
    axes[1].axvline(report["baseline_pv"] / 1000, color="#444444", linestyle="--", label="Baseline PV")
    axes[1].set(xlabel="PV (thousands)", ylabel="Assumed forward bounds", title="The assumptions change the range")
    axes[1].legend(frameon=False, fontsize=8)
    ranked = report["candidate_quotes"]
    axes[2].barh([x["label"] for x in ranked], [x["width_reduction"] / 1000 for x in ranked], color="#287c8e")
    axes[2].invert_yaxis()
    axes[2].set(xlabel="Reduction in PV range (thousands)", title="Conditional extra-quote ranking")
    for ax in axes:
        ax.spines[["top", "right"]].set_visible(False)
        ax.grid(alpha=.15)
    lower, upper = report["input"]["forward_bounds"]
    fig.suptitle(f"Curve ambiguity audit | assumed forward bounds {lower:.1%} to {upper:.1%}", fontsize=12)
    fig.savefig(out / "curve-ambiguity.png", dpi=180)
    fig.savefig(out / "curve-ambiguity.svg", metadata={"Date": None})
    svg = out / "curve-ambiguity.svg"
    svg.write_text("\n".join(line.rstrip() for line in svg.read_text(encoding="utf-8").splitlines()) + "\n",
                   encoding="utf-8", newline="\n")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir", type=Path, required=True)
    parser.add_argument("--input", type=Path, default=Path("examples/curve-ambiguity.json"))
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-quantlib", action="store_true")
    parser.add_argument("--plot", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.module_dir.resolve()))
    import pyrates as r
    data = json.loads(args.input.read_text(encoding="utf-8"))
    report = run_study(r, data, args.require_quantlib)
    report["input_sha256"] = hashlib.sha256(args.input.read_text(encoding="utf-8").replace("\r\n", "\n").encode()).hexdigest()
    try:
        import QuantLib as ql
        report["versions"]["quantlib"] = ql.__version__
    except ImportError:
        report["versions"]["quantlib"] = None
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n",
                                          encoding="utf-8", newline="\n")
    if args.plot:
        plot_report(report, args.out)
    print(f"PV {report['baseline_pv']:.2f}; attainable "
          f"[{report['bounds']['minimum']['pv']:.2f}, {report['bounds']['maximum']['pv']:.2f}]")
    for item in report["candidate_quotes"]:
        print(f"  {item['label']}: range reduction {item['width_reduction']:.2f}")


if __name__ == "__main__":
    main()
