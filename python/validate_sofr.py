"""Independent QuantLib validation of SOFR lifecycle cash flows and lag-aware calibration."""
import argparse
import csv
from datetime import date
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

    # Cross convention combinations with future, seasoned, weekend, unpaid and settled states.
    cases = [
        ("regular", "2025-01-02", "2026-01-02", 3, True, 2, "government", "same", "ACT360", "ModifiedFollowing"),
        ("leap-eom-stub", "2024-02-29", "2025-03-17", 3, True, 3, "government", "custom", "ACT365F", "ModifiedFollowing"),
        ("year-end-stub", "2024-12-31", "2025-08-17", 2, False, 0, "weekends", "same", "ThirtyE360", "Following"),
        ("holiday-stub", "2025-04-17", "2025-11-29", 3, False, 4, "government", "custom", "ACT360", "ModifiedFollowing"),
        ("short-final-stub", "2025-01-31", "2025-05-12", 3, True, 2, "weekends", "custom", "ACT360", "Following"),
    ]
    for case, first, final, months, eom, lag, calendar_name, payment_name, dc_name, adjustment_name in cases:
        cal = q.UnitedStates(q.UnitedStates.GovernmentBond) if calendar_name == "government" else q.WeekendsOnly()
        paycal = q.BespokeCalendar("SOFR-validation-payment")
        paycal.addWeekend(q.Saturday)
        paycal.addWeekend(q.Sunday)
        for holiday in cal.holidayList(q.Date(1, 1, 2023), q.Date(31, 12, 2030)):
            paycal.addHoliday(holiday)
        if payment_name == "custom":
            for holiday in ["2025-03-18", "2025-05-13", "2025-12-01", "2025-12-02"]:
                paycal.addHoliday(qd(holiday))
        rc = r.Calendar({r.Date(iso(d)) for d in cal.holidayList(q.Date(1, 1, 2023), q.Date(31, 12, 2030))})
        rpc = r.Calendar({r.Date(iso(d)) for d in paycal.holidayList(q.Date(1, 1, 2023), q.Date(31, 12, 2030))})
        start, end = qd(first), qd(final)
        adjustment = getattr(q, adjustment_name)
        fixed_dc = {"ACT360": q.Actual360(), "ACT365F": q.Actual365Fixed(), "ThirtyE360": q.Thirty360(q.Thirty360.European)}[dc_name]
        payment = paycal.advance(cal.adjust(end, adjustment), lag, q.Days)
        valuations = sorted({iso(cal.advance(start, -3, q.Days)), iso(cal.advance(start, 9, q.Days)),
                             iso(start+23), iso(cal.adjust(end, adjustment)), iso(payment), iso(payment+1)})
        if case == "regular":
            valuations = sorted(set(valuations) | {"2025-01-02", "2025-01-15", "2025-07-03", "2026-01-05", "2026-01-06", "2026-01-07"})
        for valuation in valuations:
            v = qd(valuation)
            q.Settings.instance().evaluationDate = v
            for lookback, shift, lockout in [(0, False, 0), (2, False, 0), (2, True, 0), (2, True, 2)]:
                for include_today in ([False, True] if qd(valuation) == payment else [False]):
                    name = f"{case}/{valuation}/lookback={lookback}/shift={shift}/lockout={lockout}/include={include_today}"
                    try:
                        c = r.SofrConventions()
                        c.calendar, c.payment_calendar = rc, rpc
                        c.end_of_month = eom
                        c.payment_lag_days = lag
                        c.fixed_day_count = getattr(r.DayCount, dc_name)
                        c.adjustment = getattr(r.BusinessDay, adjustment_name)
                        c.lookback_days, c.observation_shift, c.lockout_days = lookback, shift, lockout
                        # Multiple payments exercise settled coupons within a seasoned trade.
                        c.payment_months = months
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
                            # Synthetic availability permits weekend accrued checks; this is not NY Fed publication timing.
                            available = iso(current+1)+"T13:00:00Z"
                            fixing_records.append(r.FixingRecord(r.Date(iso(current)), r.Timestamp(available), rate, "synthetic-validation"))
                            current = cal.advance(current, 1, q.Days)
                        fixing_store = r.FixingStore(fixing_records)
                        swap = r.SofrSwap(r.Date(iso(start)), r.Date(iso(end)), 1e6, 0.04, r.Side.Payer, c)
                        value = r.price_sofr(rd, rf, swap, fixing_store, r.Timestamp(valuation+"T16:00:00Z"), include_today)
                        schedule = q.Schedule(start, end, q.Period(months, q.Months), cal, adjustment,
                                              adjustment, q.DateGeneration.Forward, eom)
                        external = q.OvernightIndexedSwap(q.Swap.Payer, 1e6, schedule, 0.04, fixed_dc,
                            index, 0.0, lag, q.Following, paycal, False, q.RateAveraging.Compound, lookback, lockout, shift)
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
                            check(name, f"start_{i}", coupon.accrualStartDate().serialNumber(), qd(cpp.period.start).serialNumber(), 0)
                            check(name, f"end_{i}", coupon.accrualEndDate().serialNumber(), qd(cpp.period.end).serialNumber(), 0)
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
    cal = q.UnitedStates(q.UnitedStates.GovernmentBond)
    rc = r.Calendar({r.Date(iso(d)) for d in cal.holidayList(q.Date(1, 1, 2024), q.Date(31, 12, 2030))})
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
    from validate_sofr_quote_risk import validate
    try:
        validate(r, q, check)
    except Exception as error:
        failures.append(f"dual-quote-risk: {type(error).__name__}: {error}")
    if records:
        with (args.out / "comparisons.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
    report = dict(comparisons=len(records), passed=sum(row["status"] == "PASS" for row in records),
                  quantlib_version=q.__version__, failures=failures,
                  scope="Independent SOFR lifecycle, stub/calendar combinations, accrued, OIS helpers and full two-curve quote risk")
    # Compact review artifacts; comparisons.csv retains every individual observation check.
    summaries = {}
    for row in records:
        summary = summaries.setdefault(row["case"], dict(case=row["case"], comparisons=0, failures=0, max_tolerance_fraction=0.0))
        summary["comparisons"] += 1
        summary["failures"] += row["status"] != "PASS"
        if row["tolerance"]:
            summary["max_tolerance_fraction"] = max(summary["max_tolerance_fraction"], row["absolute_error"]/row["tolerance"])
    for filename, rows in [("case-summary.csv", list(summaries.values())),
                           ("quote-risk-comparisons.csv", [row for row in records if row["case"].startswith("dual-quote/")])]:
        if rows:
            with (args.out / filename).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
    (args.out / "report.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps({**report, 'failures': failures[:20]}, indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
