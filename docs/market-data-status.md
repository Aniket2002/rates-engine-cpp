# Market realism and firm quote ingestion

The remaining external-data gap is explicit: this repository does not contain
independently sourced executable OIS or basis quotes. No institutional feed was
purchased and no indicative or synthetic number has been relabeled as executable.

The [NY Fed SOFR publication](https://www.newyorkfed.org/markets/reference-rates/sofr)
is a transaction-based overnight reference rate. It is useful for realized
fixings; it is not an executable term-swap bid/ask curve. The repository's Fed
Treasury curves are fitted historical proxies. Existing SOFR valuation combines
captured public fixings with clearly labeled synthetic OIS quotes. The new hedge
study uses synthetic quotes and assumed costs throughout.

The existing provider-neutral JSON importer validates units, complete tenors,
calendar/conventions, timestamps, spread bounds and content hashes. To import
authorized firm OIS data, use [firm-policy.json](../data/sofr/firm-policy.json).
It disables synthetic data and adds:

| Field | Requirement |
| --- | --- |
| `quote_type` | `firm` |
| `venue` | Nonempty provider-declared execution venue |
| `source_reference` | Reference to the original provider record |
| `usage_rights_reference` | Reference identifying applicable usage rights |
| `valid_until` | UTC timestamp not earlier than valuation |
| Each quote's `executable_notional` | At least the policy's `required_notional` |

These fields supplement the existing schema; bid/ask/rate, source, timestamp,
calendar hash and convention fields remain mandatory. Sizes are USD notionals.
The sample policy requires USD 1 million. Tailor the required tenors and freshness
limits to the actual licensed source and intended trade size. Do not paste secret
credentials into the references or commit restricted source data.

```powershell
.\.venv\Scripts\python.exe python/sofr_market_data.py quotes --input provider-quotes.json --calendar data/sofr/calendar.json --policy data/sofr/firm-policy.json --out results/firm-quotes
```

The importer can validate these assertions but cannot independently authenticate
the source, enforce a venue's credit rules or guarantee a fill. The provider may
withdraw a quote before use. No prepopulated "real" quote file is supplied.
To close the empirical gap, supply authorized records, retain source evidence,
reproduce the valuation as of their availability, and report actual coverage and
execution conditions. Until then, describe the study as synthetic.
