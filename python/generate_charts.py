"""Render publication charts solely from validated, recorded CSV outputs."""
import argparse
from bisect import bisect_left
import csv
from hashlib import sha256
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def read(path):
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path("docs/results"))
    parser.add_argument("--out", type=Path, default=Path("docs/charts"))
    args = parser.parse_args()
    validation = json.loads((args.results / "validation-report.json").read_text())
    study = json.loads((args.results / "case-study/report.json").read_text())
    if (validation["failures"] or validation["comparisons"] != validation["passed"] or not study["passed"]
            or not validation.get("quantlib_version") or not study.get("quantlib_version")):
        raise ValueError("Charts require successful validation reports")
    args.out.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11, "axes.spines.top": False,
                         "axes.spines.right": False, "axes.titleweight": "bold", "svg.hashsalt": "rates-engine-2.1"})
    files = ["demo/curve.csv", "demo/risk.csv", "case-study/pv-changes.csv",
             "validation-report.json", "case-study/report.json"]
    # Normalize text newlines so hashes also verify after a Git checkout on another OS.
    manifest = {name: sha256((args.results / name).read_text().encode()).hexdigest() for name in files}

    def save(fig, name, caption):
        fig.text(0.08, 0.025, caption, fontsize=9, color="#526273")
        fig.tight_layout(rect=(0, 0.065, 1, 1))
        fig.savefig(args.out / (name+".png"), dpi=180, facecolor="white")
        fig.savefig(args.out / (name+".svg"), metadata={"Date": None}, facecolor="white")
        svg = args.out / (name+".svg")
        svg.write_text("\n".join(line.rstrip() for line in svg.read_text().splitlines())+"\n", newline="\n")
        plt.close(fig)

    nodes = [r for r in read(args.results / "demo/curve.csv") if r["zero"]]
    fig, ax = plt.subplots(figsize=(10, 5.6))
    times = [0.0] + [float(r["time"]) for r in nodes]
    logs = [0.0] + [math.log(float(r["discount"])) for r in nodes]
    grid = [times[-1]*i/500 for i in range(1, 501)]
    zeros = []
    for time in grid:
        j = bisect_left(times, time)
        weight = (time-times[j-1])/(times[j]-times[j-1])
        zeros.append(-((1-weight)*logs[j-1]+weight*logs[j])/time*100)
    ax.plot(grid, zeros, color="#126c81", linewidth=2.4, label="Continuous zero yield")
    ax.scatter(times[1:], [float(r["zero"])*100 for r in nodes], color="#126c81", zorder=3)
    ax.set(xlabel="Maturity (ACT/365F years)", ylabel="Zero yield (%)",
           title="From eight market quotes to a calibrated curve")
    ax.grid(axis="y", alpha=0.2)
    save(fig, "curve", "Synthetic deposit/swap inputs | 2 Jan 2025 | Log-linear discount interpolation")

    # Instantaneous forwards are constant on each open pillar interval. Draw
    # horizontal segments and mark jumps, rather than smoothing between nodes.
    forwards = [-(logs[i + 1] - logs[i]) / (times[i + 1] - times[i]) * 100
                for i in range(len(nodes))]
    fig, ax = plt.subplots(figsize=(10, 5.6))
    ax.stairs(forwards, times, baseline=None, color="#126c81", linewidth=2.4,
              label="Piecewise constant instantaneous forward")
    for i in range(1, len(forwards)):
        ax.scatter([times[i], times[i]], [forwards[i - 1], forwards[i]],
                   color="#d58c36", s=24, zorder=3)
    ax.set(xlabel="Maturity (ACT/365F years)", ylabel="Instantaneous forward (%)",
           title="Log-linear discounts imply forward jumps at pillars")
    ax.grid(axis="y", alpha=0.2)
    ax.legend(frameon=False, loc="upper left")
    save(fig, "forwards", "Synthetic inputs | Segment slopes of -log(discount) | No smoothing or extrapolation")

    risk = read(args.results / "demo/risk.csv")
    quotes = [r for r in risk if r["coordinate"] == "market_quote"]
    zeros = [r for r in risk if r["coordinate"] == "zero_node"]
    if [r["maturity"] for r in quotes] != [r["maturity"] for r in zeros]:
        raise ValueError("Demo chart requires matching risk maturities")
    fig, ax = plt.subplots(figsize=(10, 5.6))
    x = list(range(len(quotes)))
    ax.bar([i-0.2 for i in x], [float(r["dv01"]) for r in quotes], width=0.4, color="#126c81", label="Market quote (rebootstrap)")
    ax.bar([i+0.2 for i in x], [float(r["dv01"]) for r in zeros], width=0.4, color="#d58c36", label="Continuous-zero node")
    ax.set_xticks(x, [r["label"] for r in quotes])
    ax.set(xlabel="Curve maturity", ylabel="Signed DV01 (currency / bp)", title="The same swap, two different risk coordinates")
    ax.axhline(0, color="#526273", linewidth=0.8)
    ax.legend(loc="lower left", frameon=False)
    ax.grid(axis="y", alpha=0.2)
    save(fig, "risk-buckets", "1m notional, 5Y payer, 3.7% fixed | DV01 = (PV down - PV up) / 2 for a 1 bp bump")

    changes = [r for r in read(args.results / "case-study/pv-changes.csv") if r["parallel_bps"]]
    shocks = [float(r["parallel_bps"]) for r in changes]
    fig, (ax, residual) = plt.subplots(2, 1, figsize=(10, 6.5), sharex=True, gridspec_kw={"height_ratios": [2, 1]})
    ax.plot(shocks, [float(r["full_pv_change"])/1000 for r in changes], "o-", color="#126c81", label="Full rebootstrap and repricing")
    ax.plot(shocks, [float(r["bucket_linear_change"])/1000 for r in changes], "--", color="#d58c36", label="First-order bucket estimate")
    ax.set(ylabel="PV change (thousand currency)", title="How far does a linear risk estimate take you?")
    ax.legend(frameon=False)
    residual.bar(shocks, [float(r["nonlinear_residual"]) for r in changes], width=2, color="#126c81")
    residual.set(xlabel="Parallel market-quote shock (bp)", ylabel="Residual (currency)")
    for axis in (ax, residual):
        axis.grid(axis="y", alpha=0.2)
        axis.axhline(0, color="#526273", linewidth=0.6)
    save(fig, "pv-change", "Same-date synthetic scenario; no realized trading P&L | Independent Python and QuantLib checks")
    (args.out / "manifest.json").write_text(json.dumps(dict(matplotlib_version=matplotlib.__version__,
        inputs=manifest, outputs=[f"{name}.{ext}" for name in ["curve", "forwards", "risk-buckets", "pv-change"]
                                  for ext in ["png", "svg"]]), indent=2)+"\n")


if __name__ == "__main__":
    main()
