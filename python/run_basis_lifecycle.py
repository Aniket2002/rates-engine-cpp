"""Synthetic end-to-end basis pricing, clearing and cash-collateral lifecycle demonstration.

All quotes and fixings are synthetic. Calendars exclude weekends only. One USD
account pays the base overnight index plus spread and receives a second index.
"""
import argparse
from datetime import date, timedelta
import json
from pathlib import Path
import sys


def run(module_dir):
    sys.path.insert(0, str(Path(module_dir).resolve()))
    import pyrates as r
    def timestamp(day):
        return r.Timestamp(day.isoformat()+"T16:00:00Z")
    def convention(lag=2, lookback=0, shift=False):
        c = r.SofrConventions()
        c.end_of_month = False
        c.payment_months = 3
        c.payment_lag_days = lag
        c.lookback_days, c.observation_shift = lookback, shift
        return c
    c = convention()
    bc, oc = convention(2,2,True), convention(3,1,False)
    db, bb = r.SofrCurveBootstrapper(c), r.BasisCurveBootstrapper(c,c)
    trade = r.BasisSwap(r.Date("2025-01-02"),r.Date("2025-02-03"),1e6,.0004,r.Side.Payer,bc,oc)
    terms = [3,12,24]
    base_quotes = [r.SofrQuote(f"{m}M",m,.031-.0005*i) for i,m in enumerate(terms)]
    spreads = [r.BasisQuote(f"{m}M",m,.001+.0002*i) for i,m in enumerate(terms)]
    ledger = r.ClearingLedger()
    agreement = r.CollateralAgreement()
    agreement.minimum_transfer = 10.
    agreement.rounding = 1.
    account = r.CollateralAccount(r.Date("2025-01-15"),agreement)
    start = date(2025,1,15)
    def clear(id, day, kind, reference="", amount=0., due=None):
        ledger.apply(r.ClearingEvent(id,timestamp(day),kind,"basis-1",reference,amount,r.Date(due) if due else None))
    def collateral(id, day, kind, reference="", value=0.):
        account.apply(r.CollateralEvent(id,timestamp(day),kind,reference,value))
    clear("submit",start,r.ClearingEventKind.Submit)
    clear("accept",start,r.ClearingEventKind.Accept,due="2025-02-06")
    rows, first_risk = [], None
    day = start
    while day < date(2025,2,15):
        if day.weekday() >= 5:
            day += timedelta(days=1)
            continue
        key = day.isoformat()
        if day > start:
            collateral("interest-"+key,day,r.CollateralEventKind.AccrueInterest,value=.025)
        for id, call in account.transfers.items():
            if not call.settled and not call.cancelled and str(call.due) <= key:
                collateral("settle-"+id,day,r.CollateralEventKind.SettleTransfer,reference=id)
        collateral("pay-interest-"+key,day,r.CollateralEventKind.PayInterest)
        active = ledger.trades["basis-1"].state == r.ClearingState.Accepted
        value = None
        if active:
            # Each date rebuilds curves from explicitly synthetic inputs at that date.
            t = timestamp(day)
            quotes = [r.SofrQuote(f"{m}M",m,.029-.0005*i+(day-start).days*.00001) for i,m in enumerate(terms)]
            histories = [[],[]]
            fixing_day = date(2024,12,20)
            while fixing_day < day:
                if fixing_day.weekday() < 5:
                    for index in range(2):
                        histories[index].append(r.FixingRecord(r.Date(fixing_day.isoformat()),
                            r.Timestamp((fixing_day+timedelta(days=1)).isoformat()+"T13:00:00Z"),
                            .03+.0013*index+(fixing_day.toordinal()%7)*.0001,f"synthetic-index-{index}"))
                fixing_day += timedelta(days=1)
            bf,of = [r.FixingStore(records) for records in histories]
            discount = db.build(t,quotes).curve
            base = db.build_forward(discount,t,base_quotes,bf).curve
            other = bb.build(discount,base,t,spreads,bf,of).curve
            due_value = r.price_basis(discount,base,other,trade,bf,of,t,True)
            for label,leg,sign in [("base",due_value.base_leg,-1),("other",due_value.other_leg,1)]:
                for cf in leg.coupons:
                    if str(cf.period.payment) == key:
                        if not all(obs.realized for obs in cf.observations):
                            raise AssertionError("Cannot settle a coupon containing forecasts")
                        payment_id = label+"-"+key
                        amount = sign*(cf.floating_amount+(cf.fixed_amount if label=="base" else 0.))
                        clear("register-"+payment_id,day,r.ClearingEventKind.RegisterPayment,payment_id,amount,key)
                        clear("pay-"+payment_id,day,r.ClearingEventKind.SettlePayment,payment_id)
            value = r.price_basis(discount,base,other,trade,bf,of,t)
            exposure = ledger.net_exposure({"basis-1":value.pv})
            if first_risk is None:
                risk = r.basis_quote_risk(db,db,bb,quotes,base_quotes,spreads,trade,r.FixingStore(),bf,of,t)
                first_risk = dict(discount_parallel_dv01=risk.discount_parallel_dv01,
                    base_parallel_dv01=risk.base_parallel_dv01,basis_parallel_dv01=risk.basis_parallel_dv01)
            if key >= "2025-02-06":
                clear("close",day,r.ClearingEventKind.Close)
                exposure = ledger.net_exposure({})
        else:
            exposure = ledger.net_exposure({})
        collateral("mark-"+key,day,r.CollateralEventKind.Mark,value=exposure)
        unresolved = any(not call.settled and not call.cancelled for call in account.transfers.values())
        if ledger.trades["basis-1"].state == r.ClearingState.Closed and not unresolved:
            if account.balance != 0:
                collateral("final-return-"+key,day,r.CollateralEventKind.ReturnCollateral)
            else:
                collateral("close",day,r.CollateralEventKind.Close)
        rows.append(dict(date=key,dirty_pv=exposure,clearing_cash=ledger.settled_cash,
            collateral_principal=account.balance,pending_transfer=account.pending,
            settled_interest=account.settled_interest,unsecured_exposure=account.unsecured_exposure,
            realized_observations=sum(obs.realized for leg in [value.base_leg,value.other_leg] for cf in leg.coupons for obs in cf.observations) if value else 0,
            forecast_observations=sum(not obs.realized for leg in [value.base_leg,value.other_leg] for cf in leg.coupons for obs in cf.observations) if value else 0))
        if account.closed:
            break
        day += timedelta(days=1)
    if not account.closed:
        raise AssertionError("Example failed to close its collateral account")
    # Replay both journals into empty state and verify cash conservation.
    replay = r.ClearingLedger()
    for event in ledger.journal:
        replay.apply(event)
    collateral_replay = r.CollateralAccount(r.Date("2025-01-15"),agreement)
    for event in account.journal:
        collateral_replay.apply(event)
    assert replay.settled_cash == ledger.settled_cash
    assert collateral_replay.balance == account.balance == 0
    assert collateral_replay.settled_interest == account.settled_interest
    return dict(synthetic_quotes=True,synthetic_fixings=True,currency="USD",calendar="weekends only",
        contract="generic collateralized-to-market cash collateral; no CCP connection",
        daily=rows,initial_quote_risk=first_risk,final_clearing_state=str(ledger.trades["basis-1"].state),
        collateral_closed=account.closed,clearing_cash=ledger.settled_cash,interest_cash=account.settled_interest,
        collateral_principal=account.balance,
        margin_calls=[dict(id=id,due=str(call.due),amount=call.amount,settled=call.settled,cancelled=call.cancelled) for id,call in account.transfers.items()],
        clearing_events=[dict(id=e.id,at=str(e.at),kind=str(e.kind),trade=e.trade,reference=e.reference,amount=e.amount,due=str(e.due) if e.due else None) for e in ledger.journal],
        collateral_events=[dict(id=e.id,at=str(e.at),kind=str(e.kind),reference=e.reference,value=e.value) for e in account.journal])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir",required=True,type=Path)
    parser.add_argument("--out",required=True,type=Path)
    args=parser.parse_args()
    report=run(args.module_dir)
    args.out.mkdir(parents=True,exist_ok=True)
    (args.out/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({k:report[k] for k in ["synthetic_quotes","synthetic_fixings","final_clearing_state","collateral_closed","clearing_cash","interest_cash","collateral_principal"]},indent=2))


if __name__ == "__main__":
    main()
