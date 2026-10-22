"""Render recorded hedge results; requires matplotlib only for this optional chart."""
import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, default=Path("docs/results/hedging/report.json"))
    parser.add_argument("--out", type=Path, default=Path("docs/charts"))
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    if len(report["independent_quantlib"]) != 11 or len(report["independent_quote_risk"]) != 7:
        raise ValueError("Chart requires complete independent price and risk validation")
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False,
                         "svg.hashsalt": "rates-engine-hedge-v2.3"})
    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5))
    buckets = report["quote_key_rates"]
    for offset, field, color, label in [(-.18, "unhedged_dv01", "#126c81", "Original"),
                                        (.18, "hedged_dv01", "#dc8540", "With hedges")]:
        axes[0].bar([i+offset for i in range(len(buckets))], [r[field] for r in buckets],
                    width=.36, color=color, label=label)
    axes[0].set_xticks(range(len(buckets)), [r["label"] for r in buckets])
    axes[0].set(title="Quote risk falls 51.68%, but remains", ylabel="Signed quote DV01 (USD/bp)",
                xlabel="Market quote tenor")
    axes[0].axhline(0, color="#526273", linewidth=.8)
    axes[0].legend(frameon=False)
    names = ["parallel_up_25bp", "parallel_down_25bp", "steepener", "flattener", "isolated_5y_up_25bp"]
    rows = [next(r for r in report["scenarios"] if r["scenario"] == name) for name in names]
    for offset, field, color, label in [(-.18, "unhedged_change", "#126c81", "Original"),
                                        (.18, "hedged_change", "#dc8540", "With hedges")]:
        axes[1].bar([i+offset for i in range(len(rows))], [r[field]/1000 for r in rows],
                    width=.36, color=color, label=label)
    axes[1].set_xticks(range(len(rows)), ["+25 bp", "-25 bp", "Steepener", "Flattener", "5Y +25 bp"], rotation=20)
    axes[1].set(title="A smaller risk norm does not improve every shock", ylabel="Full-repricing PV change (USD thousands)")
    axes[1].axhline(0, color="#526273", linewidth=.8)
    axes[1].legend(frameon=False)
    fig.suptitle("5Y SOFR swap hedged with 2Y / 4Y / 6Y swaps", fontsize=16, fontweight="bold")
    fig.text(.05, .025, "Synthetic quotes; fixed rounded hedge notionals. PV changes shown before assumed USD 379.73 entry costs.\n"
             "Same-date shocks, no carry or funding. Independent QuantLib calibration and quote-risk comparisons.",
             fontsize=9, color="#526273")
    fig.tight_layout(rect=(0, .10, 1, .94))
    args.out.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.out/"hedging.png", dpi=180)
    fig.savefig(args.out/"hedging.svg", metadata={"Date": None})
    svg = args.out/"hedging.svg"
    svg.write_text("\n".join(line.rstrip() for line in svg.read_text(encoding="utf-8").splitlines())+"\n",
                   encoding="utf-8", newline="\n")
    plt.close(fig)
    manifest = dict(source=str(args.report), sha256=hashlib.sha256(args.report.read_text(encoding="utf-8").encode()).hexdigest(),
                    matplotlib=matplotlib.__version__)
    (args.out/"hedging-manifest.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
