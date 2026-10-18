"""Offline provenance, import rejection, C++ historical replay and independent valuation."""
import argparse
import csv
from datetime import date
import io
import json
import math
from pathlib import Path
import subprocess
import tempfile
from import_fed_history import import_history, parse_source, SERIES
from reference_pricer import Curve, price
from replay_fed_history import run_replay, verify_data

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--require-quantlib", action="store_true")
    args = parser.parse_args()
    batch, cli = args.batch.resolve(), args.cli.resolve()
    data = ROOT / "data/fed-gsw"
    manifest, _ = verify_data(data)
    raw = (data / "source-excerpt.csv").read_bytes()
    observations, missing, _ = parse_source(raw, date(2025, 1, 1), date(2025, 12, 31))
    assert len(observations) == 249 and len(missing) == 12
    assert float(observations[0][1][0]) == 0.041919
    with tempfile.TemporaryDirectory() as temporary:
        temp = Path(temporary)
        regenerated = import_history(raw, temp, date(2025, 1, 1), date(2025, 12, 31), 100)
        assert regenerated["files"] == manifest["files"], "Pinned input must reproduce exact outputs"
        (temp / "quote_history.csv").write_text("tampered")
        try:
            verify_data(temp)
        except ValueError:
            pass
        else:
            raise AssertionError("Hash tampering accepted")
    for broken in (raw.replace(b"4.1919", b"nan", 1), raw.replace(b"4.1919", b"NA", 1),
                   raw.replace(b"Continuously Compounded,SVENYXX", b"Unknown,SVENYXX")):
        try:
            parse_source(broken, date(2025, 1, 1), date(2025, 12, 31))
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed or partially missing source accepted")
    summary = run_replay(batch, data, args.out, sample=3)
    assert summary["priced_trade_snapshots"] == 300 and summary["failed_trades"] == 0
    with (data / "portfolio.csv").open(newline="") as stream:
        trades = [dict(id=r["id"], effective=date.fromisoformat(r["effective"]),
                       maturity=date.fromisoformat(r["maturity"]), notional=float(r["notional"]),
                       coupon=float(r["fixed_rate"]), side=r["side"]) for r in csv.DictReader(stream)]
    with (data / "quote_history.csv").open(newline="") as stream:
        normalized = list(csv.DictReader(stream))
    by_date = {str(d): values for d, values in observations}
    try:
        import QuantLib
        from quantlib_reference import price as qprice
        ql_version = QuantLib.__version__
    except ImportError:
        if args.require_quantlib:
            raise
        qprice, ql_version = None, None
    comparisons = 0
    max_pv_error, max_risk_error, max_ql_error, max_df_error = 0.0, 0.0, 0.0, 0.0
    for text in (args.out / "replay.jsonl").read_text().splitlines():
        report = json.loads(text)
        day = date.fromisoformat(report["as_of"][:10])
        values = by_date[str(day)]
        # Construct reference nodes directly from provider observations, not C++ output.
        from datetime import timedelta
        def curve(shift=0, bucket=None):
            return Curve(day, [(day + timedelta(days=365*n),
                               math.exp(-(float(r) + (shift if bucket is None or bucket == n-1 else 0))*n))
                              for n, r in enumerate(values, 1)])
        def pv(c):
            return math.fsum(price(c, **{k:v for k,v in t.items() if k != "id"})["pv"] for t in trades)
        reference_pv = pv(curve())
        reference_risk = (pv(curve(-1e-4))-pv(curve(1e-4)))/2
        max_pv_error = max(max_pv_error, abs(reference_pv-report["portfolio_pv"]))
        max_risk_error = max(max_risk_error, abs(reference_risk-report["parallel_quote_dv01"]))
        assert max_pv_error < 1e-5 and max_risk_error < 1e-5
        assert report["data_source"] == "federal-reserve-gsw"
        assert report["replay_basis"] == "retrospective_observation_date"
        assert report["scenario_calibrations"] == 23
        comparisons += 2
        folder = args.out / str(day)
        with (folder / "risk.csv").open(newline="") as stream:
            risks = list(csv.DictReader(stream))
        assert len(risks) == 20
        for row in risks:
            if row["coordinate"] == "market_quote":
                index = SERIES.index(row["label"])
            else:
                index = ((date.fromisoformat(row["maturity"]) - day).days // 365)-1
            expected = (pv(curve(-1e-4, index))-pv(curve(1e-4, index)))/2
            max_risk_error = max(max_risk_error, abs(expected-float(row["dv01"])))
            assert max_risk_error < 1e-5
            comparisons += 1
        # Exercise the regular CLI too; compare exact-date pillars and residuals.
        quote_file, swap_file = folder / "quotes.csv", folder / "swap.csv"
        with quote_file.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["type", "label", "maturity", "rate"])
            writer.writerows([r[k] for k in ("type", "label", "maturity", "rate")]
                             for r in normalized if r["valuation_date"] == str(day))
        t = trades[0]
        with swap_file.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["effective", "maturity", "notional", "fixed_rate", "side"])
            writer.writerow([t[k] for k in ("effective", "maturity", "notional", "coupon", "side")])
        result = subprocess.run([str(cli), "--valuation", str(day), "--quotes", str(quote_file),
            "--swap", str(swap_file), "--curve-id", "fed-gsw-treasury-proxy", "--out", str(folder / "cli")],
            capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        with (folder / "cli/curve.csv").open() as stream:
            nodes = list(csv.DictReader(stream))[1:]
        assert len(nodes) == 10
        for node, (expected_date, discount) in zip(nodes, curve().nodes):
            assert node["date"] == str(expected_date)
            max_df_error = max(max_df_error, abs(float(node["discount"])-discount))
            assert max_df_error < 1e-14
            comparisons += 1
        with (folder / "cli/calibration.csv").open() as stream:
            assert all(r["instrument"] == "continuous_zero" and abs(float(r["residual"])) < 1e-12 for r in csv.DictReader(stream))
        with (folder / "trades.csv").open() as stream:
            actual = list(csv.DictReader(stream))
        if qprice:
            for trade, row in zip(trades[:3], actual[:3]):
                expected = qprice(curve(), trade)["pv"]
                risk = (qprice(curve(-1e-4), trade)["pv"]-qprice(curve(1e-4), trade)["pv"])/2
                max_ql_error = max(max_ql_error, abs(expected-float(row["pv"])), abs(risk-float(row["parallel_quote_dv01"])))
                assert max_ql_error < 1e-6
                comparisons += 2
    # No current-day snapshot before the explicit replay label; holiday data stays stale.
    for stamp, age, category in [("2025-01-02T23:59:58Z", "0", "missing_market_data"),
                                  ("2025-01-20T23:59:59Z", "86400", "stale_market_data")]:
        result = subprocess.run([str(batch), "--history", str(data / "quote_history.csv"),
            "--portfolio", str(data / "portfolio.csv"), "--as-of", stamp, "--max-age-seconds", age,
            "--out", str(args.out / "rejections")], capture_output=True, text=True)
        assert result.returncode == 1 and json.loads(result.stdout)["error_category"] == category
    validation = dict(comparisons=comparisons, quantlib_version=ql_version,
        max_pv_error=max_pv_error, max_risk_error=max_risk_error, max_discount_error=max_df_error,
        max_quantlib_error=max_ql_error if qprice else None, source_observations=249, passed=True)
    (args.out / "validation.json").write_text(json.dumps(validation, indent=2)+"\n")
    print(json.dumps(validation))


if __name__ == "__main__":
    main()
