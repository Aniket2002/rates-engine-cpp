"""Offline CLI contracts and independent daily accounting; optional real-data validation."""
import argparse
import copy
import csv
from datetime import datetime, timezone
import io
import json
import math
from pathlib import Path
import statistics
import subprocess
import tempfile

from import_etf_history import normalize, sha


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def check_daily(simulator, data, out, basis):
    """Independent reference: daily open execution and close SMA, no C++ pricing code."""
    manifest = json.loads((data / "manifest.json").read_text()) if (data / "manifest.json").exists() else None
    if manifest:
        assert manifest["availability_basis"] == "retrospective"
        for name, digest in manifest["hashes"].items():
            assert sha((data / name).read_bytes()) == digest, "Input hash mismatch"
    command = [str(simulator), "--prices", str(data / "prices.csv"), "--decisions", str(data / "decisions.csv"),
               "--out", str(out), "--symbol", "TLT", "--basis", basis, "--evidence",
               "Yahoo later download; assumed availability" if manifest else "deterministic synthetic fixture",
               "--lookback", "3", "--cash", "10000", "--half-spread-bps", "10", "--fee-bps", "5"]
    subprocess.run(command, check=True, capture_output=True, text=True)
    prices, days = read_csv(data / "prices.csv"), read_csv(data / "decisions.csv")
    actual, fills = read_csv(out / "equity.csv"), read_csv(out / "fills.csv")
    orders = read_csv(out / "orders.csv")
    summary = json.loads((out / "summary.json").read_text())
    assert len(actual) == len(days)
    cash, shares, pending, peak = 10000., 0, 0, 10000.
    closes, returns = [], []
    expected_fills, fees, spread_cost, turnover, max_dd = 0, 0., 0., 0., 0.
    max_error, previous = 0., 10000.
    for i, point in enumerate(actual):
        opening, closing = prices[2 * i:2 * i + 2]
        assert opening["kind"] == "open" and closing["kind"] == "close"
        assert closing["available_at"] < days[i]["decision_time"]
        if pending:
            reference = float(opening["price"])
            execution = reference * (1.001 if pending > 0 else .999)
            qty = min(pending, math.floor(cash / (execution * 1.0005))) if pending > 0 else pending
            cost = abs(qty) * execution * .0005
            if qty:
                fill = fills[expected_fills]
                assert float(fill["shares"]) == qty
                assert fill["event_time"] == opening["event_time"]
                order = orders[int(fill["order_id"])]
                assert order["signal_available_at"] < order["decision_time"] < fill["event_time"]
                assert math.isclose(float(fill["execution_price"]), execution, abs_tol=1e-12)
                expected_fills += 1
            cash -= qty * execution + cost
            shares += qty
            fees += cost
            spread_cost += abs(qty) * reference * .001
            turnover += abs(qty) * execution
        close = float(closing["price"])
        closes.append(close)
        equity = cash + shares * close
        peak = max(peak, equity)
        max_dd = max(max_dd, 1 - equity / peak)
        if i:
            returns.append(equity / previous - 1)
        previous = equity
        for key, expected in dict(cash=cash, shares=shares, exposure=shares * close,
                                   equity=equity, pnl=equity - 10000).items():
            error = abs(float(point[key]) - expected)
            max_error = max(max_error, error)
            assert error < 1e-8, (key, point[key], expected)
        pending = 0
        if len(closes) >= 3:
            go_long = close > statistics.mean(closes[-3:])
            if go_long and shares == 0:
                pending = math.floor(cash / (close * 1.001 * 1.0005))
            elif not go_long and shares:
                pending = -shares
    assert expected_fills == len(fills) == summary["fill_count"]
    for key, expected in dict(final_equity=previous, fees=fees, spread_cost=spread_cost,
                               turnover=turnover / 10000, max_drawdown=max_dd,
                               period_volatility=statistics.stdev(returns)).items():
        assert math.isclose(summary[key], expected, abs_tol=1e-8), (key, summary[key], expected)
    assert summary["availability_basis"] == basis and summary["point_in_time_certified"] is False
    # Reusing an output directory cannot silently overwrite a completed run.
    assert subprocess.run(command, capture_output=True).returncode != 0
    return dict(status="passed", decisions=len(days), fills=len(fills), max_accounting_error=max_error,
                source_sha256=manifest["hashes"]["source.json"] if manifest else None)


def fixture(directory):
    prices = [100, 99, 101, 104, 103, 95, 94, 97, 100, 99, 98, 101]
    with (directory / "prices.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["event_time", "available_at", "kind", "price", "revision"])
        for i, price in enumerate(prices, 1):
            day = f"2025-03-{i:02}"
            writer.writerow([day + "T14:30:00Z", day + "T14:30:01Z", "open", price + 0.5, 0])
            writer.writerow([day + "T21:00:00Z", day + "T21:00:01Z", "close", price, 0])
    (directory / "decisions.csv").write_text("decision_time\n" + "".join(
        f"2025-03-{i:02}T21:05:00Z\n" for i in range(1, len(prices) + 1)))


def importer_contract():
    dates = [6, 7, 8, 10, 13, 14, 15, 16, 17, 21, 22, 23, 24, 27, 28, 29, 30, 31]
    timestamps = [int(datetime(2025, 1, d, 14, 30, tzinfo=timezone.utc).timestamp()) for d in dates]
    row = dict(meta=dict(symbol="TLT", currency="USD", instrumentType="ETF", exchangeTimezoneName="America/New_York"),
               timestamp=timestamps, indicators=dict(quote=[dict(open=[90.] * 18, close=[91.] * 18)]))
    raw = dict(chart=dict(result=[row], error=None))
    prices, decisions, count = normalize(json.dumps(raw).encode())
    assert count == 18 and len(list(csv.DictReader(io.StringIO(prices.decode())))) == 36
    assert b"2025-01-31T21:05:00Z" in decisions
    mutations = [lambda r: r.update(events={"dividends": {"x": dict(amount=.3)}}),
                 lambda r: r["meta"].update(currency="EUR"),
                 lambda r: r["timestamp"].pop(),
                 lambda r: r["indicators"]["quote"][0]["open"].__setitem__(0, None),
                 lambda r: r["timestamp"].__setitem__(0, r["timestamp"][0] + 60),
                 lambda r: r["timestamp"].__setitem__(0, r["timestamp"][1])]
    for mutation in mutations:
        modified = copy.deepcopy(raw)
        mutation(modified["chart"]["result"][0])
        try:
            normalize(json.dumps(modified).encode())
        except ValueError:
            continue
        raise AssertionError("Invalid vendor input accepted")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", type=Path, required=True)
    parser.add_argument("--data", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    simulator = args.simulator.resolve()
    importer_contract()
    with tempfile.TemporaryDirectory(prefix="rates-trading-") as temporary:
        root = Path(temporary)
        data = root / "data"
        data.mkdir()
        fixture(data)
        evidence = check_daily(simulator, data, root / "run", "synthetic")
        for options in (["--unknown"], [], ["--basis", "bogus"], ["--help", "--help"]):
            run = subprocess.run([str(simulator), *options], capture_output=True)
            assert run.returncode == (0 if "--help" in options else 1)
        # Strict parser rejects malformed numbers, revisions, unknown kinds and missing history.
        original = (data / "prices.csv").read_text()
        base = [str(simulator), "--prices", str(data / "prices.csv"), "--decisions", str(data / "decisions.csv"),
                "--out", str(root / "invalid"), "--symbol", "TEST", "--basis", "synthetic", "--evidence", "test"]
        for malformed in [original.replace(",open,", ",bad,", 1), original.replace("100.5", "nan", 1),
                          original.replace(",0\n", ",1\n", 1), "bad\n", original.replace("100.5", "100x", 1)]:
            (data / "prices.csv").write_text(malformed)
            assert subprocess.run(base, capture_output=True).returncode == 1
        (data / "prices.csv").write_text(original)
        for options in (["--lookback", "2.5"], ["--fee-bps", "-1"], ["--basis", "retrospective"],
                        ["--cash", "inf"], ["--latency-seconds", "-1"]):
            assert subprocess.run(base + options, capture_output=True).returncode == 1
        if args.data:
            if not args.out:
                parser.error("--data requires --out")
            evidence = check_daily(simulator, args.data, args.out, "retrospective")
            (args.out / "validation.json").write_text(json.dumps(evidence, indent=2) + "\n")
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(evidence))


if __name__ == "__main__":
    main()
