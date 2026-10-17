"""Independent convention and QuantLib rate-helper validation, retaining failures."""
import argparse
from collections import Counter
import csv
from dataclasses import fields
from datetime import date, timedelta
import json
import math
from pathlib import Path
import subprocess
import sys

from reference_pricer import Conventions, Curve, add_months, adjust, bootstrap, price, quote_risk, schedule
from validate_results import read_csv

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--output", default=Path("results/conventions"), type=Path)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records, failures, helper_cases = [], [], []
    try:
        import QuantLib as ql
        import quantlib_reference as qr
        version = ql.__version__
    except ImportError:
        version = None
        if args.require_quantlib:
            failures.append("QuantLib required but unavailable")

    def check(case, source, metric, expected, actual, tolerance):
        error = abs(actual-expected)
        ok = math.isfinite(actual) and math.isfinite(expected) and error <= tolerance
        records.append(dict(case=case, source=source, metric=metric, reference=expected, cpp=actual,
            absolute_error=error, relative_error=error/abs(expected) if abs(expected)>1e-10 else "",
            tolerance=tolerance, status="PASS" if ok else "FAIL"))
        if not ok:
            failures.append(f"{case}/{source}/{metric}: {actual} vs {expected} (tol {tolerance})")

    specifications = json.loads((ROOT / "examples/convention_cases.json").read_text())
    for spec in specifications["cases"]:
        name = spec["name"]
        try:
            options = {field.name: spec[field.name] for field in fields(Conventions) if field.name in spec}
            options["holidays"] = tuple(date.fromisoformat(d) for d in options.get("holidays", []))
            c = Conventions(**options)
            valuation = date.fromisoformat(spec["valuation"])
            months = spec.get("months", [1,3,6,12,24,36,60,120])
            quotes = [dict(type="deposit" if i<3 else "swap", label=f"{m}M",
                           maturity=add_months(valuation,m,c.eom), rate=spec.get("flat_quote",0.025+0.0015*i))
                      for i,m in enumerate(months)]
            trade = dict(effective=adjust(add_months(valuation,spec.get("effective_months",0)),c),
                         maturity=add_months(valuation,spec.get("trade_months",60),c.eom)+timedelta(days=spec.get("extra_days",0)),
                         notional=1e6,coupon=0.027,side=spec.get("side","payer"),conventions=c)
            folder = args.output/name
            folder.mkdir(exist_ok=True)
            with (folder/"quotes.csv").open("w",newline="") as stream:
                writer=csv.writer(stream); writer.writerow(["type","label","maturity","rate"])
                for quote in quotes:
                    writer.writerow([quote["type"],quote["label"],quote["maturity"],quote["rate"]])
            with (folder/"swap.csv").open("w",newline="") as stream:
                writer=csv.writer(stream); writer.writerow(["effective","maturity","notional","fixed_rate","side"])
                writer.writerow([trade[k] for k in ("effective","maturity","notional","coupon","side")])
            command=[str(args.cli.resolve()),"--valuation",str(valuation),"--quotes",str(folder/"quotes.csv"),
                "--swap",str(folder/"swap.csv"),"--out",str(folder),"--fixed-months",str(c.fixed_months),
                "--floating-months",str(c.floating_months),"--fixed-dc",c.fixed_dc,"--floating-dc",c.floating_dc,
                "--deposit-dc",c.deposit_dc,"--adjustment",c.adjustment,"--eom",str(c.eom).lower()]
            if c.holidays:
                command += ["--holidays",",".join(map(str,c.holidays))]
            process=subprocess.run(command,capture_output=True,text=True)
            (folder/"cli.log").write_text(process.stdout+process.stderr)
            if process.returncode:
                raise RuntimeError(process.stderr)
            summary={r["metric"]:r["value"] for r in read_csv(folder/"summary.csv")}
            reference=bootstrap(valuation,quotes,c)
            cpp=Curve(valuation,[(date.fromisoformat(r["date"]),float(r["discount"])) for r in read_csv(folder/"curve.csv")[1:]])
            for field, expected in price(reference,**trade).items():
                check(name,"python",field,expected,float(summary[field]),2e-5 if field.endswith("pv") else 2e-10)
            for (day,df),(cpp_day,cpp_df) in zip(reference.nodes,cpp.nodes):
                check(name,"python","pillar_date",day.toordinal(),cpp_day.toordinal(),0)
                check(name,"python","pillar_discount",df,cpp_df,5e-10)
            flows=read_csv(folder/"cashflows.csv")
            for leg,interval,dc in [("fixed",c.fixed_months,c.fixed_dc),("floating",c.floating_months,c.floating_dc)]:
                expected=schedule(trade["effective"],trade["maturity"],interval,dc,c)
                actual=[r for r in flows if r["leg"]==leg]
                check(name,"python",leg+"_period_count",len(expected),len(actual),0)
                for i,((start,end,alpha),row) in enumerate(zip(expected,actual)):
                    for field,day in [("start",start),("end",end),("payment",end)]:
                        check(name,"python",f"{leg}_{i}_{field}",day.toordinal(),date.fromisoformat(row[field]).toordinal(),0)
                    check(name,"python",f"{leg}_{i}_accrual",alpha,float(row["accrual"]),1e-15)
                    rate=trade["coupon"] if leg=="fixed" else (reference.discount(start)/reference.discount(end)-1)/alpha
                    check(name,"python",f"{leg}_{i}_pv",trade["notional"]*alpha*rate*reference.discount(end),float(row["pv"]),2e-5)
                if version:
                    external=qr.periods(trade["effective"],trade["maturity"],interval,dc,c)
                    check(name,"quantlib_schedule",leg+"_period_count",len(external),len(actual),0)
                    for i,((start,end,alpha),row) in enumerate(zip(external,actual)):
                        check(name,"quantlib_schedule",f"{leg}_{i}_start",start.toordinal(),date.fromisoformat(row["start"]).toordinal(),0)
                        check(name,"quantlib_schedule",f"{leg}_{i}_end",end.toordinal(),date.fromisoformat(row["end"]).toordinal(),0)
                        check(name,"quantlib_schedule",f"{leg}_{i}_accrual",alpha,float(row["accrual"]),1e-15)
            check(name,"python","parallel_dv01",quote_risk(valuation,quotes,trade),float(summary["parallel_quote_dv01"]),0.002)
            if version:
                for field,expected in qr.price(cpp,trade,c).items():
                    check(name,"quantlib_pricing",field,expected,float(summary[field]),1e-7 if field.endswith("pv") else 1e-12)
            if version and spec.get("helpers",False):
                qcurve,helpers=qr.bootstrap(valuation,quotes,c)
                helper_cases.append(name)
                for day,df in cpp.nodes:
                    check(name,"quantlib_helpers","discount_"+str(day),qcurve.discount(qr.qdate(day)),df,5e-10)
                for row in flows:
                    day=date.fromisoformat(row["payment"])
                    check(name,"quantlib_helpers","interpolated_"+str(day),qcurve.discount(qr.qdate(day)),float(row["discount"]),5e-10)
                for quote,helper in zip(quotes,helpers):
                    check(name,"quantlib_helpers","implied_"+quote["label"],quote["rate"],helper.impliedQuote(),2e-12)
                for field,expected in qr.price_on_curve(qcurve,valuation,trade,c).items():
                    check(name,"quantlib_helpers",field,expected,float(summary[field]),2e-5 if field.endswith("pv") else 2e-10)
                # Independent helper rebootstrap for every quoted risk coordinate.
                risk_rows=read_csv(folder/"risk.csv")
                for bucket in [None]+list(range(len(quotes))):
                    values=[]
                    for sign in [-1,1]:
                        shocked=[dict(q,rate=q["rate"]+(sign*1e-4 if bucket is None or bucket==i else 0)) for i,q in enumerate(quotes)]
                        shocked_curve,_=qr.bootstrap(valuation,shocked,c)
                        values.append(qr.price_on_curve(shocked_curve,valuation,trade,c)["pv"])
                    actual=float(summary["parallel_quote_dv01"]) if bucket is None else float(next(r["dv01"] for r in risk_rows if r["coordinate"]=="market_quote" and r["label"]==quotes[bucket]["label"]))
                    check(name,"quantlib_helpers","dv01_"+("parallel" if bucket is None else quotes[bucket]["label"]),(values[0]-values[1])/2,actual,0.002)
        except Exception as error:
            failures.append(f"{name}: {type(error).__name__}: {error}")
    if records:
        with (args.output/"comparisons.csv").open("w",newline="") as stream:
            writer=csv.DictWriter(stream,fieldnames=list(records[0])); writer.writeheader(); writer.writerows(records)
    report=dict(comparisons=len(records),passed=sum(r["status"]=="PASS" for r in records),
                source_counts=dict(Counter(r["source"] for r in records)),quantlib_version=version,
                helper_cases=helper_cases,failures=failures,
                helper_scope="Independent quote-only DepositRateHelper/SwapRateHelper calibration; no engine node inputs")
    (args.output/"report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))
    return 1 if failures else 0


if __name__=="__main__":
    sys.exit(main())
