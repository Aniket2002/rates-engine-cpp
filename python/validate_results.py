"""Run CLI, independent Python and optional matched QuantLib comparisons.

Writes all comparisons (including failures) to CSV before returning nonzero.
QuantLib uses the C++ discount nodes to validate interpolation and swap pricing;
it is NOT an independent QuantLib bootstrap comparison. Python does bootstrap.
"""
import argparse
import csv
from datetime import date
import json
import math
from pathlib import Path
import subprocess
import sys

from reference_pricer import (Curve, add_months, adjust, bootstrap, fraction,
                              price, quote_risk, schedule, zero_risk)

ROOT = Path(__file__).resolve().parents[1]


def read_csv(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def quantlib_price(curve, trade):
    import QuantLib as ql
    def qdate(d):
        return ql.Date(d.day, d.month, d.year)
    ql.Settings.instance().evaluationDate = qdate(curve.valuation)
    dates = [qdate(curve.valuation)] + [qdate(d) for d, _ in curve.nodes]
    discounts = [1.0] + [df for _, df in curve.nodes]
    qcurve = ql.DiscountCurve(dates, discounts, ql.Actual365Fixed(), ql.WeekendsOnly())
    handle = ql.YieldTermStructureHandle(qcurve)
    def qsched(months):
        return ql.Schedule(qdate(trade["effective"]), qdate(trade["maturity"]), ql.Period(months, ql.Months),
                           ql.WeekendsOnly(), ql.ModifiedFollowing, ql.ModifiedFollowing,
                           ql.DateGeneration.Forward, True)
    index = ql.IborIndex("Synthetic", ql.Period(3, ql.Months), 0, ql.USDCurrency(),
                        ql.WeekendsOnly(), ql.ModifiedFollowing, True, ql.Actual360(), handle)
    # At-par coupons use each coupon's accrual dates, including final stubs.
    ql.IborCoupon.createAtParCoupons()
    qswap = ql.VanillaSwap(ql.VanillaSwap.Payer if trade.get("side", "payer") == "payer" else ql.VanillaSwap.Receiver,
                         trade["notional"], qsched(6), trade["coupon"], ql.Thirty360(ql.Thirty360.European),
                         qsched(3), index, 0.0, ql.Actual360())
    qswap.setPricingEngine(ql.DiscountingSwapEngine(handle))
    return {"pv": qswap.NPV(), "fixed_pv": abs(qswap.fixedLegNPV()) * (1 if trade["coupon"] >= 0 else -1),
            "floating_pv": qswap.floatingLegNPV() * (1 if trade.get("side", "payer") == "payer" else -1),
            "par_rate": qswap.fairRate()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--output", default=Path("results/validation"), type=Path)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    failures = []
    try:
        import QuantLib as ql
        quantlib_version = ql.__version__
    except ImportError:
        quantlib_version = None
        if args.require_quantlib:
            failures.append("QuantLib required but unavailable")

    def check(case, metric, expected, actual, tolerance, source="python"):
        error = abs(actual-expected)
        ok = math.isfinite(actual) and math.isfinite(expected) and error <= tolerance
        records.append(dict(case=case, source=source, metric=metric, reference=expected, cpp=actual,
                            absolute_error=error, relative_error=error/abs(expected) if abs(expected) > 1e-10 else "",
                            tolerance=tolerance, status="PASS" if ok else "FAIL"))
        if not ok:
            failures.append(f"{case}/{source}/{metric}: {actual} vs {expected}; tolerance {tolerance}")

    def validate_case(name, valuation, quotes, trade, target=None):
        directory = args.output / name
        directory.mkdir(exist_ok=True)
        quote_file, trade_file = directory / "quotes.csv", directory / "swap.csv"
        with quote_file.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["type", "label", "maturity", "rate"])
            for q in quotes:
                writer.writerow([q["type"], q["label"], q["maturity"].isoformat(), repr(q["rate"])])
        with trade_file.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["effective", "maturity", "notional", "fixed_rate", "side"])
            writer.writerow([trade["effective"], trade["maturity"], trade["notional"], trade["coupon"], trade.get("side", "payer")])
        result = subprocess.run([str(args.cli.resolve()), "--valuation", str(valuation), "--quotes", str(quote_file),
                                 "--swap", str(trade_file), "--out", str(directory)], capture_output=True, text=True)
        (directory / "cli.log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f"CLI failed ({name}): {result.stderr}")
        summary = {r["metric"]: r["value"] for r in read_csv(directory / "summary.csv")}
        reference = bootstrap(valuation, quotes)
        for field, expected in price(reference, **trade).items():
            check(name, field, expected, float(summary[field]), 2e-5 if field.endswith("pv") else 2e-10)
        node_rows = read_csv(directory / "curve.csv")[1:]
        cpp_curve = Curve(valuation, [(date.fromisoformat(r["date"]), float(r["discount"])) for r in node_rows])
        for i, (day, df) in enumerate(reference.nodes):
            check(name, "discount_" + str(day), df, cpp_curve.nodes[i][1], 5e-10)
            if target:
                check(name, "recovered_target_" + str(day), target.discount(day), cpp_curve.nodes[i][1], 5e-10, "analytical_input")
        for row in read_csv(directory / "calibration.csv"):
            check(name, "repricing_" + row["label"], float(row["market_quote"]), float(row["model_quote"]), 1e-12)
        for i, row in enumerate(read_csv(directory / "cashflows.csv")):
            start, end = date.fromisoformat(row["start"]), date.fromisoformat(row["end"])
            alpha = fraction(start, end, "30E360" if row["leg"] == "fixed" else "ACT360")
            rate = trade["coupon"] if row["leg"] == "fixed" else (reference.discount(start)/reference.discount(end)-1)/alpha
            check(name, f"accrual_{i}", alpha, float(row["accrual"]), 1e-15)
            check(name, f"cashflow_{i}", trade["notional"]*rate*alpha*reference.discount(end), float(row["pv"]), 2e-5)
        check(name, "parallel_quote_dv01", quote_risk(valuation, quotes, trade), float(summary["parallel_quote_dv01"]), 0.002)
        quote_lookup = {q["label"]: i for i, q in enumerate(quotes)}
        for row in read_csv(directory / "risk.csv"):
            if row["coordinate"] == "market_quote":
                expected = quote_risk(valuation, quotes, trade, quote_lookup[row["label"]])
            else:
                index = next(i for i, (d, _) in enumerate(reference.nodes) if str(d) == row["maturity"])
                expected = zero_risk(reference, trade, index)
            check(name, row["coordinate"]+"_"+row["label"], expected, float(row["dv01"]), 0.002)
        if quantlib_version:
            for field, expected in quantlib_price(cpp_curve, trade).items():
                check(name, field, expected, float(summary[field]), 1e-7 if field.endswith("pv") else 1e-12, "quantlib")

    specifications = json.loads((ROOT / "examples/validation_cases.json").read_text())
    valuation = date.fromisoformat(specifications["valuation"])
    for case in specifications["cases"]:
        try:
            target = Curve(valuation, [(adjust(add_months(valuation, m)), math.exp(-z*fraction(valuation, adjust(add_months(valuation, m)), "ACT365F")))
                                        for m, z in zip(case["months"], case["zeros"])])
            quotes = []
            for m in case["months"]:
                maturity = add_months(valuation, m)
                instrument = "deposit" if m <= 7 else "swap"
                rate = ((1/target.discount(adjust(maturity))-1)/fraction(valuation, adjust(maturity), "ACT360")
                        if instrument == "deposit" else price(target, valuation, maturity, 1, 0)["par_rate"])
                quotes.append(dict(type=instrument, label=f"{m}M", maturity=maturity, rate=rate))
            trade = dict(effective=valuation, maturity=add_months(valuation, case["trade_months"]), notional=1e6, coupon=0.037)
            validate_case(case["name"], valuation, quotes, trade, target)
        except Exception as error:
            failures.append(f"{case['name']}: {type(error).__name__}: {error}")
    # Supplied demo is a separate regression with direct quoted input.
    try:
        quotes = [dict(type=r["type"], label=r["label"], maturity=date.fromisoformat(r["maturity"]), rate=float(r["rate"]))
                  for r in read_csv(ROOT / "examples/synthetic_quotes.csv")]
        validate_case("demo", valuation, quotes, dict(effective=valuation, maturity=add_months(valuation, 60), notional=1e6, coupon=0.037))
    except Exception as error:
        failures.append(f"demo: {type(error).__name__}: {error}")
    if records:
        with (args.output / "validation.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
    report = dict(comparisons=len(records), passed=sum(r["status"] == "PASS" for r in records),
                  quantlib_version=quantlib_version, failures=failures,
                  quantlib_scope="Matched log-linear node curve and VanillaSwap pricing; not QuantLib bootstrapping",
                  python_version=sys.version)
    (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
