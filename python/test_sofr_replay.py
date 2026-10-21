"""Offline real-fixing replay, rejection paths and independent QuantLib dual-curve checks."""
import argparse
from copy import deepcopy
from datetime import timedelta
import json
import math
from pathlib import Path
import tempfile

import sofr_market_data as md
from run_sofr_valuation import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent / "data/sofr"
    manifest = md.decode((root / "manifest.json").read_bytes())
    original = {**manifest, **{key: str((root / manifest[key]).resolve()) for key in ("calendar", "fixings", "quotes", "trade")}}
    checks = []

    def check(name, expected, actual, tolerance):
        if not math.isfinite(actual) or abs(expected-actual) > tolerance:
            raise AssertionError(f"{name}: {actual} vs {expected}, tolerance {tolerance}")
        checks.append(dict(name=name, expected=expected, actual=actual, tolerance=tolerance))

    with tempfile.TemporaryDirectory() as directory:
        temp = Path(directory)
        path = temp / "manifest.json"
        def replay(document):
            path.write_bytes(md.canonical(document))
            return run(path, args.module_dir)
        baseline = replay(original)
        if not baseline["realized_observations"] or not baseline["forecast_observations"]:
            raise AssertionError("Expected mixed realized and projected coupon")
        rejected = []
        def reject(name, document):
            try:
                replay(document)
            except (ValueError, RuntimeError):
                rejected.append(name)
            else:
                raise AssertionError(f"Accepted {name}")
        reject("capture unavailable", {**original, "as_of": md.stamp(md.timestamp(original["as_of"])-timedelta(seconds=1))})
        reject("stale quotes", {**original, "as_of": md.stamp(md.timestamp(original["as_of"])+timedelta(hours=2))})
        fixing = md.read_snapshot(original["fixings"], "sofr-fixings")
        fixing["records"] = []
        missing = md.write_snapshot(temp, fixing)
        reject("missing historical fixing", {**original, "fixings": str(missing)})
        missing.write_bytes(missing.read_bytes()+b" ")
        reject("corrupt fixing snapshot", {**original, "fixings": str(missing)})
        # A second, explicitly synthetic quote set isolates projection from discounting.
        quote = md.read_snapshot(original["quotes"], "ois-quotes")
        forward_doc = deepcopy(quote["document"])
        for row in forward_doc["quotes"]:
            for key in ("bid", "rate", "ask"):
                row[key] += .001
        calendar = md.Calendar(md.decode(Path(original["calendar"]).read_bytes()))
        forward = md.import_quotes(md.canonical(forward_doc), calendar, quote["policy"], md.timestamp(quote["received_at"]))
        forward_path = md.write_snapshot(temp, forward)
        dual = replay({**original, "projection_quotes": str(forward_path)})
        if abs(dual["dirty_pv"]-baseline["dirty_pv"]) < 100:
            raise AssertionError("Projection curve change did not affect seasoned swap")
        try:
            import QuantLib as q
        except ImportError:
            if args.require_quantlib:
                raise
            q = None
        if q:
            def qd(iso):
                d = md.day(iso)
                return q.Date(d.day, d.month, d.year)
            v = qd(original["as_of"][:10])
            q.Settings.instance().evaluationDate = v
            cal = q.UnitedStates(q.UnitedStates.GovernmentBond)
            def build(document, discount=None):
                index = q.OvernightIndex("SOFR-replay-helper", 0, q.USDCurrency(), cal, q.Actual360())
                helpers = [q.OISRateHelper(2, q.Period(row["tenor_months"], q.Months), row["rate"], index,
                           paymentLag=2, paymentCalendar=cal, paymentConvention=q.Following,
                           endOfMonth=False, rule=q.DateGeneration.Forward, fixedDayCount=q.Actual360(),
                           discountingCurve=q.YieldTermStructureHandle(discount) if discount else q.YieldTermStructureHandle())
                           for row in document["quotes"]]
                curve = q.PiecewiseLogLinearDiscount(v, helpers, q.Actual365Fixed(), q.IterativeBootstrap(1e-14))
                curve.discount(helpers[-1].pillarDate())
                return curve
            discount = build(quote["document"])
            projection = build(forward_doc, discount)
            trade = md.decode(Path(original["trade"]).read_bytes())
            schedule = q.Schedule(qd(trade["effective"]), qd(trade["maturity"]), q.Period(3, q.Months), cal,
                                  q.ModifiedFollowing, q.ModifiedFollowing, q.DateGeneration.Forward, False)
            fixing = md.read_snapshot(original["fixings"], "sofr-fixings")
            def price(dc, pc):
                index = q.OvernightIndex("SOFR-real-capture-check", 0, q.USDCurrency(), cal, q.Actual360(), q.YieldTermStructureHandle(pc))
                index.clearFixings()
                latest = {row["date"]: row["rate"] for row in fixing["records"] if row["available_at"] <= original["as_of"]}
                for d, rate in latest.items():
                    index.addFixing(qd(d), rate)
                swap = q.OvernightIndexedSwap(q.Swap.Payer, trade["notional"], schedule, trade["fixed_rate"],
                       q.Actual360(), index, 0.0, 2, q.Following, cal, False, q.RateAveraging.Compound, 2, 0, True)
                swap.setPricingEngine(q.DiscountingSwapEngine(q.YieldTermStructureHandle(dc)))
                return swap.NPV(), swap
            for name, pc, report in (("single", discount, baseline), ("dual", projection, dual)):
                pv, swap = price(discount, pc)
                check(name+" PV", pv, report["dirty_pv"], 2e-5)
                for i, (cf, ours) in enumerate(zip(swap.overnightLeg(), report["cashflows"])):
                    check(f"{name} coupon {i}", cf.amount(), ours["floating_amount"], 2e-5)
            # Independent central node shocks on QuantLib curves; no C++ bumped curves reused.
            for field, curve in (("discount_zero_risk", discount), ("projection_zero_risk", projection)):
                pillars = [v]+[qd(row["pillar"]) for row in dual[field]]
                def shocked(bucket, sign):
                    factors = [curve.discount(d)*math.exp(-sign*.0001*q.Actual365Fixed().yearFraction(v, d))
                               if i == bucket else curve.discount(d) for i, d in enumerate(pillars)]
                    return q.DiscountCurve(pillars, factors, q.Actual365Fixed())
                for i, row in enumerate(dual[field], 1):
                    def scenario(sign):
                        changed = shocked(i, sign)
                        return price(changed if field == "discount_zero_risk" else discount,
                                     changed if field == "projection_zero_risk" else projection)[0]
                    check(f"{field}/{row['pillar']}", (scenario(-1)-scenario(1))/2, row["dv01"], 2e-5)
        report = dict(passed=True, checks=checks, rejected=rejected, quantlib_version=q.__version__ if q else None,
                      fixture_as_of=original["as_of"], synthetic_quotes=True,
                      scope="Public NY Fed realized fixings; synthetic OIS quotes; independent cash flows and dual-curve node risk")
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / "report.json").write_text(json.dumps(report, indent=2)+"\n")
        print(f"SOFR replay: {len(checks)} independent comparisons, {len(rejected)} rejected invalid inputs")


if __name__ == "__main__":
    main()
