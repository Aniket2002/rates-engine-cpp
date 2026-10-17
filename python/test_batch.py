"""End-to-end replay, independent portfolio reference and operational failures."""
import csv
from datetime import date
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from generate_replay import generate
from reference_pricer import bootstrap, price

exe=str(Path(sys.argv[1]).resolve())


with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary)
    generate(root,100)
    history=root/"quote_history.csv"
    portfolio=root/"portfolio.csv"
    def run(name,extra,history_path=history,portfolio_path=portfolio):
        folder=root/name
        process=subprocess.run([exe,"--history",str(history_path),"--portfolio",str(portfolio_path),"--out",str(folder),*extra],capture_output=True,text=True)
        reports=[json.loads(line) for line in process.stdout.splitlines()]
        assert reports and all(r["processing_ms"]>=0 for r in reports), process.stderr
        return process.returncode,reports,folder
    code,reports,folder=run("base",["--as-of","2025-01-02T10:02:00Z","--max-age-seconds","300"])
    result=reports[0]
    assert code==0 and result["priced_trades"]==100 and result["failed_trades"]==0
    assert result["data_age_seconds"]==120 and result["schedule_cache_entries"]==5
    assert result["scenario_calibrations"]==19 and result["calibrations_this_run"]==19
    assert json.loads((folder/"summary.json").read_text())==result
    with history.open() as stream:
        quotes=[dict(type=r["type"],label=r["label"],maturity=date.fromisoformat(r["maturity"]),rate=float(r["rate"]))
                for r in csv.DictReader(stream) if r["timestamp"]=="2025-01-02T10:00:00Z"]
    with portfolio.open() as stream:
        trades=[dict(effective=date.fromisoformat(r["effective"]),maturity=date.fromisoformat(r["maturity"]),
                     notional=float(r["notional"]),coupon=float(r["fixed_rate"]),side=r["side"]) for r in csv.DictReader(stream)]
    def reference(shift=0,bucket=None):
        q=[dict(q,rate=q["rate"]+(shift if bucket is None or i==bucket else 0)) for i,q in enumerate(quotes)]
        curve=bootstrap(date(2025,1,2),q)
        return sum(price(curve,**t)["pv"] for t in trades)
    assert abs(result["portfolio_pv"]-reference())<0.002
    assert abs(result["parallel_quote_dv01"]-(reference(-1e-4)-reference(1e-4))/2)<0.02
    with (folder/"risk.csv").open() as stream:
        risks=[r for r in csv.DictReader(stream) if r["coordinate"]=="market_quote"]
    for i,row in enumerate(risks):
        assert abs(float(row["dv01"])-(reference(-1e-4,i)-reference(1e-4,i))/2)<0.02
    code,replay,folder=run("replay",["--replay-from","2025-01-02T10:00:00Z","--replay-to","2025-01-02T10:10:00Z","--step-seconds","120","--max-age-seconds","300"])
    assert code==0 and len(replay)==6
    assert [r["calibrations_this_run"] for r in replay]==[19,0,0,19,0,19]
    assert [r["snapshot_timestamp"] for r in replay][:3]==["2025-01-02T10:00:00Z"]*3
    assert len((folder/"replay.jsonl").read_text().splitlines())==6
    for name,time,category in [("missing","2025-01-02T09:59:59Z","missing_market_data"),
                               ("stale","2025-01-02T10:20:01Z","stale_market_data")]:
        code,rows,_=run(name,["--as-of",time,"--max-age-seconds","300"])
        assert code==1 and rows[0]["error_category"]==category and rows[0]["portfolio_pv"] is None
    broken=root/"impossible.csv"
    broken.write_text(history.read_text().replace(",0.025",",-100"))
    code,rows,_=run("calibration",["--as-of","2025-01-02T10:00:00Z"],history_path=broken)
    assert code==1 and rows[0]["calibration_status"]=="failed" and rows[0]["error_category"]=="calibration"
    partial=root/"partial.csv"
    partial.write_text(portfolio.read_text()+"bad,2025-01-06,2030-01-02,-1,0.03,payer\n")
    code,rows,folder=run("partial",["--as-of","2025-01-02T10:00:00Z"],portfolio_path=partial)
    assert code==2 and rows[0]["failed_trades"]==1 and rows[0]["priced_trades"]==100
    assert "bad" in (folder/"failed_trades.csv").read_text()
    code,rows,_=run("absent",["--as-of","2025-01-02T10:00:00Z"],history_path=root/"absent.csv")
    assert code==1 and rows[0]["error_category"]=="missing_market_data"
    # A broken newer observation must not reuse the previously successful curve.
    newer=root/"newer-broken.csv"
    newer.write_text("\n".join(line.rsplit(",",1)[0]+",-100" if line.startswith("2025-01-02T10:05:00Z") else line
                               for line in history.read_text().splitlines())+"\n")
    code,rows,_=run("no-fallback",["--replay-from","2025-01-02T10:00:00Z","--replay-to","2025-01-02T10:10:00Z","--step-seconds","300"],history_path=newer)
    assert code==1 and [r["status"] for r in rows]==["success","failed","success"]
    assert rows[1]["portfolio_pv"] is None and not rows[1]["scenarios_reused"]
    assert rows[1]["calibration_ms"]>0 and rows[2]["calibrations_this_run"]==19
    all_bad=root/"all-bad.csv"
    all_bad.write_text("id,effective,maturity,notional,fixed_rate,side\nbad,2025-01-06,2030-01-02,-1,0.03,payer\n")
    code,rows,_=run("all-failed",["--as-of","2025-01-02T10:00:00Z"],portfolio_path=all_bad)
    assert code==1 and rows[0]["failed_trades"]==1 and rows[0]["portfolio_pv"] is None
    trailing=root/"trailing.csv"
    trailing.write_text(portfolio.read_text().replace("payer\n","payer,\n"))
    code,rows,_=run("bad-columns",["--as-of","2025-01-02T10:00:00Z"],portfolio_path=trailing)
    assert code==1 and rows[0]["error_category"]=="invalid_input"
print("Batch/replay: independent 100-trade PV and quote risk, freshness, no look-ahead, scenario reuse and failure summaries passed")
