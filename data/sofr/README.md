# Public SOFR fixing capture and synthetic quote fixture

`manifest.json` replays a seasoned USD SOFR payer swap using actual public New
York Fed fixing observations captured on 21 October 2026. `fixings/` retains
the exact API response (base64), its SHA-256, request URL, local receipt time,
normalized decimal rates and first-local-capture availability. The requested
effective-date interval is 1 September through 20 October 2026.

**All OIS quotes in this directory are synthetic.** `example-quotes.json` is
an import-schema example, not a market curve. `example-policy.json` explicitly
allows synthetic inputs; set `allow_synthetic` to false for externally supplied
market quotes. Its observation timestamp describes fixture creation only.
`quotes/` contains the immutable imported snapshot selected by the manifest.

`calendar.json` records QuantLib's US Government Bond holidays and version for
2018–2040. The import and replay enforce this finite coverage. Calendar updates
require a new capture/import with matching hashes and operator review.

Snapshots use their SHA-256 as filenames. Do not rename or edit them. Hashes
detect changes; they are not cryptographic proof of the publisher's identity.
Historic NY Fed API rows do not supply exact original publication timestamps.
These fixtures therefore become available only at their actual local capture
time and must not be backdated for point-in-time trading tests.

Run from the repository root after building the Python bindings:

```sh
python python/run_sofr_valuation.py --module-dir build/release --manifest data/sofr/manifest.json --out results/sofr-demo
python python/test_sofr_replay.py --module-dir build/release --out results/sofr-replay --require-quantlib
```

Source: [New York Fed SOFR](https://www.newyorkfed.org/markets/reference-rates/sofr),
[API documentation](https://markets.newyorkfed.org/static/docs/markets-api.html).
See [the lifecycle and data guide](../../docs/sofr-lifecycle.md) for conventions,
current capture commands, rejection rules and remaining boundaries.
