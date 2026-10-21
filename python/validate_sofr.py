"""Independent QuantLib validation of SOFR lifecycle cash flows and lag-aware calibration."""
import argparse
import csv
from datetime import date, timedelta
import json
import math
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    sys.path.insert(0, str(args.module_dir.resolve()))
    import pyrates as r
    args.out.mkdir(parents=True, exist_ok=True)
    records, failures = [], []
    try:
        import QuantLib as q
    except ImportError:
        report = dict(passed=0, comparisons=0, quantlib_version=None,
                      failures=["QuantLib unavailable"] if args.require_quantlib else [], skipped=["SOFR lifecycle validation"])
        (args.out / "report.json").write_text(json.dumps(report, indent=2)+"\n")
        return int(args.require_quantlib)

    def qd(d):
        d = date.fromisoformat(str(d))
        return q.Date(d.day, d.month, d.year)

    def iso(d):
        return d.ISO()

    def check(case, metric, expected, actual, tol):
        error = abs(expected-actual)
        ok = math.isfinite(expected) and math.isfinite(actual) and error <= tol
        records.append(dict(case=case, metric=metric, reference=expected, cpp=actual,
                            absolute_error=error, tolerance=tol, status="PASS" if ok else "FAIL"))
        if not ok:
            failures.append(f"{case}/{metric}: {actual} vs {expected} (tol {tol})")

    cal = q.UnitedStates(q.UnitedStates.GovernmentBond)
    holidays = cal.holidayList(q.Date(1, 1, 2024), q.Date(31, 12, 2030))
    rc = r.Calendar({r.Date(iso(d)) for d in holidays})
    start, end = q.Date(2, 1, 2025), q.Date(2, 1, 2026)
    for valuation in ["2025-01-02", "2025-01-15", "2025-07-03", "2026-01-05", "2026-01-06", "2026-01-07"]:
        v = qd(valuation)
        q.Settings.instance().evaluationDate = v
        for lookback, shift, lockout in [(0, False, 0), (2, False, 0), (2, True, 0), (2, True, 2)]:
            for include_today in ([False, True] if valuation == "2026-01-06" else [False]):
                name = f"{valuation}/lookback={lookback}/shift={shift}/lockout={lockout}/include={include_today}"
                try:
                    c = r.SofrConventions()
                    c.calendar = c.payment_calendar = rc
                    c.lookback_days, c.observation_shift, c.lockout_days = lookback, shift, lockout
                    # Quarterly payments exercise settled coupons within a seasoned trade.
                    c.payment_months = 3
                    dcurve = q.FlatForward(v, 0.025, q.Actual365Fixed())
                    fcurve = q.FlatForward(v, 0.035, q.Actual365Fixed())
                    last = q.Date(2, 1, 2028)
                    rd = r.DiscountCurve(r.Date(valuation), [(r.Date(iso(last)), dcurve.discount(last))])
                    rf = r.DiscountCurve(r.Date(valuation), [(r.Date(iso(last)), fcurve.discount(last))])
                    index = q.OvernightIndex("SOFR-lifecycle-validation", 0, q.USDCurrency(), cal,
                                              q.Actual360(), q.YieldTermStructureHandle(fcurve))
                    index.clearFixings()
                    fixing_records = []
                    current = cal.advance(start, -10, q.Days)
                    while current < v:
                        rate = 0.03 + (current.serialNumber() % 17)*0.0001
                        index.addFixing(current, rate)
                        available = iso(cal.advance(current, 1, q.Days))+"T13:00:00Z"
                        fixing_records.append(r.FixingRecord(r.Date(iso(current)), r.Timestamp(available), rate, "synthetic-validation"))
                        current = cal.advance(current, 1, q.Days)
                    fixing_store = r.FixingStore(fixing_records)
                    swap = r.SofrSwap(r.Date(iso(start)), r.Date(iso(end)), 1e6, 0.04, r.Side.Payer, c)
                    value = r.price_sofr(rd, rf, swap, fixing_store, r.Timestamp(valuation+"T16:00:00Z"), include_today)
                    schedule = q.Schedule(start, end, q.Period(3, q.Months), cal, q.ModifiedFollowing,
                                          q.ModifiedFollowing, q.DateGeneration.Forward, True)
                    external = q.OvernightIndexedSwap(q.Swap.Payer, 1e6, schedule, 0.04, q.Actual360(),
                        index, 0.0, 2, q.Following, cal, False, q.RateAveraging.Compound, lookback, lockout, shift)
                    handle = q.YieldTermStructureHandle(dcurve)
                    fixed = q.CashFlows.npv(external.fixedLeg(), handle, include_today, v, v)
                    floating = q.CashFlows.npv(external.overnightLeg(), handle, include_today, v, v)
                    check(name, "fixed_pv", fixed, value.fixed_pv, 2e-7)
                    check(name, "floating_pv", floating, value.floating_pv, 2e-7)
                    check(name, "dirty_pv", floating-fixed, value.pv, 2e-7)
                    outstanding = [cf for cf in external.overnightLeg() if cf.date() > v or (include_today and cf.date() == v)]
                    check(name, "outstanding_count", len(outstanding), len(value.coupons), 0)
                    fixed_coupons = [cf for cf in external.fixedLeg() if cf.date() > v or (include_today and cf.date() == v)]
                    for i, (cf, ff, cpp) in enumerate(zip(outstanding, fixed_coupons, value.coupons)):
                        coupon = q.as_overnight_indexed_coupon(cf)
                        check(name, f"payment_{i}", cf.date().serialNumber(), qd(cpp.period.payment).serialNumber(), 0)
                        check(name, f"amount_{i}", cf.amount(), cpp.floating_amount, 2e-7)
                        check(name, f"fixed_accrued_{i}", q.as_coupon(ff).accruedAmount(v), cpp.fixed_accrued, 2e-7)
                        # Daily weights and fixing dates independently generated by QuantLib.
                        check(name, f"observation_count_{i}", len(coupon.fixingDates()), len(cpp.observations), 0)
                        for j, (fixing_date, weight, observation) in enumerate(zip(coupon.fixingDates(), coupon.dt(), cpp.observations)):
                            check(name, f"fixing_{i}_{j}", fixing_date.serialNumber(), qd(observation.fixing_date).serialNumber(), 0)
                            check(name, f"weight_{i}_{j}", weight, observation.weight, 1e-15)
                    # The project defines accrued as partial compounded interest in each unpaid
                    # coupon, prorating an observation weight inside its original accrual interval.
                    # Check this definition independently instead of silently adopting a vendor's
                    # shifted-accrual reporting convention.
                    accrual = 0.0
                    for cf, ff in zip(outstanding, fixed_coupons):
                        coupon = q.as_overnight_indexed_coupon(cf)
                        product = 1.0
                        interest_dates = list(cal.businessDayList(coupon.accrualStartDate(), coupon.accrualEndDate()))
                        for a, b, fixing_date, weight in zip(interest_dates, interest_dates[1:], coupon.fixingDates(), coupon.dt()):
                            if a >= v:
                                break
                            fraction = (min(b, v)-a)/(b-a)
                            product *= 1 + index.fixing(fixing_date)*weight*fraction
                        normalization = q.Actual360().yearFraction(coupon.accrualStartDate(), coupon.accrualEndDate()) / sum(coupon.dt())
                        accrual += 1e6*normalization*(product-1)-q.as_coupon(ff).accruedAmount(v)
                    check(name, "accrued", accrual, value.accrued, 2e-7)
                    check(name, "clean_pv", floating-fixed-accrual, value.clean_pv, 3e-7)
                except Exception as error:
                    failures.append(f"{name}: {type(error).__name__}: {error}")

    # Independent OIS helper bootstrap includes both T+2 spot and payment lag.
    v = q.Date(2, 1, 2025)
    q.Settings.instance().evaluationDate = v
    for lookback, shift in [(0, False), (2, False), (2, True)]:
        name = f"bootstrap/lookback={lookback}/shift={shift}"
        try:
            c = r.SofrConventions()
            c.calendar = c.payment_calendar = rc
            c.lookback_days, c.observation_shift = lookback, shift
            c.end_of_month = False
            terms = [(1, 0.04), (3, 0.039), (12, 0.037), (24, 0.035), (60, 0.032)]
            quotes = [r.SofrQuote(f"{m}M", m, rate) for m, rate in terms]
            built = r.SofrCurveBootstrapper(c).build(r.Timestamp(iso(v)+"T16:00:00Z"), quotes)
            index = q.OvernightIndex("SOFR-helper-validation", 0, q.USDCurrency(), cal, q.Actual360())
            helpers = [q.OISRateHelper(2, q.Period(m, q.Months), rate, index, paymentLag=2,
                paymentConvention=q.Following, paymentCalendar=cal, endOfMonth=False,
                lookbackDays=lookback, applyObservationShift=shift, rule=q.DateGeneration.Forward,
                fixedDayCount=q.Actual360()) for m, rate in terms]
            external = q.PiecewiseLogLinearDiscount(v, helpers, q.Actual365Fixed(), q.IterativeBootstrap(1e-14))
            for row, helper, (_, rate) in zip(built.diagnostics, helpers, terms):
                check(name, "pillar", helper.pillarDate().serialNumber(), qd(row.pillar).serialNumber(), 0)
                check(name, "discount", external.discount(qd(row.pillar)), built.curve.discount(row.pillar), 5e-10)
                check(name, "repricing", rate, row.model_quote, 1e-12)
                check(name, "external_repricing", rate, helper.impliedQuote(), 2e-12)
        except Exception as error:
            failures.append(f"{name}: {type(error).__name__}: {error}")
    if records:
        with (args.out / "comparisons.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
    report = dict(comparisons=len(records), passed=sum(row["status"] == "PASS" for row in records),
                  quantlib_version=q.__version__, failures=failures,
                  scope="Independent SOFR lifecycle coupons, observation dates/weights, accrued definition and lag-aware OIS helpers")
    (args.out / "report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({**report, 'failures': failures[:20]}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
