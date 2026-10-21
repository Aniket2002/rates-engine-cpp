"""Reprice the README swap under quote shocks and validate the PV-change attribution."""
import argparse
import csv
from datetime import date
import json
import math
from pathlib import Path
import subprocess
import sys

from reference_pricer import bootstrap, price

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "report.json").write_text(json.dumps(dict(passed=False, reason="Validation incomplete"))+"\n")
    try:
        import QuantLib as ql
        import quantlib_reference as qr
    except ImportError:
        ql = None
    if args.require_quantlib and ql is None:
        raise RuntimeError("QuantLib required but unavailable")
    with (ROOT / "examples/synthetic_quotes.csv").open(newline="") as stream:
        quotes = [dict(type=row["type"], label=row["label"], maturity=date.fromisoformat(row["maturity"]),
                       rate=float(row["rate"])) for row in csv.DictReader(stream)]
    with (ROOT / "examples/sample_swap.csv").open(newline="") as stream:
        row = next(csv.DictReader(stream))
        trade = dict(effective=date.fromisoformat(row["effective"]), maturity=date.fromisoformat(row["maturity"]),
                     notional=float(row["notional"]), coupon=float(row["fixed_rate"]), side=row["side"])
    valuation = date(2025, 1, 2)

    def run(name, shocks):
        folder = args.out / name
        folder.mkdir(exist_ok=True)
        shifted = [dict(q, rate=q["rate"] + bp*1e-4) for q, bp in zip(quotes, shocks)]
        with (folder / "quotes.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["type", "label", "maturity", "rate"])
            writer.writeheader()
            writer.writerows(shifted)
        result = subprocess.run([str(args.cli.resolve()), "--quotes", str(folder / "quotes.csv"),
            "--swap", str(ROOT / "examples/sample_swap.csv"), "--out", str(folder)], capture_output=True, text=True)
        (folder / "cli.log").write_text(result.stdout + result.stderr)
        result.check_returncode()
        with (folder / "summary.csv").open(newline="") as stream:
            summary = {r["metric"]: r["value"] for r in csv.DictReader(stream)}
        pv = float(summary["pv"])
        reference = price(bootstrap(valuation, shifted), **trade)["pv"]
        external = None
        if ql:
            external = qr.price_on_curve(qr.bootstrap(valuation, shifted)[0], valuation, trade)["pv"]
        if not math.isfinite(pv) or abs(pv-reference) > 2e-5 or (external is not None and abs(pv-external) > 2e-5):
            raise AssertionError(f"Independent PV check failed for {name}: {pv}, {reference}, {external}")
        return pv, summary, abs(pv-reference), abs(pv-external) if external is not None else None

    zero = [0]*len(quotes)
    base, summary, base_error, base_ql_error = run("base", zero)
    with (args.out / "base/risk.csv").open(newline="") as stream:
        buckets = {r["label"]: float(r["dv01"]) for r in csv.DictReader(stream) if r["coordinate"] == "market_quote"}
    scenarios = [(f"parallel_{bp:+d}bp", [bp]*len(quotes), bp) for bp in [-50, -10, -1, 1, 10, 50]]
    scenarios.append(("steepener", [-10, -10, -10, -5, 0, 5, 10, 15], None))
    rows = []
    for name, shocks, parallel in scenarios:
        pv, _, error, qerror = run(name, shocks)
        # Risk is signed PV_down minus PV_up, so positive rate changes use a minus sign.
        linear = -sum(buckets[q["label"]]*bp for q, bp in zip(quotes, shocks))
        delta = pv-base
        rows.append(dict(scenario=name, parallel_bps=parallel if parallel is not None else "",
            base_pv=base, shocked_pv=pv, full_pv_change=delta, bucket_linear_change=linear,
            nonlinear_residual=delta-linear, python_pv_error=error,
            quantlib_pv_error=qerror if qerror is not None else ""))
    with (args.out / "pv-changes.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    report = dict(passed=True, scenarios=len(rows), valuation=str(valuation),
        base_pv=base, parallel_quote_dv01=float(summary["parallel_quote_dv01"]),
        bucket_sum=sum(buckets.values()), pv_tolerance=2e-5,
        max_python_pv_error=max([base_error]+[r["python_pv_error"] for r in rows]),
        max_quantlib_pv_error=max([base_ql_error]+[r["quantlib_pv_error"] for r in rows]) if ql else None,
        quantlib_version=ql.__version__ if ql else None,
        shocks_bps={name: dict(zip([q["label"] for q in quotes], shocks)) for name, shocks, _ in scenarios},
        interpretation="Same-date hypothetical quote shocks; full rebootstrap; no carry, cash settlement or realized trading P&L")
    (args.out / "report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
