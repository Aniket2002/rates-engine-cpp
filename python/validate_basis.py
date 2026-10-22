"""Independent QuantLib basis calibration, cash-flow lifecycle and three-coordinate quote risk."""
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
    try:
        import QuantLib as q
    except ImportError:
        (args.out / "report.json").write_text(json.dumps(dict(comparisons=0, passed=0, skipped=True,
            failures=["QuantLib unavailable"] if args.require_quantlib else []))+"\n")
        return int(args.require_quantlib)
    records, failures = [], []

    def check(case, metric, expected, actual, tol):
        ok = math.isfinite(expected) and math.isfinite(actual) and abs(expected-actual) <= tol
        records.append(dict(case=case, metric=metric, reference=expected, cpp=actual,
                            error=abs(expected-actual), tolerance=tol, passed=ok))
        if not ok:
            failures.append(f"{case}/{metric}: {actual} vs {expected}")

    def qd(day):
        d = date.fromisoformat(str(day))
        return q.Date(d.day, d.month, d.year)

    cal = q.UnitedStates(q.UnitedStates.GovernmentBond)
    rc = r.Calendar({r.Date(d.ISO()) for d in cal.holidayList(qd("2024-01-01"), qd("2032-12-31"))})
    def conventions(months=3, lag=2, lookback=0, shift=False, lockout=0):
        c = r.SofrConventions()
        c.calendar = c.payment_calendar = rc
        c.end_of_month = False
        c.payment_months, c.payment_lag_days = months, lag
        c.lookback_days, c.observation_shift, c.lockout_days = lookback, shift, lockout
        return c

    c = conventions()
    builder, basis_builder = r.SofrCurveBootstrapper(c), r.BasisCurveBootstrapper(c, c)
    terms = [3, 12, 24]
    for level in [.03, -.005]:
        for valuation in ["2025-01-15", "2025-02-18", "2026-03-18", "2026-03-19", "2026-03-20"]:
            name = f"level={level}/{valuation}"
            try:
                v = qd(valuation)
                q.Settings.instance().evaluationDate = v
                t = r.Timestamp(valuation+"T16:00:00Z")
                drates = [level+.002, level+.001, level]
                brates = [x+.003 for x in drates]
                spreads = [.0008, -.0002, .0012]
                known = [[], []]
                current = qd("2025-01-02")
                while current < v:
                    if cal.isBusinessDay(current):
                        for i in range(2):
                            known[i].append((current, level+.002*i+(current.serialNumber() % 11)*.0001))
                    current += 1
                fixing_stores = [r.FixingStore([r.FixingRecord(r.Date(d.ISO()), r.Timestamp((d+1).ISO()+"T13:00:00Z"),
                    rate, f"synthetic-index-{i}") for d, rate in rows]) for i, rows in enumerate(known)]
                def index(i, curve=None):
                    idx = q.OvernightIndex(f"basis-validation-{i}", 0, q.USDCurrency(), cal, q.Actual360(),
                        q.YieldTermStructureHandle(curve) if curve else q.YieldTermStructureHandle())
                    idx.clearFixings()
                    for d, rate in known[i]:
                        idx.addFixing(d, rate)
                    return idx
                def ois(rates, discount=None):
                    helpers = [q.OISRateHelper(2, q.Period(m, q.Months), rate, index(0), paymentLag=2,
                        paymentCalendar=cal, paymentConvention=q.Following, paymentFrequency=q.Quarterly,
                        endOfMonth=False, rule=q.DateGeneration.Forward, fixedDayCount=q.Actual360(),
                        discountingCurve=q.YieldTermStructureHandle(discount) if discount else q.YieldTermStructureHandle())
                        for m, rate in zip(terms, rates)]
                    curve = q.PiecewiseLogLinearDiscount(v, helpers, q.Actual365Fixed(), q.IterativeBootstrap(1e-14))
                    curve.discount(helpers[-1].pillarDate())
                    return curve
                def basis(rates, discount, base):
                    helpers = [q.OvernightOvernightBasisSwapRateHelper(q.QuoteHandle(q.SimpleQuote(rate)), q.Period(m,q.Months),
                        2, cal, q.ModifiedFollowing, False, index(0, base), index(1), q.YieldTermStructureHandle(discount),
                        False, 2, q.Quarterly) for m, rate in zip(terms, rates)]
                    curve = q.PiecewiseLogLinearDiscount(v, helpers, q.Actual365Fixed(), q.IterativeBootstrap(1e-14))
                    curve.discount(helpers[-1].pillarDate())
                    return curve, helpers
                def quotes(rates):
                    return [r.SofrQuote(f"{terms[i]}M", terms[i], rates[i]) for i in [2,0,1]]
                oq = [r.BasisQuote(f"{terms[i]}M", terms[i], spreads[i]) for i in [2,0,1]]
                dc = builder.build(t, quotes(drates), fixing_stores[0]).curve
                bc = builder.build_forward(dc, t, quotes(brates), fixing_stores[0]).curve
                built = basis_builder.build(dc, bc, t, oq, *fixing_stores)
                d = ois(drates)
                b = ois(brates, d)
                o, helpers = basis(spreads, d, b)
                for row, helper, spread in zip(built.diagnostics, helpers, spreads):
                    check(name, "pillar", helper.pillarDate().serialNumber(), qd(row.pillar).serialNumber(), 0)
                    check(name, "curve", o.discount(qd(row.pillar)), built.curve.discount(row.pillar), 5e-10)
                    check(name, "repricing", spread, row.model_quote, 1e-12)
                    check(name, "helper_repricing", spread, helper.impliedQuote(), 2e-12)
                start, end = qd("2025-01-31"), qd("2026-03-17")

                def leg(i, curve, conv, spread=0.):
                    schedule = q.Schedule(start, end, q.Period(conv.payment_months,q.Months), cal,
                        q.ModifiedFollowing,q.ModifiedFollowing,q.DateGeneration.Forward,False)
                    return q.OvernightLeg([1e6], schedule, index(i,curve), q.Actual360(), q.Following,
                        spreads=[spread], paymentCalendar=cal, paymentLag=conv.payment_lag_days,
                        lookbackDays=conv.lookback_days, lockoutDays=conv.lockout_days, applyObservationShift=conv.observation_shift)
                def reference(d, b, o, cb, co, include, sign=1):
                    bl, ol = leg(0,b,cb,.001), leg(1,o,co)
                    handle = q.YieldTermStructureHandle(d)
                    pv = sign*(q.CashFlows.npv(ol,handle,include,v,v)-q.CashFlows.npv(bl,handle,include,v,v))
                    return pv, bl, ol
                for advanced in [False,True]:
                    cb, co = (conventions(3,2,2,True,2), conventions(6,3,1,False,0)) if advanced else (c,c)
                    for include in [False,True]:
                        trade = r.BasisSwap(r.Date(start.ISO()),r.Date(end.ISO()),1e6,.001,r.Side.Receiver,cb,co)
                        result = r.price_basis(dc,bc,built.curve,trade,*fixing_stores,t,include)
                        pv, bl, ol = reference(d,b,o,cb,co,include,-1)
                        case = f"{name}/advanced={advanced}/include={include}"
                        check(case,"pv",pv,result.pv,2e-5)
                        accrued = 0.
                        for i,(leg_,cpp,conv) in enumerate([(bl,result.base_leg,cb),(ol,result.other_leg,co)]):
                            outstanding = [cf for cf in leg_ if cf.date()>v or (include and cf.date()==v)]
                            check(case,f"leg{i}/count",len(outstanding),len(cpp.coupons),0)
                            for j,(cf,ours) in enumerate(zip(outstanding,cpp.coupons)):
                                check(case,f"leg{i}/{j}/payment",cf.date().serialNumber(),qd(ours.period.payment).serialNumber(),0)
                                check(case,f"leg{i}/{j}/amount",cf.amount(),ours.floating_amount+(ours.fixed_amount if i==0 else 0),2e-5)
                                coupon = q.as_overnight_indexed_coupon(cf)
                                dates = list(cal.businessDayList(coupon.accrualStartDate(),coupon.accrualEndDate()))
                                growth = 1.
                                idx = index(i,b if i==0 else o)
                                for a,z,f,w in zip(dates,dates[1:],coupon.fixingDates(),coupon.dt()):
                                    if a>=v:
                                        break
                                    growth *= 1+idx.fixing(f)*w*(min(z,v)-a)/(z-a)
                                amount = 1e6*(growth-1)*q.Actual360().yearFraction(dates[0],dates[-1])/sum(coupon.dt())
                                if i==0:
                                    amount += 1e6*.001*q.Actual360().yearFraction(dates[0],max(dates[0],min(v,dates[-1])))
                                accrued += amount*(1 if i==0 else -1) # Receiver.
                        check(case,"accrued",accrued,result.accrued,2e-5)
                        check(case,"clean",pv-accrued,result.clean_pv,3e-5)
                # Full dependency-chain risk on the seasoned/future payer using helper curves.
                if valuation in ["2025-01-15","2025-02-18"]:
                    trade = r.BasisSwap(r.Date(start.ISO()),r.Date(end.ISO()),1e6,.001,r.Side.Payer,c,c)
                    for bump in [.5,1.]:
                        risk = r.basis_quote_risk(builder,builder,basis_builder,quotes(drates),quotes(brates),oq,trade,
                            fixing_stores[0],*fixing_stores,t,bump)
                        check(name,f"risk-base/{bump}",reference(d,b,o,c,c,False)[0],risk.base.pv,2e-5)
                        for coord, buckets, parallel in [(0,risk.discount_quotes,risk.discount_parallel_dv01),
                            (1,risk.base_quotes,risk.base_parallel_dv01),(2,risk.basis_quotes,risk.basis_parallel_dv01)]:
                            def scenario(bucket, sign):
                                dr,br,sr = list(drates),list(brates),list(spreads)
                                changed = [dr,br,sr][coord]
                                for i in range(len(changed)):
                                    if bucket is None or bucket==i:
                                        changed[i]+=sign*bump*1e-4
                                dd=ois(dr) if coord==0 else d
                                bb=ois(br,dd) if coord<2 else b
                                oo=basis(sr,dd,bb)[0]
                                return reference(dd,bb,oo,c,c,False)[0]
                            for i,bucket in enumerate(buckets):
                                check(name,f"risk/{coord}/{bucket.label}/{bump}",(scenario(i,-1)-scenario(i,1))/(2*bump),bucket.dv01,2e-5)
                            check(name,f"risk/{coord}/parallel/{bump}",(scenario(None,-1)-scenario(None,1))/(2*bump),parallel,2e-5)
            except Exception as error:
                failures.append(f"{name}: {type(error).__name__}: {error}")
    if records:
        with (args.out/"comparisons.csv").open("w",newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(records[0]))
            writer.writeheader()
            writer.writerows(records)
    report=dict(comparisons=len(records),passed=sum(x["passed"] for x in records),failures=failures,
        quantlib_version=q.__version__,synthetic_quotes=True,
        scope="Independent overnight/overnight basis helpers; distinct fixing histories; seasoned/stub/observation conventions; three-coordinate recalibrated risk")
    (args.out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({**report,"failures":failures[:15]},indent=2))
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
