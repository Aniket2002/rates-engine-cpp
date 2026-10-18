# Real historical rates: Federal Reserve GSW curves

The engine now uses **249 actual historical daily curve estimates covering
2025-01-02 through 2025-12-31**, each with ten annual zero-rate tenors. Source:
[Board of Governors of the Federal Reserve System, nominal yield curve](https://www.federalreserve.gov/data/nominal-yield-curve.htm).
These fitted off-the-run Treasury curves are based on historical Treasury coupon
security prices. They are a staff research product, subject to revisions. The
sample swaps remain hypothetical.

The original file was downloaded from the [official CSV endpoint](https://www.federalreserve.gov/data/yield-curve-tables/feds200628.csv).
The repository retains the selected original rows, all original columns, normalized
engine input and SHA-256 provenance in [data/fed-gsw](../data/fed-gsw).
The [Board's reuse policy](https://www.federalreserve.gov/disclaimer.htm) permits
copying and redistribution of public-domain Board information with attribution.
No external account or API key is needed. The repository code is MIT-licensed;
the data's source attribution and reuse status are separately documented.

## Conversion and interpretation

The importer reads **SVENY01 through SVENY10**, continuously compounded zero yields
in annual percent. It divides each value by 100. A tenor of `n` years becomes an
exact pillar at `valuation + 365*n days`, preserving the supplied year fraction
under the engine's ACT/365F clock. Zero pillars bypass business-day adjustment.
At a pillar, `DF = exp(-rate_decimal*n)`; the engine interpolates log discounts
between pillars. The first year interpolates from DF(0)=1, implying a constant
zero yield there. It does not reconstruct the full Svensson curve between the
published integer-year points. No values are treated as swap or deposit quotes.

`Instrument::ContinuousZero` and CSV type `continuous_zero` load these exact-date
pillars analytically. Existing deposit/swap bootstrapping is preserved. Ten quoted
zero rates require 23 curve builds for a complete scenario set, with no numerical
root solve for these zero inputs. The existing `parallel_quote_dv01` field now
means parallel **continuous-zero-yield DV01** for this dataset. Input-quote and
zero-node buckets coincide because each source quote directly specifies one node.
Their interpretation differs from the synthetic deposit/swap quote coordinates.

The 12 rows with all selected source tenors missing are recorded in the manifest
and excluded from replay. A partially missing curve fails import. Nothing is
forward-filled, interpolated across dates, or replaced with invented rates.
Duplicate dates, unknown compounding, non-finite values and changed file hashes
also fail. Synthetic fixtures remain available for controlled numerical tests.

## Time and model boundaries

The Fed describes weekly updates and possible historical revisions. The file
contains observation dates without the original availability times or vintages.
The importer attaches `23:59:59 UTC` as a **retrospective observation-date label**.
Every historical batch summary explicitly carries `data_source=federal-reserve-gsw`
and `replay_basis=retrospective_observation_date`; the manifest sets
`point_in_time=false` and `publication_time_known=false`.

The replay engine still selects no timestamp after its query, and rejects stale
or missing snapshots, but these checks cannot eliminate revision bias in the
downloaded vintage. Do not use this run as evidence of a historically executable
trading strategy or point-in-time data availability.

The 100-swap portfolio starts after the final observation date so no unknown
past coupon fixings are invented. It uses the existing weekend calendar and
default leg conventions. Projection and discounting both use the Treasury curve
as a research proxy, rather than claiming an actual OIS/IBOR swap valuation.
PV changes combine curve movements and the passage of time; they are not realized
trading P&L or a hedged strategy return.

## Run and refresh

After the usual Release build, replay the pinned data with no network dependency:

```sh
python python/replay_fed_history.py --batch build/release/rates_batch --data data/fed-gsw --out results/fed-replay
```

Append `.exe` on Windows. To download and convert the latest source vintage into
a separate directory, then use it:

```sh
python python/import_fed_history.py --start 2025-01-01 --end 2025-12-31 --out results/fed-new-vintage
python python/replay_fed_history.py --batch build/release/rates_batch --data results/fed-new-vintage --out results/fed-new-replay
```

For an offline reconstruction from the retained original rows:

```sh
python python/import_fed_history.py --source-file data/fed-gsw/source-excerpt.csv --out results/fed-reconstructed
```

The normalized files reproduce exactly; provenance identifies the supplied local
input hash rather than claiming that the excerpt is the entire source download.
The replay wrapper visits only complete observation dates, sets maximum data age
to zero, and invokes the C++ batch executable for each date. It retains per-trade
and aggregate output, `replay.jsonl`, and a summary. Schedules/scenarios are shared
within each batch; each date is a fresh process, not an ingestion throughput benchmark.
The scheduled operations workflow runs this real-data replay in addition to the
synthetic profiling workload, using the pinned dataset rather than silently updating it.

## Measured historical run

The complete run priced **24,900 trade/date combinations** (249 dates × 100 swaps),
with **zero failed trades**. Aggregate PV ranged from -35,632.63 to 455,755.22 in
the model's currency units; first/last PVs were 345,774.98 and 25,476.71.
First/last parallel zero-yield DV01s were -4,207.52 and -4,560.95 currency/bp.
See the [summary](results/fed-replay-summary.json) and [daily event log](results/fed-replay.jsonl).

Offline tests reconstruct the import from original rows, verify hashes, reject
missing/non-finite inputs, compare exact pillar discounts with `exp(-z*t)`, and
check portfolio PV plus every risk bucket against an independent Python curve.
QuantLib independently checks selected trade PVs and parallel risk from the source
zero rates. Both frontends are exercised, including stale-holiday and pre-observation
rejection. CI requires the QuantLib comparison on all four platform builds.

Local Release passed **53 CTest entries** after this integration. The added
source-based validation passed 114 comparisons on the first, middle and last
observations. Maximum portfolio PV error was 3.50e-10, maximum bucket/parallel
risk error 5.77e-10, and maximum QuantLib error 1.17e-10 in model currency units
(risk uses currency/bp). See [validation results](results/fed-validation.json).
