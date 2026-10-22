"""Render auditable SOFR cash flows and realized/projected observations (PNG/SVG)."""
import argparse
from datetime import date
from hashlib import sha256
import json
import math
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.dates as mdates
import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True, help="Content-addressed run_sofr_valuation JSON")
    parser.add_argument("--out", type=Path, default=Path("docs/charts"))
    args = parser.parse_args()
    raw = args.report.read_bytes()
    digest = sha256(raw).hexdigest()
    if args.report.stem != digest:
        raise ValueError("Valuation report content hash does not match its filename")
    report = json.loads(raw)
    if not report["cashflows"]:
        raise ValueError("Chart requires outstanding coupons")
    args.out.mkdir(parents=True, exist_ok=True)
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 10,
                         "axes.spines.top": False, "axes.spines.right": False,
                         "svg.hashsalt": "rates-engine-sofr-2.2"})
    fig, (ax, daily) = plt.subplots(2, 1, figsize=(11, 8), gridspec_kw={"height_ratios": [1.2, 1]})
    realized_amounts, projected_amounts, counts = [], [], []
    for cf in report["cashflows"]:
        obs = [o for o in report["observations"] if o["coupon"] == cf["coupon"]]
        realized = math.fsum(math.log1p(o["rate"]*o["weight"]) for o in obs if o["realized"])
        projected = math.fsum(math.log1p(o["rate"]*o["weight"]) for o in obs if not o["realized"])
        growth = math.expm1(realized+projected)
        # Infer the shared notional/accrual scale from the recorded full coupon.
        # At exactly zero net growth, this attribution is undefined: fail explicitly.
        if growth == 0:
            raise ValueError("Cannot attribute a zero-growth coupon from report amounts alone")
        known = cf["floating_amount"]*math.expm1(realized)/growth
        realized_amounts.append(known/1000)
        projected_amounts.append((cf["floating_amount"]-known)/1000)
        counts.append((sum(o["realized"] for o in obs), sum(not o["realized"] for o in obs)))
    x = list(range(len(counts)))
    ax.bar(x, realized_amounts, color="#126c81", label="Realized fixing contribution")
    ax.bar(x, projected_amounts, bottom=realized_amounts, color="#d58c36", label="Projected contribution (includes compounding cross-term)")
    ax.plot(x, [cf["fixed_amount"]/1000 for cf in report["cashflows"]], "_", color="#26354a", markersize=20, markeredgewidth=2, label="Fixed payment")
    ax.set_xticks(x, [cf["payment"] for cf in report["cashflows"]], rotation=20)
    for i, (realized, projected) in enumerate(counts):
        ax.annotate(f"{realized} R / {projected} P", (i, max(realized_amounts[i]+projected_amounts[i], report["cashflows"][i]["fixed_amount"]/1000)),
                    xytext=(0, 8), textcoords="offset points", ha="center", fontsize=8)
    ax.set(title="Seasoned SOFR swap: outstanding cash flows", ylabel="Undiscounted amount (USD thousands)", xlabel="Payment date | R / P = realized / projected observation counts")
    ax.margins(y=.45)
    ax.legend(loc="upper right", fontsize=8, frameon=False)
    obs = [o for o in report["observations"] if o["coupon"] == report["cashflows"][0]["coupon"]]
    for realized, color, label in [(True, "#126c81", "Realized NY Fed fixings"), (False, "#d58c36", "Projected from curve")]:
        selected = [o for o in obs if o["realized"] == realized]
        for i, o in enumerate(selected):
            daily.plot([date.fromisoformat(o["accrual_start"]), date.fromisoformat(o["accrual_end"])],
                       [100*o["rate"]]*2, color=color, linewidth=2, label=label if i == 0 else None)
    daily.axvline(date.fromisoformat(report["as_of"][:10]), color="#26354a", linestyle="--", label="Valuation date")
    daily.set(title="First coupon: daily rates over contractual accrual intervals", ylabel="Observation rate (%)", xlabel="Accrual date (fixing dates use the trade's lookback)")
    daily.xaxis.set_major_formatter(mdates.DateFormatter("%d %b %Y"))
    daily.legend(frameon=False, fontsize=9)
    for axis in (ax, daily):
        axis.grid(axis="y", alpha=.2)
    quote_label = "SYNTHETIC OIS QUOTES — illustrative valuation, not a market mark" if report["synthetic_quotes"] else "Imported OIS quotes — see report provenance"
    fig.suptitle(f"{report['realized_observations']} realized + {report['forecast_observations']} projected observations | as of {report['as_of']}", fontsize=12)
    fig.text(.07, .025, quote_label+"\nRealized contribution compounds known fixings alone; projected contribution includes their interaction.", fontsize=9, color="#526273")
    fig.tight_layout(rect=(0, .075, 1, .96))
    for ext in ["png", "svg"]:
        fig.savefig(args.out / f"sofr-cashflows.{ext}", dpi=180, facecolor="white", metadata={"Date": None} if ext == "svg" else None)
    plt.close(fig)
    (args.out / "sofr-manifest.json").write_text(json.dumps(dict(
        input_report=args.report.as_posix(), input_sha256=digest, matplotlib_version=matplotlib.__version__,
        synthetic_quotes=report["synthetic_quotes"], outputs=["sofr-cashflows.png", "sofr-cashflows.svg"]), indent=2)+"\n")


if __name__ == "__main__":
    main()
