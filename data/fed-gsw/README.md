# Real Federal Reserve historical curve data

Source: **Board of Governors of the Federal Reserve System (US)**,
[Gurkaynak–Sack–Wright nominal Treasury yield-curve dataset](https://www.federalreserve.gov/data/nominal-yield-curve.htm).
No endorsement by the Board is implied.

Under the Board's [copyright policy](https://www.federalreserve.gov/disclaimer.htm),
Board information is public domain unless otherwise indicated and may be copied
and distributed with source attribution. The repository's MIT license applies
to its code; these data retain their source attribution and public-domain status.

- `source-excerpt.csv`: original columns and selected 2025 rows, including NA rows;
  line endings normalized to LF. The full download hash is retained in the manifest.
- `quote_history.csv`: 249 complete curves × 10 tenors, using actual source values
  divided by 100, with type `continuous_zero`.
- `portfolio.csv`: 100 **hypothetical** forward-starting swaps for demonstration.
- `manifest.json`: source, retrieval metadata, hashes, units, missing dates,
  conversion and replay assumptions.

The dataset contains fitted estimates based on historical Treasury prices.
SVENY01..SVENY10 are continuous annual zero yields. These are not deposit or
interest-rate-swap market quotes. The engine's single Treasury curve is a proxy
for valuing the sample swaps; it is not a collateralized multi-curve swap model.

`23:59:59Z` is a generated label for each observation date, **not the original
publication time**. The Fed normally publishes updates weekly and can revise
earlier estimates. This pinned download supports retrospective analysis, not a
claim of point-in-time backtesting. Never relabel it as historical executable quotes.

Full commands and results: [historical-data guide](../../docs/real-historical-data.md).
