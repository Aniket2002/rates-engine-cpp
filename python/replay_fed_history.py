"""Run the C++ batch engine on pinned, real Fed curves, one available date at a time."""
import argparse
import csv
import json
import math
from pathlib import Path
import subprocess
from import_fed_history import sha256


def verify_data(data):
    manifest = json.loads((data / "manifest.json").read_text(encoding="utf-8"))
    if manifest["timestamp_basis"] != "retrospective_observation_date" or manifest["point_in_time"] is not False:
        raise ValueError("Expected explicitly retrospective Fed dataset")
    for name in ("source-excerpt.csv", "quote_history.csv", "portfolio.csv"):
        if sha256((data / name).read_bytes()) != manifest["files"][name]:
            raise ValueError(f"Data hash mismatch: {name}")
    with (data / "quote_history.csv").open(newline="", encoding="utf-8") as stream:
        dates = sorted({r["timestamp"] for r in csv.DictReader(stream)})
    if len(dates) != manifest["observations"]:
        raise ValueError("Observation count differs from manifest")
    return manifest, dates


def run_replay(batch, data, output, sample=0):
    manifest, dates = verify_data(data)
    if sample < 0 or sample > len(dates):
        raise ValueError("Sample must be zero (all dates) or within available dates")
    if sample:
        dates = [dates[i * (len(dates) - 1) // max(1, sample - 1)] for i in range(sample)]
    output.mkdir(parents=True, exist_ok=True)
    reports = []
    with (output / "replay.jsonl").open("w", encoding="utf-8") as events:
        for stamp in dates:
            folder = output / stamp[:10]
            result = subprocess.run([str(batch), "--history", str(data / "quote_history.csv"),
                "--portfolio", str(data / "portfolio.csv"), "--as-of", stamp, "--max-age-seconds", "0",
                "--data-source", "federal-reserve-gsw", "--replay-basis", "retrospective_observation_date",
                "--out", str(folder)], capture_output=True, text=True)
            try:
                report = json.loads(result.stdout)
            except json.JSONDecodeError as error:
                raise RuntimeError(f"Batch did not produce a JSON summary: {result.stderr}") from error
            events.write(json.dumps(report, allow_nan=False) + "\n")
            events.flush()
            reports.append(report)
            if result.returncode or report["status"] != "success":
                raise RuntimeError(f"Historical batch failed at {stamp}: {report}")
            if report["snapshot_timestamp"] != stamp or report["data_age_seconds"] != 0:
                raise RuntimeError("Replay selected a different or stale snapshot")
    pvs = [r["portfolio_pv"] for r in reports]
    risks = [r["parallel_quote_dv01"] for r in reports]
    if not all(math.isfinite(x) for x in pvs + risks):
        raise RuntimeError("Non-finite historical result")
    summary = dict(source=manifest["dataset"], source_url=manifest["source_url"],
        source_excerpt_sha256=manifest["files"]["source-excerpt.csv"],
        replay_basis=manifest["timestamp_basis"], point_in_time=False, publication_time_known=False,
        portfolio=manifest["portfolio"], snapshots=len(reports), trades_per_snapshot=manifest["trades"],
        priced_trade_snapshots=sum(r["priced_trades"] for r in reports),
        failed_trades=sum(r["failed_trades"] for r in reports),
        first_observation=dates[0][:10], last_observation=dates[-1][:10],
        first_pv=pvs[0], last_pv=pvs[-1], min_pv=min(pvs), max_pv=max(pvs),
        first_parallel_zero_quote_dv01=risks[0], last_parallel_zero_quote_dv01=risks[-1],
        max_processing_ms=max(r["processing_ms"] for r in reports),
        risk_units="currency/bp; input quotes are continuously compounded zero yields",
        warning="Retrospective current-vintage Treasury proxy; hypothetical swaps; PV changes are not realized trading P&L")
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--batch", required=True, type=Path)
    parser.add_argument("--data", type=Path, default=Path("data/fed-gsw"))
    parser.add_argument("--out", type=Path, default=Path("results/fed-replay"))
    parser.add_argument("--sample", type=int, default=0)
    args = parser.parse_args()
    print(json.dumps(run_replay(args.batch.resolve(), args.data.resolve(), args.out, args.sample), indent=2))
