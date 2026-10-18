"""Import real Fed GSW fitted Treasury zero rates, retaining source and provenance.

SVENY01..SVENY10 are continuous annual percent yields. No swap/deposit quote
conversion, missing-value filling, or assumption of historical publication time.
"""
import argparse
import csv
from datetime import date, datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation
import hashlib
import io
import json
from pathlib import Path
from urllib.request import Request, urlopen

URL = "https://www.federalreserve.gov/data/yield-curve-tables/feds200628.csv"
SOURCE = "https://www.federalreserve.gov/data/nominal-yield-curve.htm"
TERMS = "https://www.federalreserve.gov/disclaimer.htm"
SERIES = [f"SVENY{n:02}" for n in range(1, 11)]


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def parse_source(data, start, end):
    lines = data.decode("utf-8-sig").splitlines()
    header = next((i for i, line in enumerate(lines) if line.startswith("Date,")), None)
    if header is None or "Zero-coupon yield,Continuously Compounded,SVENYXX" not in lines[:header]:
        raise ValueError("Missing Fed header or continuous zero-rate convention")
    reader = csv.DictReader(io.StringIO("\n".join(lines[header:])))
    if not set(["Date", *SERIES]).issubset(reader.fieldnames or []):
        raise ValueError("Missing required Fed series")
    selected, observations, missing = [], [], []
    seen = set()
    for line, row in zip(lines[header + 1:], reader, strict=True):
        day = date.fromisoformat(row["Date"])
        if not start <= day <= end:
            continue
        if day in seen:
            raise ValueError(f"Duplicate observation date {day}")
        seen.add(day)
        selected.append(line)
        values = [row[s] for s in SERIES]
        absent = [value in ("NA", "", "N/A") for value in values]
        if all(absent):
            missing.append(str(day))
            continue
        if any(absent):
            raise ValueError(f"Partially missing curve {day}; refusing to fill it")
        try:
            rates = [Decimal(value) / 100 for value in values]
        except InvalidOperation as error:
            raise ValueError(f"Invalid source rate on {day}") from error
        if any(not value.is_finite() for value in rates):
            raise ValueError(f"Non-finite source rate on {day}")
        observations.append((day, rates))
    if not observations:
        raise ValueError("No complete observations in requested range")
    if observations != sorted(observations):
        raise ValueError("Source dates are not chronological")
    # Keep all source columns and missing rows for the selected date range.
    excerpt = ("\n".join(lines[:header + 1] + selected) + "\n").encode()
    return observations, missing, excerpt


def import_history(data, output, start, end, trades=100, retrieved_at=None, origin=URL):
    if start > end or not 1 <= trades <= 100000:
        raise ValueError("Ordered date range and 1..100000 trades required")
    observations, missing, excerpt = parse_source(data, start, end)
    output.mkdir(parents=True, exist_ok=True)
    (output / "source-excerpt.csv").write_bytes(excerpt)
    with (output / "quote_history.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["timestamp", "valuation_date", "type", "label", "maturity", "rate"])
        for day, rates in observations:
            for years, rate in enumerate(rates, 1):
                # Exact ACT/365F times preserve the provider's integer-year horizons.
                writer.writerow([f"{day}T23:59:59Z", day, "continuous_zero", SERIES[years - 1],
                                 day + timedelta(days=365 * years), str(rate)])
    effective = observations[-1][0] + timedelta(days=1)
    while effective.weekday() >= 5:
        effective += timedelta(days=1)
    # Hypothetical fixed portfolio, starting after the entire replay window.
    # This avoids inventing historical coupon fixings.
    from reference_pricer import add_months
    if add_months(effective, 72) + timedelta(days=3) > observations[0][0] + timedelta(days=3650):
        raise ValueError("Requested span too wide for this fixed forward-starting portfolio")
    with (output / "portfolio.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["id", "effective", "maturity", "notional", "fixed_rate", "side"])
        for i in range(trades):
            writer.writerow([f"hypothetical-{i:05}", effective, add_months(effective, 24 + 12 * (i % 5)),
                             100000 * (1 + i % 7), str(Decimal("0.035") + Decimal("0.0002") * (i % 9)),
                             "receiver" if i % 3 == 0 else "payer"])
    manifest = dict(
        schema_version=1, provider="Board of Governors of the Federal Reserve System",
        dataset="Gurkaynak-Sack-Wright fitted nominal Treasury zero-coupon yields",
        source_url=URL, methodology_url=SOURCE, reuse_terms_url=TERMS,
        attribution="Source: Board of Governors of the Federal Reserve System (US). No endorsement implied.",
        reuse="Public-domain Board information unless otherwise indicated; source attribution retained.",
        input_origin=origin, input_sha256=sha256(data),
        input_bytes=len(data),
        retrieval_time_note="For a local input, retrieved_at_utc records file modification time; original publication time is unknown.",
        retrieved_at_utc=retrieved_at or datetime.now(timezone.utc).isoformat(),
        requested_start=str(start), requested_end=str(end), first_observation=str(observations[0][0]),
        last_observation=str(observations[-1][0]), observations=len(observations),
        source_missing_dates=missing, series=SERIES, input_units="percent per annum, continuously compounded",
        conversion="decimal=percent/100; exact maturity=valuation+365*tenor_years days; DF=exp(-decimal*tenor_years)",
        interpolation="engine log-linear discount factors; anchor DF(0)=1; first year uses constant zero rate",
        timestamp_basis="retrospective_observation_date", timestamp_rule="observation date at 23:59:59 UTC, a replay label only",
        point_in_time=False, publication_time_known=False,
        vintage_warning="Current downloaded vintage; Fed normally updates weekly and may revise history. These timestamps are not historical availability times.",
        portfolio="hypothetical forward-starting vanilla swaps; Treasury single-curve valuation proxy, not executable swap quotes",
        trades=trades, portfolio_effective=str(effective),
        files={name: sha256((output / name).read_bytes()) for name in
               ("source-excerpt.csv", "quote_history.csv", "portfolio.csv")})
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-file", type=Path, help="Use a pinned local Fed CSV instead of downloading")
    parser.add_argument("--out", type=Path, default=Path("data/fed-gsw"))
    parser.add_argument("--start", type=date.fromisoformat, default=date(2025, 1, 1))
    parser.add_argument("--end", type=date.fromisoformat, default=date(2025, 12, 31))
    parser.add_argument("--trades", type=int, default=100)
    args = parser.parse_args()
    if args.source_file:
        data = args.source_file.read_bytes()
    else:
        request = Request(URL, headers={"User-Agent": "rates-engine-cpp historical research importer"})
        with urlopen(request, timeout=90) as response:
            data = response.read(50_000_001)
        if len(data) > 50_000_000:
            raise ValueError("Source exceeds 50 MB import limit")
    retrieved = (datetime.fromtimestamp(args.source_file.stat().st_mtime, timezone.utc).isoformat()
                 if args.source_file else datetime.now(timezone.utc).isoformat())
    report = import_history(data, args.out, args.start, args.end, args.trades, retrieved_at=retrieved,
                            origin="local pinned CSV" if args.source_file else URL)
    print(json.dumps({k: report[k] for k in ("observations", "first_observation", "last_observation", "trades", "point_in_time")}))


if __name__ == "__main__":
    main()
