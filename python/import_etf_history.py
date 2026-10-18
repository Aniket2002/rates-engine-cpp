"""Download a limited, action-free TLT interval for RETROSPECTIVE simulation.

No original publication or receipt timestamps are supplied by this endpoint.
The generated times are explicit modeling assumptions, NOT point-in-time evidence.
Raw vendor prices stay in the ignored results directory; they are not relicensed.
"""
import argparse
import csv
from datetime import date, datetime, timedelta, timezone
import hashlib
import io
import json
import math
from pathlib import Path
import urllib.request

START, END = date(2025, 1, 6), date(2025, 2, 1)  # end exclusive
URL = ("https://query1.finance.yahoo.com/v8/finance/chart/TLT?"
       "period1=1736121600&period2=1738368000&interval=1d&events=div%2Csplits%2CcapitalGains")


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def normalize(raw):
    payload = json.loads(raw)
    if payload["chart"].get("error"):
        raise ValueError("Vendor returned an error")
    results = payload["chart"]["result"]
    if len(results) != 1:
        raise ValueError("Expected one instrument")
    data = results[0]
    meta = data["meta"]
    if (meta["symbol"], meta["currency"], meta["instrumentType"], meta["exchangeTimezoneName"]) != (
            "TLT", "USD", "ETF", "America/New_York"):
        raise ValueError("Unexpected instrument, currency or timezone")
    if any(data.get("events", {}).values()):
        raise ValueError("Corporate actions present: price-only accounting cannot process this interval")
    timestamps = data["timestamp"]
    quotes = data["indicators"]["quote"][0]
    if any(len(quotes[key]) != len(timestamps) for key in ("open", "close")):
        raise ValueError("Mismatched price arrays")
    # This importer deliberately supports ONLY this fixed interval, with no early
    # closes or DST transitions. Jan 9 (Carter mourning) and Jan 20 were closed.
    expected = []
    day = START
    while day < END:
        if day.weekday() < 5 and day not in {date(2025, 1, 9), date(2025, 1, 20)}:
            expected.append(day)
        day += timedelta(days=1)
    dates = [datetime.fromtimestamp(t, timezone.utc).date() for t in timestamps]
    if dates != expected:
        raise ValueError("Missing, repeated, unordered or unexpected trading sessions")
    prices = io.StringIO(newline="")
    decisions = io.StringIO(newline="")
    writer = csv.writer(prices, lineterminator="\n")
    writer.writerow(["event_time", "available_at", "kind", "price", "revision"])
    schedule = csv.writer(decisions, lineterminator="\n")
    schedule.writerow(["decision_time"])
    for i, day in enumerate(dates):
        if datetime.fromtimestamp(timestamps[i], timezone.utc).strftime("%H:%M:%S") != "14:30:00":
            raise ValueError("Unexpected daily-bar timestamp")
        for kind, clock in (("open", "14:30"), ("close", "21:00")):
            price = quotes[kind][i]
            if isinstance(price, bool) or not isinstance(price, (float, int)) or not math.isfinite(price) or price <= 0:
                raise ValueError("Missing or invalid unadjusted price")
            writer.writerow([f"{day}T{clock}:00Z", f"{day}T{clock}:01Z", kind, repr(price), 0])
        schedule.writerow([f"{day}T21:05:00Z"])
    return prices.getvalue().encode(), decisions.getvalue().encode(), len(dates)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("results/tlt-data"))
    parser.add_argument("--source-file", type=Path)
    args = parser.parse_args()
    if args.out.exists():
        raise ValueError("Choose a fresh output directory to preserve the downloaded vintage")
    if args.source_file:
        raw = args.source_file.read_bytes()
    else:
        request = urllib.request.Request(URL, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(request, timeout=60) as response:
            raw = response.read(2_000_001)
        if len(raw) > 2_000_000:
            raise ValueError("Unexpectedly large response")
    prices, decisions, count = normalize(raw)
    args.out.mkdir(parents=True)
    for name, content in (("source.json", raw), ("prices.csv", prices), ("decisions.csv", decisions)):
        (args.out / name).write_bytes(content)
    manifest = dict(
        symbol="TLT", provider="Yahoo Finance", source_url=URL,
        source_page="https://finance.yahoo.com/quote/TLT/history/",
        retrieved_at_utc=datetime.now(timezone.utc).isoformat(),
        retrieval_note="Time this import ran; source-file mode may use an earlier downloaded vintage",
        availability_basis="retrospective", point_in_time_certified=False,
        publication_time_known=False, first_session=str(START), last_session="2025-01-31",
        session_count=count, corporate_actions_reported=0,
        price_basis="quote.open and quote.close, not dividend-adjusted adjclose",
        timestamp_assumption="14:30 UTC open, 21:00 UTC close, available one second later; NOT original availability",
        calendar_note="Fixed January 2025 interval only: Jan 9 and Jan 20 excluded; no early closes or DST changes",
        return_basis="price_return_action_free_interval",
        reuse_note="Vendor data is not covered by this repository's MIT license. No redistribution permission claimed; keep downloaded files local and review Yahoo terms for intended use.",
        hashes={"source.json": sha(raw), "prices.csv": sha(prices), "decisions.csv": sha(decisions)})
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(dict(sessions=count, source_sha256=sha(raw), output=str(args.out),
                          availability_basis="retrospective")))


if __name__ == "__main__":
    main()
