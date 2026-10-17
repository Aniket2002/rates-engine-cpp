"""Deterministic work-count checks; optional timing gate on a controlled runner."""
import argparse
import csv
from pathlib import Path
import subprocess
import math


def read(path):
    with Path(path).open(newline="") as source:
        return {(r["workload"], int(r["trades"])): r for r in csv.DictReader(source)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--max-slowdown", type=float, default=1.25)
    parser.add_argument("--samples", type=int, default=3)
    args = parser.parse_args()
    if not math.isfinite(args.max_slowdown) or args.max_slowdown < 1:
        parser.error("max-slowdown must be finite and >= 1")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.benchmark, str(args.output), str(args.samples), "100"], check=True)
    rows = read(args.output)
    expected = {"cached": (0, 0), "uncached_schedules": (0, 100),
                "end_to_end": (19, 5), "independent": (1900, 100)}
    assert len(rows) == len(expected)
    for name, counts in expected.items():
        row = rows[name, 100]
        assert (int(row["calibrations_per_iteration"]), int(row["schedule_builds_per_iteration"])) == counts
        assert 0 < float(row["p50_us"]) <= float(row["p95_us"]) <= float(row["p99_us"])
        assert float(row["trades_per_second"]) > 0
    if args.baseline:
        baseline = read(args.baseline)
        for key, row in rows.items():
            previous = baseline[key]
            for field in ("compiler", "configuration", "platform", "samples"):
                assert row[field] == previous[field], f"Incompatible baseline {field}"
            assert float(row["p50_us"]) <= float(previous["p50_us"]) * args.max_slowdown, key
    print("Numerical equivalence and deterministic performance work counts passed.")


if __name__ == "__main__":
    main()
