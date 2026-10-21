"""Validate OIS and multi-curve calibration/pricing/risk against quote-only QuantLib helpers."""
import argparse
import csv
from datetime import date
import json
import math
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.module_dir.resolve()))
    import pyrates as r
    try:
        import QuantLib as ql
    except ImportError:
        ql = None
    args.out.mkdir(parents=True, exist_ok=True)
    records, failures = [], []

    def check(case, metric, expected, actual, tolerance):
        error = abs(expected - actual)
        passed = math.isfinite(expected) and math.isfinite(actual) and error <= tolerance
        records.append(dict(case=case, metric=metric, reference=expected, cpp=actual,
                            absolute_error=error, tolerance=tolerance, status="PASS" if passed else "FAIL"))
        if not passed:
            failures.append(f"{case}/{metric}: {actual} vs {expected}, tolerance {tolerance}")

    # Closed-form, dependency-free check of genuinely different discount/projection curves.
    v = r.Date("2025-01-02")
    c = r.Conventions()
    c.fixed_months = c.floating_months = 12
    c.business_day = r.BusinessDay.Unadjusted
    discount = r.DiscountCurve(v, [(v.add_days(365), math.exp(-0.02))])
    forward = r.DiscountCurve(v, [(v.add_days(365), math.exp(-0.05))])
    t = r.Swap(v, v.add_days(365), 1e6, 0.035, r.Side.Payer, c)
    actual = r.price_multicurve(discount, forward, t)
    check("analytical", "dual_curve_pv", 1e6 * (math.expm1(0.05) - 0.035) * math.exp(-0.02), actual.pv, 1e-8)
    check("analytical", "sofr_weekend", ((1 + 0.04*3/360)*(1+0.05/360)-1)*90,
          r.compound_sofr(r.Date("2025-01-03"), r.Date("2025-01-07"),
                          [(r.Date("2025-01-03"), 0.04), (r.Date("2025-01-06"), 0.05)]), 1e-13)
    spec = json.loads((ROOT / "examples/multicurve_cases.json").read_text())
    if not ql and args.require_quantlib:
        failures.append("QuantLib required but unavailable")

    if ql:
        def qdate(text):
            day = date.fromisoformat(str(text))
            return ql.Date(day.day, day.month, day.year)

        qv = qdate(spec["valuation"])
        ql.Settings.instance().evaluationDate = qv
        for case in spec["cases"]:
            name = case["name"]
            try:
                holidays = case.get("holidays", [])
                cal = ql.BespokeCalendar("matched-weekends-and-explicit-holidays")
                cal.addWeekend(ql.Saturday)
                cal.addWeekend(ql.Sunday)
                for holiday in holidays:
                    cal.addHoliday(qdate(holiday))
                rc = r.Calendar({r.Date(day) for day in holidays})
                dc = r.sofr_ois_conventions(rc)
                fc = r.Conventions()
                fc.calendar = rc
                db, fb = r.CurveBootstrapper(dc), r.CurveBootstrapper(fc)

                def quotes(rates):
                    return [r.MarketQuote(r.Instrument.Deposit if m == 6 else r.Instrument.Swap,
                                           f"{m}M", v.add_months(m), rate)
                            for m, rate in zip(spec["months"], rates)]

                def qbuild(drates, frates):
                    overnight = ql.OvernightIndex("SOFR-matched", 0, ql.USDCurrency(), cal, ql.Actual360())
                    dh = []
                    for m, rate in zip(spec["months"], drates):
                        tenor = ql.Period(m, ql.Months)
                        if m == 6:
                            helper = ql.DepositRateHelper(rate, tenor, 0, cal, ql.ModifiedFollowing, True, ql.Actual360())
                        else:
                            helper = ql.OISRateHelper(0, tenor, rate, overnight, paymentLag=0,
                                paymentConvention=ql.ModifiedFollowing, paymentCalendar=cal,
                                endOfMonth=True, rule=ql.DateGeneration.Forward, fixedDayCount=ql.Actual360())
                        dh.append(helper)
                    dcurve = ql.PiecewiseLogLinearDiscount(qv, dh, ql.Actual365Fixed(),
                                                          ql.IterativeBootstrap(1e-14))
                    dhdl = ql.YieldTermStructureHandle(dcurve)
                    index = ql.IborIndex("forward-3M", ql.Period(3, ql.Months), 0, ql.USDCurrency(), cal,
                                         ql.ModifiedFollowing, True, ql.Actual360())
                    fh = []
                    for m, rate in zip(spec["months"], frates):
                        tenor = ql.Period(m, ql.Months)
                        if m == 6:
                            helper = ql.DepositRateHelper(rate, tenor, 0, cal, ql.ModifiedFollowing, True, ql.Actual360())
                        else:
                            helper = ql.SwapRateHelper(rate, tenor, cal, ql.Semiannual, ql.ModifiedFollowing,
                                ql.Thirty360(ql.Thirty360.European), index, ql.QuoteHandle(),
                                ql.Period(0, ql.Days), dhdl, 0, ql.Pillar.MaturityDate, ql.Date(), True, False,
                                ql.ModifiedFollowing)
                        fh.append(helper)
                    fcurve = ql.PiecewiseLogLinearDiscount(qv, fh, ql.Actual365Fixed(), ql.IterativeBootstrap(1e-14))
                    return dcurve, fcurve, dh, fh

                def qprice(dcurve, fcurve, ois=False):
                    maturity = qv + ql.Period(spec["trade_months"], ql.Months)
                    def schedule(months):
                        return ql.Schedule(qv, maturity, ql.Period(months, ql.Months), cal,
                            ql.ModifiedFollowing, ql.ModifiedFollowing, ql.DateGeneration.Forward, True)
                    if ois:
                        index = ql.OvernightIndex("SOFR-matched", 0, ql.USDCurrency(), cal, ql.Actual360(),
                                                 ql.YieldTermStructureHandle(dcurve))
                        swap = ql.OvernightIndexedSwap(ql.Swap.Payer, spec["notional"], schedule(12),
                            spec["coupon"], ql.Actual360(), index, 0.0, 0, ql.ModifiedFollowing, cal)
                    else:
                        index = ql.IborIndex("forward-3M", ql.Period(3, ql.Months), 0, ql.USDCurrency(), cal,
                            ql.ModifiedFollowing, True, ql.Actual360(), ql.YieldTermStructureHandle(fcurve))
                        swap = ql.VanillaSwap(ql.Swap.Payer, spec["notional"], schedule(6), spec["coupon"],
                            ql.Thirty360(ql.Thirty360.European), schedule(3), index, 0, ql.Actual360())
                    swap.setPricingEngine(ql.DiscountingSwapEngine(ql.YieldTermStructureHandle(dcurve)))
                    return swap.NPV(), swap.fairRate()

                dq, fq = quotes(case["ois_rates"]), quotes(case["forward_rates"])
                d = r.OisCurveBootstrapper(rc).build(v, dq)
                f = fb.build_forward(d.curve, fq)
                qd, qf, dh, fh = qbuild(case["ois_rates"], case["forward_rates"])
                for label, built, external in [("discount", d, qd), ("projection", f, qf)]:
                    for day, df in built.curve.nodes:
                        check(name, f"{label}_df_{day}", external.discount(qdate(day)), df, 5e-10)
                    for row in built.diagnostics:
                        check(name, f"{label}_residual_{row.pillar}", 0, row.residual, 1e-12)
                for label, helpers, rates in [("ois", dh, case["ois_rates"]), ("forward", fh, case["forward_rates"])]:
                    for i, (helper, rate) in enumerate(zip(helpers, rates)):
                        check(name, f"ql_{label}_repricing_{i}", rate, helper.impliedQuote(), 2e-12)
                trade = r.Swap(v, v.add_months(spec["trade_months"]), spec["notional"], spec["coupon"], r.Side.Payer, fc)
                ois = r.Swap(v, v.add_months(spec["trade_months"]), spec["notional"], spec["coupon"], r.Side.Payer, dc)
                value = r.price_multicurve(d.curve, f.curve, trade)
                for label, actual, expected in [("dual", value, qprice(qd, qf)),
                                                ("ois", r.price_ois(d.curve, ois), qprice(qd, qf, True))]:
                    check(name, label + "_pv", expected[0], actual.pv, 2e-5)
                    check(name, label + "_par_rate", expected[1], actual.par_rate, 2e-10)
                # Check coupon cash flows independently, using QL factors and accrual dates.
                for i, flow in enumerate(value.floating_flows):
                    a, b = qdate(flow.period.start), qdate(flow.period.end)
                    expected = spec["notional"] * (qf.discount(a)/qf.discount(b)-1) * qd.discount(b)
                    check(name, f"floating_pv_{i}", expected, flow.pv, 2e-5)
                risk = r.multi_curve_risk(db, fb, v, dq, fq, trade)
                for side, actual_buckets, parallel in [
                        ("discount", risk.discount_quotes, risk.discount_parallel_dv01),
                        ("forward", risk.forwarding_quotes, risk.forwarding_parallel_dv01)]:
                    for bucket in [None] + list(range(len(dq))):
                        pvs = []
                        for shift in [-1e-4, 1e-4]:
                            rates = list(case["ois_rates" if side == "discount" else "forward_rates"])
                            rates = [rate + (shift if bucket is None or i == bucket else 0) for i, rate in enumerate(rates)]
                            sd, sf, _, _ = qbuild(rates if side == "discount" else case["ois_rates"],
                                                 rates if side == "forward" else case["forward_rates"])
                            pvs.append(qprice(sd, sf)[0])
                        actual = parallel if bucket is None else actual_buckets[bucket].dv01
                        check(name, f"{side}_dv01_{bucket}", (pvs[0]-pvs[1])/2, actual, 0.002)
                if name == "upward":
                    example = dict(valuation=spec["valuation"], input="examples/multicurve_cases.json",
                        ois_pv=r.price_ois(d.curve, ois).pv, multi_curve_pv=value.pv, par_rate=value.par_rate,
                        single_curve_pv=r.price(d.curve, trade).pv,
                        discount_quote_dv01=risk.discount_parallel_dv01,
                        forwarding_quote_dv01=risk.forwarding_parallel_dv01)
                    (args.out / "example.json").write_text(json.dumps(example, indent=2)+"\n")
            except Exception as error:
                failures.append(f"{name}: {type(error).__name__}: {error}")
    with (args.out / "comparisons.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    report = dict(comparisons=len(records), passed=sum(r["status"] == "PASS" for r in records),
        quantlib_version=ql.__version__ if ql else None, failures=failures,
        scope="Independent OISRateHelper and SwapRateHelper quote-only bootstraps, pricing and both quote-risk curves",
        skipped=[] if ql else ["Independent OIS/multi-curve QuantLib comparisons"])
    (args.out / "report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
