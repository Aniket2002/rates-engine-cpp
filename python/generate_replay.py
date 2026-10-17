"""Generate reproducible synthetic intraday quotes and forward-starting swaps."""
import argparse
import csv
from datetime import date, datetime, timedelta
from pathlib import Path
from reference_pricer import add_months


def generate(output, trades=100, snapshots=4):
    if not 1 <= trades <= 100000 or not 1 <= snapshots <= 100:
        raise ValueError("Use 1..100000 trades and 1..100 intraday snapshots")
    output.mkdir(parents=True, exist_ok=True)
    valuation = date(2025,1,2)
    with (output/"quote_history.csv").open("w",newline="") as stream:
        writer=csv.writer(stream)
        writer.writerow(["timestamp","valuation_date","type","label","maturity","rate"])
        for snapshot in range(snapshots):
            timestamp=datetime(2025,1,2,10)+timedelta(minutes=5*snapshot)
            for i,months in enumerate([1,3,6,12,24,36,60,120]):
                writer.writerow([timestamp.strftime("%Y-%m-%dT%H:%M:%SZ"),valuation,"deposit" if i<3 else "swap",
                                 f"{months}M",add_months(valuation,months),0.025+0.0015*i+0.0001*snapshot])
    with (output/"portfolio.csv").open("w",newline="") as stream:
        writer=csv.writer(stream)
        writer.writerow(["id","effective","maturity","notional","fixed_rate","side"])
        for i in range(trades):
            writer.writerow([f"swap-{i:05}","2025-01-06",add_months(valuation,24+12*(i%5)),
                             100000*(1+i%7),0.028+0.0002*(i%9),"receiver" if i%3==0 else "payer"])


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,default=Path("results/replay-input"))
    parser.add_argument("--trades",type=int,default=100)
    parser.add_argument("--snapshots",type=int,default=4)
    args=parser.parse_args()
    generate(args.out,args.trades,args.snapshots)
