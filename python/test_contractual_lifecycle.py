"""Independent cash-accounting checks and JSON event replay for the generic lifecycle."""
import argparse
from datetime import date, timedelta
from decimal import Decimal, localcontext
import json
from pathlib import Path
import sys

from run_basis_lifecycle import run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir",required=True,type=Path)
    parser.add_argument("--out",required=True,type=Path)
    args=parser.parse_args()
    sys.path.insert(0,str(args.module_dir.resolve()))
    import pyrates as r
    # Round-trip a persisted report, not live C++ event objects.
    report=json.loads(json.dumps(run(args.module_dir)))
    checks=[]
    def check(name, expected, actual, tol=1e-8):
        if abs(expected-actual)>tol:
            raise AssertionError(f"{name}: {actual} vs {expected}")
        checks.append(dict(name=name,expected=expected,actual=actual,tolerance=tol))
    def previous(day,n):
        for _ in range(n):
            day-=timedelta(days=1)
            while day.weekday()>=5:
                day-=timedelta(days=1)
        return day
    start,end=date(2025,1,2),date(2025,2,3)
    # Independent Decimal compounding from synthetic observation definitions;
    # no engine factors, coupons, or reported payment amounts enter the calculation.
    with localcontext() as context:
        context.prec=40
        amounts=[]
        for index,lookback,shift in [(0,2,True),(1,1,False)]:
            growth=Decimal(1)
            total_weight=Decimal(0)
            a=start
            while a<end:
                b=a+timedelta(days=1)
                while b.weekday()>=5:
                    b+=timedelta(days=1)
                fixing=previous(a,lookback)
                obs_end=previous(b,lookback)
                weight=Decimal((obs_end-fixing).days if shift else (b-a).days)/360
                rate=Decimal('.03')+Decimal('.0013')*index+Decimal('.0001')*(fixing.toordinal()%7)
                growth*=1+rate*weight
                total_weight+=weight
                a=b
            amounts.append(Decimal(1000000)*(growth-1)*Decimal((end-start).days)/360/total_weight)
        expected=float(amounts[1]-amounts[0]-Decimal(1000000)*Decimal('.0004')*Decimal((end-start).days)/360)
        check("settled basis cash / independent decimal compounding",expected,report["clearing_cash"])
        calls={call["id"]:Decimal(str(call["amount"])) for call in report["margin_calls"]}
        balance,accrued,paid=Decimal(0),Decimal(0),Decimal(0)
        through=date(2025,1,15)
        for event in report["collateral_events"]:
            kind=event["kind"].split(".")[-1]
            day=date.fromisoformat(event["at"][:10])
            if kind=="AccrueInterest":
                accrued-=balance*Decimal(str(event["value"]))*Decimal((day-through).days)/360
                through=day
            elif kind=="SettleTransfer":
                balance+=calls[event["reference"]]
            elif kind=="PayInterest":
                paid+=accrued
                accrued=Decimal(0)
        check("principal cash conservation",0.,float(balance),0.)
        check("actual settled cash interest integral",float(paid),report["interest_cash"],1e-12)
        check("no unpaid interest at close",0.,float(accrued),0.)
    ledger=r.ClearingLedger()
    for e in report["clearing_events"]:
        event=r.ClearingEvent(e["id"],r.Timestamp(e["at"]),getattr(r.ClearingEventKind,e["kind"].split(".")[-1]),
            e["trade"],e["reference"],e["amount"],r.Date(e["due"]) if e["due"] else None)
        if not ledger.apply(event) or ledger.apply(event):
            raise AssertionError("Clearing event idempotency failure")
    check("persisted clearing replay cash",report["clearing_cash"],ledger.settled_cash)
    agreement=r.CollateralAgreement()
    agreement.minimum_transfer,agreement.rounding=10.,1.
    account=r.CollateralAccount(r.Date("2025-01-15"),agreement)
    for e in report["collateral_events"]:
        event=r.CollateralEvent(e["id"],r.Timestamp(e["at"]),getattr(r.CollateralEventKind,e["kind"].split(".")[-1]),e["reference"],e["value"])
        if not account.apply(event) or account.apply(event):
            raise AssertionError("Collateral event idempotency failure")
    check("persisted collateral replay principal",0.,account.balance,0.)
    check("persisted collateral replay interest",report["interest_cash"],account.settled_interest,0.)
    check("persisted collateral closure",1,int(account.closed),0.)
    # An independent hand-calculated call fixture: thresholds, pending offsets,
    # a sign reversal, cancellation, ACT/365F and negative remuneration.
    agreement=r.CollateralAgreement()
    agreement.receive_threshold,agreement.post_threshold=100.,200.
    agreement.minimum_transfer,agreement.rounding=50.,25.
    agreement.settlement_days=0
    agreement.interest_day_count=r.DayCount.ACT365F
    a=r.CollateralAccount(r.Date("2025-01-02"),agreement)
    def event(id,kind,value=0.,reference="",day="2025-01-02"):
        return r.CollateralEvent(id,r.Timestamp(day+"T16:00:00Z"),kind,reference,value)
    a.apply(event("first",r.CollateralEventKind.Mark,1001.))
    check("rounded receipt above threshold",925.,a.pending,0.)
    a.apply(event("repeat",r.CollateralEventKind.Mark,1001.))
    check("pending receipt avoids duplicate call",925.,a.pending,0.)
    a.apply(event("flip",r.CollateralEventKind.Mark,-450.))
    check("sign reversal offsets pending receipts",-250.,a.pending,0.)
    a.apply(event("cancel-first",r.CollateralEventKind.CancelTransfer,reference="first"))
    a.apply(event("re-mark",r.CollateralEventKind.Mark,-450.))
    check("cancelled call is replaced on next mark",-250.,a.pending,0.)
    for ref in ["flip","re-mark"]:
        a.apply(event("settle-"+ref,r.CollateralEventKind.SettleTransfer,reference=ref))
    check("net settled posting",-250.,a.balance,0.)
    a.apply(event("interest",r.CollateralEventKind.AccrueInterest,-.0365,day="2025-01-03"))
    check("negative interest on posted collateral",-.025,a.accrued_interest,1e-14)
    result=dict(passed=True,checks=checks,clearing_replayed_events=len(report["clearing_events"]),
        collateral_replayed_events=len(report["collateral_events"]),synthetic_quotes=True,synthetic_fixings=True,
        scope="Independent Decimal coupon cash and settled-principal interest; persisted idempotent replay; hand-calculated margin-call fixtures")
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/"report.json").write_text(json.dumps(result,indent=2)+"\n")
    print(f"Contractual lifecycle: {len(checks)} checks passed; journals replayed from JSON")


if __name__ == "__main__":
    main()
