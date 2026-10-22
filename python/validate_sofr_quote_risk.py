"""Independent helper recalibration for two-curve SOFR lifecycle quote sensitivities.

Invoked by validate_sofr.py; no engine discount factors enter the reference model.
"""
from datetime import date


def validate(r, q, check):
    def qd(day):
        d = date.fromisoformat(str(day))
        return q.Date(d.day, d.month, d.year)

    cal = q.UnitedStates(q.UnitedStates.GovernmentBond)
    rc = r.Calendar({r.Date(d.ISO()) for d in cal.holidayList(qd("2024-01-01"), qd("2030-12-31"))})
    # Distinct curve conventions, nonannual payments, negative rates, lookback,
    # shift and lockout; a short final trade stub spans a holiday weekend.
    for lookback, shift, lockout, level in [(0, False, 0, .03), (2, True, 2, -.005)]:
        for valuation in ["2025-01-15", "2025-02-18", "2025-05-28", "2025-05-29", "2025-05-30"]:
            v = qd(valuation)
            q.Settings.instance().evaluationDate = v
            ts = r.Timestamp(valuation+"T16:00:00Z")
            c = r.SofrConventions()
            c.calendar = c.payment_calendar = rc
            c.end_of_month = False
            c.lookback_days, c.observation_shift, c.lockout_days = lookback, shift, lockout
            pc = r.SofrConventions()
            pc.calendar = pc.payment_calendar = rc
            pc.end_of_month = False
            pc.lookback_days, pc.observation_shift, pc.lockout_days = lookback, shift, lockout
            pc.payment_months = 3
            db, pb = r.SofrCurveBootstrapper(c), r.SofrCurveBootstrapper(pc)
            terms = [1, 6, 18]
            drates = [level+.002, level+.001, level]
            prates = [rate+.004 for rate in drates]
            # Intentionally unsorted: labels/pillars must still align after calibration.
            def quotes(rates):
                return [r.SofrQuote(f"{terms[i]}M", terms[i], rates[i]) for i in [2, 0, 1]]

            known = []
            day = qd("2024-12-20")
            while day < v:
                if cal.isBusinessDay(day):
                    known.append((day, level+.003+(day.serialNumber() % 7)*.0001))
                day += 1
            fixings = r.FixingStore([r.FixingRecord(r.Date(d.ISO()),
                r.Timestamp(cal.advance(d, 1, q.Days).ISO()+"T13:00:00Z"), rate, "synthetic-validation") for d, rate in known])
            index_name = "SOFR-two-curve-risk-validation"

            def index(curve=None):
                idx = q.OvernightIndex(index_name, 0, q.USDCurrency(), cal, q.Actual360(),
                    q.YieldTermStructureHandle(curve) if curve else q.YieldTermStructureHandle())
                idx.clearFixings()
                for d, rate in known:
                    idx.addFixing(d, rate)
                return idx

            def build(rates, discount=None):
                helpers = [q.OISRateHelper(2, q.Period(m, q.Months), rate, index(), paymentLag=2,
                    paymentCalendar=cal, paymentConvention=q.Following, endOfMonth=False,
                    paymentFrequency=q.Quarterly if discount else q.Annual,
                    rule=q.DateGeneration.Forward, fixedDayCount=q.Actual360(),
                    lookbackDays=lookback, lockoutDays=lockout, applyObservationShift=shift,
                    discountingCurve=q.YieldTermStructureHandle(discount) if discount else q.YieldTermStructureHandle())
                    for m, rate in zip(terms, rates)]
                curve = q.PiecewiseLogLinearDiscount(v, helpers, q.Actual365Fixed(), q.IterativeBootstrap(1e-14))
                curve.discount(helpers[-1].pillarDate())
                return curve, helpers

            dc, dh = build(drates)
            projection, ph = build(prates, dc)
            start, end = qd("2025-01-31"), qd("2025-05-27")
            schedule = q.Schedule(start, end, q.Period(3, q.Months), cal, q.ModifiedFollowing,
                q.ModifiedFollowing, q.DateGeneration.Forward, False)
            trade = r.SofrSwap(r.Date(start.ISO()), r.Date(end.ISO()), 1e6, .025, r.Side.Receiver, pc)

            def price(d, p, include):
                swap = q.OvernightIndexedSwap(q.Swap.Receiver, 1e6, schedule, .025, q.Actual360(),
                    index(p), 0., 2, q.Following, cal, False, q.RateAveraging.Compound, lookback, lockout, shift)
                handle = q.YieldTermStructureHandle(d)
                return (q.CashFlows.npv(swap.fixedLeg(), handle, include, v, v)
                        - q.CashFlows.npv(swap.overnightLeg(), handle, include, v, v))

            for include in ([False, True] if valuation == "2025-05-29" else [False]):
                for bump in [0.5, 2.0]:
                    name = f"dual-quote/{valuation}/lookback={lookback}/lockout={lockout}/include={include}/bump={bump}"
                    result = r.sofr_multi_curve_quote_risk(db, pb, quotes(drates), quotes(prates), trade, fixings, ts, bump, include)
                    check(name, "base_pv", price(dc, projection, include), result.base.pv, 2e-5)
                    for is_discount, buckets, parallel, helpers in [
                        (True, result.discount_quotes, result.discount_parallel_dv01, dh),
                        (False, result.projection_quotes, result.projection_parallel_dv01, ph)]:
                        prefix = "discount" if is_discount else "projection"
                        def scenario(bucket, sign):
                            changed = list(drates if is_discount else prates)
                            for i in range(len(changed)):
                                if bucket is None or bucket == i:
                                    changed[i] += sign*bump*1e-4
                            d = build(changed)[0] if is_discount else dc
                            p = build(prates if is_discount else changed, d)[0]
                            return price(d, p, include)
                        for i, (bucket, helper) in enumerate(zip(buckets, helpers)):
                            check(name, f"{prefix}/pillar/{bucket.label}", helper.pillarDate().serialNumber(), qd(bucket.maturity).serialNumber(), 0)
                            expected = (scenario(i, -1)-scenario(i, 1))/(2*bump)
                            check(name, f"{prefix}/{bucket.label}", expected, bucket.dv01, 2e-5)
                        check(name, prefix+"/parallel", (scenario(None, -1)-scenario(None, 1))/(2*bump), parallel, 2e-5)
