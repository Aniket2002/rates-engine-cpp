"""Offline tests of capture, revision, integrity and quote-quality failure paths."""
from copy import deepcopy
from datetime import date, timedelta
import io
from pathlib import Path
import tempfile
import unittest
from urllib.error import HTTPError, URLError

import sofr_market_data as m


class MarketDataTests(unittest.TestCase):
    def setUp(self):
        self.calendar = m.Calendar(dict(schema_version=1, name="US-Government-Bond", source="test",
                                       start="2025-01-01", end="2026-12-31", holidays=["2025-01-01"]))
        self.now = m.timestamp("2025-01-06T16:00:00Z")
        self.start, self.end = date(2025, 1, 2), date(2025, 1, 3)
        self.raw = m.canonical({"refRates": [dict(effectiveDate=d, type="SOFR", percentRate=4.3)
                                           for d in ("2025-01-02", "2025-01-03")]})
        self.policy = dict(tenor_months=[1, 12], max_age_seconds=3600, max_spread=0.001,
                           min_rate=-0.05, max_rate=0.25, allow_synthetic=False, conventions=m.CONVENTIONS)
        self.quotes = dict(schema_version=1, currency="USD", index="SOFR", instrument="OIS", units="decimal",
                           source="test-provider", synthetic=False, calendar_sha256=self.calendar.sha256,
                           observed_at="2025-01-06T15:55:00Z", available_at="2025-01-06T15:56:00Z",
                           conventions=m.CONVENTIONS,
                           quotes=[dict(label=f"{n}M", tenor_months=n, bid=.0399, rate=.04, ask=.0401) for n in (1, 12)])

    def capture(self, raw=None, previous=None, now=None):
        return m.normalize_fixings(raw or self.raw, self.start, self.end, self.calendar, now or self.now, previous)

    def test_fixing_units_capture_time_and_revisions(self):
        first = self.capture()
        self.assertAlmostEqual(first["records"][0]["rate"], .043)
        self.assertEqual(first["records"][0]["available_at"], m.stamp(self.now))
        later = self.now+timedelta(hours=1)
        repeated = self.capture(previous=first, now=later)
        self.assertEqual(repeated["records"], first["records"])
        raw = m.decode(self.raw)
        raw["refRates"][0]["percentRate"] = 4.31
        revised = self.capture(m.canonical(raw), first, later)
        self.assertEqual(len(revised["records"]), 3)
        self.assertEqual(revised["records"][1]["revision"], 1)
        self.assertEqual(revised["records"][1]["available_at"], m.stamp(later))
        with self.assertRaises(m.DataError):
            self.capture(m.canonical(raw), first)

    def test_incomplete_duplicate_wrong_type_and_out_of_range_fixings(self):
        for change in (lambda x: x["refRates"].pop(),
                       lambda x: x["refRates"].append(x["refRates"][0]),
                       lambda x: x["refRates"][0].update(type="EFFR"),
                       lambda x: x["refRates"][0].update(percentRate=100),
                       lambda x: x["refRates"][0].update(effectiveDate="2025-01-04")):
            doc = m.decode(self.raw)
            change(doc)
            with self.assertRaises(m.DataError):
                self.capture(m.canonical(doc))
        with self.assertRaises(m.DataError):
            self.capture(now=m.timestamp("2025-01-03T16:00:00Z"))
        with self.assertRaises(m.DataError):
            self.calendar.business(date(2030, 1, 1))

    def test_snapshot_atomic_idempotent_integrity(self):
        with tempfile.TemporaryDirectory() as directory:
            payload = self.capture()
            path = m.write_snapshot(directory, payload)
            self.assertEqual(m.read_snapshot(path, "sofr-fixings"), payload)
            self.assertEqual(m.write_snapshot(directory, payload), path)
            path.write_bytes(path.read_bytes()+b" ")
            with self.assertRaises(m.DataError):
                m.read_snapshot(path)
            with self.assertRaises(m.DataError):
                m.write_snapshot(directory, payload)
            self.assertEqual(len(list(Path(directory).iterdir())), 1)

    def test_quote_quality_and_as_of_controls(self):
        snapshot = m.import_quotes(m.canonical(self.quotes), self.calendar, self.policy, self.now)
        self.assertEqual(m.quotes_as_of(snapshot, self.calendar, self.now), self.quotes)
        for asof in (self.now-timedelta(seconds=1), self.now+timedelta(hours=2)):
            with self.assertRaises(m.DataError):
                m.quotes_as_of(snapshot, self.calendar, asof)
        changes = [lambda x: x.update(units="percent"), lambda x: x.update(synthetic=True),
                   lambda x: x.update(calendar_sha256="bad"), lambda x: x.update(source=""),
                   lambda x: x.update(available_at="2025-01-06T17:00:00Z"),
                   lambda x: x.update(observed_at="2025-01-06T14:00:00Z"),
                   lambda x: x["quotes"].pop(), lambda x: x["quotes"].append(x["quotes"][0]),
                   lambda x: x["quotes"][0].update(bid=.05),
                   lambda x: x["quotes"][0].update(ask=.06),
                   lambda x: x["conventions"].update(lookback_days=2)]
        for change in changes:
            document = deepcopy(self.quotes)
            change(document)
            with self.assertRaises(m.DataError):
                m.import_quotes(m.canonical(document), self.calendar, self.policy, self.now)

    def test_strict_json_and_timestamps(self):
        policy = {**self.policy, "allow_synthetic": "false"}
        with self.assertRaises(m.DataError):
            m.import_quotes(m.canonical(self.quotes), self.calendar, policy, self.now)
        for raw in (b'{"a":1,"a":2}', b'{"a":NaN}', b'not-json'):
            with self.assertRaises(m.DataError):
                m.decode(raw)
        for value in ("2025-01-06T16:00:00", "2025-01-06T16:00:00+01:00", "2025-1-06T16:00:00Z"):
            with self.assertRaises(m.DataError):
                m.timestamp(value)

    def test_firm_quote_evidence_and_size(self):
        policy = {**self.policy, "require_executable": True, "required_notional": 1e6}
        document = deepcopy(self.quotes)
        document.update(quote_type="firm", venue="test-only-venue", source_reference="test-record-1",
                        usage_rights_reference="test-entitlement", valid_until="2025-01-06T16:01:00Z")
        for row in document["quotes"]:
            row["executable_notional"] = 2e6
        m.import_quotes(m.canonical(document), self.calendar, policy, self.now)
        for change in [lambda x: x.update(quote_type="indicative"), lambda x: x.update(synthetic=True),
                       lambda x: x.update(valid_until="2025-01-06T15:59:59Z"),
                       lambda x: x.update(source_reference=""), lambda x: x.update(usage_rights_reference=""),
                       lambda x: x["quotes"][0].update(executable_notional=999999)]:
            changed = deepcopy(document)
            change(changed)
            with self.assertRaises(m.DataError):
                m.import_quotes(m.canonical(changed), self.calendar, policy, self.now)
        with self.assertRaises(m.DataError):
            m.import_quotes(m.canonical(document), self.calendar, {**policy, "require_executable": "yes"}, self.now)

    def test_transport_retries_and_permanent_failures(self):
        attempts, waits = [], []
        class Response(io.BytesIO):
            def geturl(self):
                return m.NYFED
        def recover(request, timeout):
            self.assertEqual(timeout, 15)
            attempts.append(request)
            if len(attempts) < 3:
                raise URLError("temporary")
            return Response(b'{"refRates":[]}')
        self.assertEqual(m.fetch(m.NYFED, recover, waits.append), b'{"refRates":[]}')
        self.assertEqual(waits, [1, 2])
        def denied(request, timeout):
            raise HTTPError(m.NYFED, 403, "denied", {}, None)
        with self.assertRaises(m.DataError):
            m.fetch(m.NYFED, denied, lambda _: self.fail("must not retry 403"))


if __name__ == "__main__":
    unittest.main()
