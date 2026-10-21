"""Auditable NY Fed SOFR capture and strict, provider-neutral decimal OIS quote import.

Snapshots are immutable, content addressed JSON files. Historical NY Fed responses
do not disclose publication timestamps: availability is first LOCAL capture, never
an invented historical release time. Hashes detect corruption, not source forgery.
"""
import argparse
import base64
from datetime import date, datetime, timedelta, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

NYFED = "https://markets.newyorkfed.org/api/rates/secured/sofr/search.json"
MAX_BYTES = 8 * 1024 * 1024
CONVENTIONS = {"payment_months": 12, "payment_lag_days": 2, "settlement_days": 2,
               "lookback_days": 0, "lockout_days": 0, "observation_shift": False,
               "end_of_month": False}


class DataError(ValueError):
    pass


def utc_now():
    return datetime.now(timezone.utc).replace(microsecond=0)


def stamp(value):
    return value.astimezone(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def timestamp(value):
    if not isinstance(value, str) or len(value) != 20 or not value.endswith("Z"):
        raise DataError("Timestamp must be UTC YYYY-MM-DDTHH:MM:SSZ")
    try:
        result = datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    except ValueError as error:
        raise DataError("Invalid UTC timestamp") from error
    if stamp(result) != value:
        raise DataError("Noncanonical UTC timestamp")
    return result


def day(value):
    try:
        result = date.fromisoformat(value)
    except (ValueError, TypeError) as error:
        raise DataError("Invalid ISO date") from error
    if result.isoformat() != value:
        raise DataError("Date must be YYYY-MM-DD")
    return result


def number(value, name):
    if type(value) not in (int, float) or not math.isfinite(value):
        raise DataError(f"{name} must be a finite number")
    return float(value)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()


def digest(data):
    return hashlib.sha256(data).hexdigest()


def decode(raw):
    if len(raw) > MAX_BYTES:
        raise DataError("Input exceeds size limit")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise DataError(f"Duplicate JSON key: {key}")
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(DataError(f"Invalid number {value}")))
    except (ValueError, UnicodeError) as error:
        raise DataError(f"Invalid JSON: {error}") from error


class Calendar:
    def __init__(self, document):
        if document.get("schema_version") != 1 or document.get("name") != "US-Government-Bond":
            raise DataError("Unsupported calendar")
        self.document = document
        self.start, self.end = day(document["start"]), day(document["end"])
        self.holidays = {day(value) for value in document["holidays"]}
        if self.start >= self.end or len(self.holidays) != len(document["holidays"]):
            raise DataError("Invalid calendar coverage or duplicate holidays")
        if any(d < self.start or d > self.end for d in self.holidays):
            raise DataError("Holiday outside calendar coverage")
        self.sha256 = digest(canonical(document))

    def business(self, d):
        if not self.start <= d <= self.end:
            raise DataError(f"Date {d} outside calendar coverage")
        return d.weekday() < 5 and d not in self.holidays

    def dates(self, start, end):
        if start > end or (end-start).days > 3660:
            raise DataError("Invalid date range (maximum ten years)")
        return {start+timedelta(days=i) for i in range((end-start).days+1)
                if self.business(start+timedelta(days=i))}


def export_calendar(start, end):
    import QuantLib as q
    cal = q.UnitedStates(q.UnitedStates.GovernmentBond)
    def qd(d):
        return q.Date(d.day, d.month, d.year)
    return dict(schema_version=1, name="US-Government-Bond", start=start.isoformat(), end=end.isoformat(),
                source=f"QuantLib {q.__version__} UnitedStates.GovernmentBond",
                holidays=[d.ISO() for d in cal.holidayList(qd(start), qd(end))])


def fetch(url, opener=urlopen, sleeper=time.sleep):
    """Bounded retries for transient failures; never retry malformed payloads or 4xx."""
    for attempt in range(3):
        try:
            with opener(Request(url, headers={"Accept": "application/json", "User-Agent": "rates-engine/2.2"}), timeout=15) as response:
                if response.geturl().split("?")[0] != NYFED:
                    raise DataError("Unexpected response origin/path")
                raw = response.read(MAX_BYTES+1)
                if len(raw) > MAX_BYTES:
                    raise DataError("Response exceeds size limit")
                return raw
        except (HTTPError, URLError, TimeoutError, OSError) as error:
            transient = not isinstance(error, HTTPError) or error.code in (429, 500, 502, 503, 504)
            if isinstance(error, HTTPError):
                error.close()
            if not transient or attempt == 2:
                raise DataError(f"NY Fed request failed: {error}") from error
            sleeper(2**attempt)


def write_snapshot(root, payload):
    """Publish complete bytes atomically; existing snapshots are never replaced."""
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    raw = canonical(payload)
    target = root / (digest(raw)+".json")
    if target.exists():
        if target.read_bytes() != raw:
            raise DataError("Existing snapshot is corrupt")
        return target
    descriptor, name = tempfile.mkstemp(prefix=".capture-", dir=root)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(raw)
            stream.flush()
            os.fsync(stream.fileno())
        # Hard-link publication is atomic and refuses to overwrite a concurrent writer.
        try:
            os.link(name, target)
        except FileExistsError:
            if target.read_bytes() != raw:
                raise DataError("Conflicting snapshot")
    finally:
        Path(name).unlink(missing_ok=True)
    return target


def read_snapshot(path, kind=None):
    path = Path(path)
    raw = path.read_bytes()
    if digest(raw) != path.stem:
        raise DataError("Snapshot content hash mismatch")
    payload = decode(raw)
    if payload.get("schema_version") != 1 or (kind and payload.get("kind") != kind):
        raise DataError("Unexpected snapshot schema/kind")
    original = base64.b64decode(payload["raw_base64"], validate=True)
    if digest(original) != payload["raw_sha256"]:
        raise DataError("Raw-source hash mismatch")
    timestamp(payload["received_at"])
    return payload


def envelope(kind, raw, received, **fields):
    return dict(schema_version=1, kind=kind, received_at=stamp(received), raw_sha256=digest(raw),
                raw_base64=base64.b64encode(raw).decode(), **fields)


def normalize_fixings(raw, start, end, calendar, received, previous=None):
    if end >= received.date():
        raise DataError("SOFR capture may only request completed historical effective dates")
    expected = calendar.dates(start, end)
    document = decode(raw)
    if not isinstance(document, dict) or not isinstance(document.get("refRates"), list):
        raise DataError("NY Fed response must contain refRates")
    current = {}
    for row in document["refRates"]:
        d = day(row["effectiveDate"])
        rate = number(row["percentRate"], "percentRate")/100
        if row.get("type") != "SOFR" or d not in expected or d in current or not -0.05 <= rate <= 0.25:
            raise DataError("Unexpected/duplicate fixing date, type or rate")
        current[d] = (rate, str(row.get("revisionIndicator", "")))
    if set(current) != expected or not current:
        raise DataError(f"Incomplete fixing response: missing {sorted(expected-set(current))}")
    records = []
    if previous:
        if previous["calendar_sha256"] != calendar.sha256 or previous["source"] != NYFED:
            raise DataError("Previous capture has incompatible calendar/source")
        if timestamp(previous["received_at"]) > received:
            raise DataError("Capture time predates previous snapshot")
        records = [dict(row) for row in previous["records"]]
    latest = {row["date"]: row for row in records}
    for d, (rate, indicator) in sorted(current.items()):
        old = latest.get(d.isoformat())
        if old and old["rate"] == rate:
            continue
        if old and timestamp(old["available_at"]) >= received:
            raise DataError("Revision requires a later capture timestamp")
        records.append(dict(date=d.isoformat(), rate=rate, available_at=stamp(received), source=NYFED,
                            revision=old["revision"]+1 if old else 0, provider_revision=indicator))
    records.sort(key=lambda row: (row["date"], row["revision"]))
    return envelope("sofr-fixings", raw, received, source=NYFED, calendar_sha256=calendar.sha256,
                    request_start=start.isoformat(), request_end=end.isoformat(), records=records,
                    previous_sha256=digest(canonical(previous)) if previous else None,
                    availability_policy="first-local-capture; API historical publication times unavailable")


def conventions(document):
    if set(document) != set(CONVENTIONS):
        raise DataError("All supported convention fields must be explicit; unknown fields rejected")
    for key in ("payment_months", "payment_lag_days", "settlement_days", "lookback_days", "lockout_days"):
        value = document[key]
        low, high = (1, 120) if key == "payment_months" else (0, 30)
        if type(value) is not int or not low <= value <= high:
            raise DataError(f"Invalid {key}")
    for key in ("observation_shift", "end_of_month"):
        if type(document[key]) is not bool:
            raise DataError(f"Invalid {key}")
    return document


def validate_quotes(document, calendar, policy, as_of):
    if (document.get("schema_version") != 1 or document.get("currency") != "USD" or
            document.get("index") != "SOFR" or document.get("instrument") != "OIS" or
            document.get("units") != "decimal"):
        raise DataError("Expected schema-1 USD SOFR OIS quotes in decimal units")
    if not isinstance(document.get("source"), str) or not document["source"].strip():
        raise DataError("Quote source is required")
    if type(document.get("synthetic")) is not bool:
        raise DataError("Explicit synthetic flag is required")
    if type(policy.get("allow_synthetic")) is not bool:
        raise DataError("Import policy requires a boolean allow_synthetic flag")
    if document["synthetic"] and not policy["allow_synthetic"]:
        raise DataError("Synthetic quotes disabled by import policy")
    if document.get("calendar_sha256") != calendar.sha256:
        raise DataError("Quote/calendar mismatch")
    conventions(document["conventions"])
    if document["conventions"] != policy["conventions"]:
        raise DataError("Quote conventions do not match import policy")
    observed, available = timestamp(document["observed_at"]), timestamp(document["available_at"])
    max_age = number(policy["max_age_seconds"], "max age")
    spread = number(policy["max_spread"], "max spread")
    low, high = number(policy["min_rate"], "min rate"), number(policy["max_rate"], "max rate")
    if max_age <= 0 or spread < 0 or low >= high:
        raise DataError("Invalid quote quality policy")
    if not observed <= available <= as_of or (as_of-observed).total_seconds() > max_age:
        raise DataError("Future, unavailable or stale quote snapshot")
    if not calendar.business(as_of.date()) or observed.date() != as_of.date():
        raise DataError("Quotes must be from the current valuation business date")
    required = policy["tenor_months"]
    if not required or any(type(m) is not int or not 1 <= m <= 600 for m in required) or len(set(required)) != len(required):
        raise DataError("Invalid required tenor universe")
    seen = set()
    labels = set()
    rows = document["quotes"]
    for row in rows:
        m = row["tenor_months"]
        label = row["label"]
        if type(m) is not int or m not in required or m in seen or not isinstance(label, str) or not label.strip() or label in labels:
            raise DataError("Unexpected/duplicate tenor or label")
        seen.add(m)
        labels.add(label)
        bid, mid, ask = [number(row[k], k) for k in ("bid", "rate", "ask")]
        if not low <= bid <= mid <= ask <= high or ask-bid > spread:
            raise DataError("Invalid quote bounds, crossed market or excessive spread")
    if seen != set(required):
        raise DataError("Incomplete quote tenor universe")
    return document


def import_quotes(raw, calendar, policy, received):
    document = validate_quotes(decode(raw), calendar, policy, received)
    return envelope("ois-quotes", raw, received, source=document["source"], document=document,
                    policy=policy, calendar_sha256=calendar.sha256)


def quotes_as_of(snapshot, calendar, as_of):
    if timestamp(snapshot["received_at"]) > as_of:
        raise DataError("Quote capture was unavailable at valuation time")
    return validate_quotes(snapshot["document"], calendar, snapshot["policy"], as_of)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    calendar_parser = sub.add_parser("calendar")
    calendar_parser.add_argument("--start", type=day, required=True)
    calendar_parser.add_argument("--end", type=day, required=True)
    calendar_parser.add_argument("--out", type=Path, required=True)
    for name in ("fixings", "quotes"):
        p = sub.add_parser(name)
        p.add_argument("--calendar", type=Path, required=True)
        p.add_argument("--out", type=Path, required=True)
        if name == "fixings":
            p.add_argument("--start", type=day, required=True)
            p.add_argument("--end", type=day, required=True)
            p.add_argument("--previous", type=Path)
        else:
            p.add_argument("--input", type=Path, required=True)
            p.add_argument("--policy", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "calendar":
            document = export_calendar(args.start, args.end)
            Calendar(document)
            args.out.write_bytes(canonical(document))
            print(args.out)
            return
        calendar = Calendar(decode(args.calendar.read_bytes()))
        if args.command == "fixings":
            calendar.dates(args.start, args.end)
            url = NYFED+"?"+urlencode({"startDate": args.start.isoformat(), "endDate": args.end.isoformat()})
            raw = fetch(url)
            payload = normalize_fixings(raw, args.start, args.end, calendar, utc_now(),
                                        read_snapshot(args.previous, "sofr-fixings") if args.previous else None)
            payload["request_url"] = url
        else:
            payload = import_quotes(args.input.read_bytes(), calendar, decode(args.policy.read_bytes()), utc_now())
        print(write_snapshot(args.out, payload))
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.exit(1, f"Market data rejected: {error}\n")


if __name__ == "__main__":
    main()
