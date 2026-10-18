# Historical ETF trading simulation

`rates_simulate` adds a separate C++ long/cash ETF simulator to the existing rates
library. It accepts a versioned price tape and explicit decision times, produces
orders and simulated fills, and accounts for cash, holdings, dollar exposure,
P&L, turnover, drawdown and period-return volatility. It does not use Federal
Reserve fitted yields as executable prices.

The engine enforces the supplied availability times. **The public TLT example is
retrospective, not a certified point-in-time backtest.** A later download does
not establish the original publication time, historical vintage or actual
fillability of a price. Adding a lag cannot supply that missing evidence.

## Data and decisions

`include/rates/trading.hpp` exposes `PriceTape`, `SimulationConfig` and `simulate`.
There is one instrument and one USD cash account per simulation. Price inputs
must be positive, finite, in chronological **availability** order, and use UTC
seconds. The input contract requires prices in consistent, unadjusted share units.

```csv
event_time,available_at,kind,price,revision
2025-01-06T14:30:00Z,2025-01-06T14:30:01Z,open,100,0
2025-01-06T21:00:00Z,2025-01-06T21:00:01Z,close,101,0
2025-01-06T21:00:00Z,2025-01-07T12:00:00Z,close,100.9,1
```

These rows are **synthetic schema examples**. Each `(event_time, kind)` starts
at revision zero; subsequent revisions must be contiguous and have strictly
increasing availability times. A revision changes the known value only after
its availability time and does not add a new observation to the moving average.
Availability cannot precede the market event. The index retains every version.

Signals use only closes with `available_at < decision_time`, strictly excluding
equal timestamps. The most recent version known at the decision is selected
for each close. Future prices and future corrections are inaccessible to the
strategy. A correction's arrival does not reset the age of the original close.
No known close, a stale latest close, malformed data or numerical overflow fails
the run. A short lookback history is a warmup period with no order.

The decision file has one `decision_time` column, with strictly increasing UTC
timestamps. It defines the evaluation calendar; the library does not infer
holidays or feed completeness. The public-data importer separately verifies
every expected session for its supported interval. General callers must validate
their own session universe, corporate actions and provenance.

## Strategy and execution

The transparent strategy enters a long position when the latest known close is
above the mean of the last `lookback` closes (including that close), and exits
to cash otherwise. It does not rebalance an existing long position. Parameters
are fixed before the run; the example is an accounting demonstration, not a
parameter search or evidence of a profitable strategy.

An entry order's whole-share quantity is fixed at decision time, using current
cash and the known close including modeled costs. An exit requests all holdings.
Execution uses the next eligible **original open** whose event time is strictly
after decision time plus configured latency. Close observations and revisions
cannot fill orders. Old events received after a decision cannot be used as a
future execution opportunity. Excessively delayed open reports are ignored.

Buy price = reported open × `(1 + half_spread_bps / 10000)`; sell price uses
`1 - half_spread_bps / 10000`. Fees are `fee_bps / 10000` times absolute executed
notional. A gap up may reduce a buy to the affordable whole-share quantity; its
remainder is cancelled. An unaffordable order gets no fill. This affordability
rule is an explicit simulation assumption, not a broker guarantee.

Orders expire after their configured TTL, or are replaced at the next decision
if still pending. Replacements use the new signal. Orders remaining at the last
decision are recorded as `unfilled_end`; no price beyond the simulation horizon
is consumed. Open prices are historical observations used in an execution model,
not firm bid/ask quotes. There is no queue, auction participation, available-size
model, market impact or partial fill based on liquidity. Fills are accounted
when their reports become available; both event and availability times are saved.

## Accounting and risk

- Cash changes by signed execution notional plus fees; holdings by signed shares.
  Positions cannot be short or financed by borrowing. Sub-`1e-8` USD negative
  floating-point residue is clamped to zero; larger deficits fail.
- Equity is cash plus shares × latest known close. Dollar exposure is that stock
  value; no ETF duration or yield DV01 is inferred from price data.
- P&L is equity minus initial cash. There is no terminal forced liquidation.
  Costs of a potential final liquidation are therefore not included.
- Drawdown is `1 - equity / running_peak`, including initial capital in the peak.
  Maximum drawdown is sampled at decision times, not intraday.
- Period return is consecutive sampled equity ratio minus one. Volatility is
  sample standard deviation of returns between observations; it is zero with
  fewer than two returns. It is **not annualized**; irregular decision spacing
  must not be interpreted as daily or annual volatility.
- Turnover is gross absolute executed notional divided by initial capital. It
  is neither annualized nor the half-turnover convention.

There is no cash interest, settlement restriction, tax, dividend entitlement,
split handling, borrow, financing or multi-asset allocation. **Use only intervals
without corporate actions.** The importer rejects reported dividends, splits
and capital-gain distributions. Expanding to longer ETF histories requires
explicit action accounting and original availability evidence, not adjusted-close
execution prices or a fixed lag applied to a revised historical file.

## Public TLT demonstration

The importer retrieves [Yahoo Finance TLT historical data](https://finance.yahoo.com/quote/TLT/history/)
through its public chart endpoint, saving the original response, normalized files
and SHA-256 hashes locally. It selects `quote.open` and `quote.close`, not
`adjclose`. Yahoo's [adjusted-close documentation](https://in.help.yahoo.com/kb/adjusted-close-sln28256.html)
describes dividend and split adjustments. Even the non-dividend-adjusted vendor
fields are a later vintage and are not certified original as-traded prices.

The deliberately limited interval is **January 6–31, 2025: 18 sessions**. It has
no corporate actions reported by the endpoint. The calendar includes the
[January 9 Carter mourning closure](https://ir.nasdaq.com/news-releases/news-release-details/nasdaq-announces-closure-its-us-markets-honor-national-day-0)
and January 20 holiday. The importer supports only this fixed winter interval;
it does not generalize fixed UTC hours across DST or early closes. It checks the
vendor's daily timestamps against 14:30 UTC and labels the close at 21:00 UTC,
consistent with [Nasdaq's regular hours](https://www.nasdaq.com/market-activity/stock-market-holiday-schedule).
Availability one second after each event is a **retrospective assumption**, not
historical evidence. Decisions occur at 21:05 UTC.

Downloaded vendor data stay under ignored `results/`. The repository publishes
the importer, hashes and derived run summary, not a redistributed price archive.
The code's MIT license does not license vendor data; consult the
[provider's terms](https://legal.yahoo.com/us/en/yahoo/terms/otos/index.html)
for your intended use. No redistribution permission is claimed. The endpoint
may change, rate-limit or stop serving a request; a network failure is surfaced.
`--source-file` can replay your saved JSON without a new download.

```sh
cmake --preset release
cmake --build --preset release --parallel 4
python python/import_etf_history.py --out results/tlt-data
python python/test_trading.py --simulator build/release/rates_simulate --data results/tlt-data --out results/tlt-simulation
```

Use `.exe` on Windows and fresh output directories for each run. The validation
command verifies manifest hashes, runs C++, and independently checks all daily
cash/holdings/exposure/P&L and aggregate statistics in Python. The demonstration
uses a three-close lookback, $10,000 cash, 10 bp half-spread and 5 bp fee, with
one-second latency. These are illustrative assumptions, not estimated TLT costs.

The completed real-data run had **5 fills**, final equity **$9,884.15**, P&L
**−$115.85**, sampled maximum drawdown **2.5039%**, and period volatility
**0.4522%**. Fees were **$24.81**, modeled spread cost **$49.60**, and turnover
**4.9611×**. All daily accounting comparisons matched within `1e-8` USD (observed
maximum error zero). One final order remained unfilled. This short, single-window
result provides no evidence of strategy robustness or investable performance.

Evidence: [summary](results/tlt-simulation-summary.json),
[independent validation](results/tlt-simulation-validation.json), and
[source manifest](results/tlt-source-manifest.json). Preserve your downloaded
response for exact replication; a subsequent response may have a different hash
because of revised history or changing current-market metadata.

## Running another verified tape

```sh
./build/release/rates_simulate --prices prices.csv --decisions decisions.csv --out results/new-run --symbol TLT --basis recorded_availability --evidence archive-manifest-sha256 --lookback 5
```

`--basis` and `--evidence` are mandatory provenance labels. Supported bases are
`synthetic`, `retrospective`, and `recorded_availability`. The last is a caller
attestation, not a certificate; the executable cannot authenticate a supplied
timestamp or establish a vendor's historical completeness. Every summary retains
`point_in_time_certified: false`. A credible use of recorded availability needs
an immutable feed/archive, original receipt/publication timestamps, version and
correction history, session/action validation and a documented execution model.

Output: `orders.csv` records signal event/availability and decision timestamps,
requested/filled quantity and status; `fills.csv` records both timestamps, prices,
costs and order ID; `equity.csv` records marks and accounting; `summary.json`
records configuration, provenance and metrics. A fresh output directory is
required. Success is exit zero with a complete summary written last; output
I/O failure may leave partial files and returns nonzero. Treat these as failed
runs rather than consuming an incomplete directory.

## Verification

Ten GoogleTest cases exercise strict time boundaries, revisions, delayed data,
future-data invariance, hand-calculated round trips, costs, price gaps, expiry,
staleness, invalid configuration and numerical overflow. The independent Python
CLI test covers daily accounting, importer validation, input corruption and
output reuse. CI runs these offline using synthetic data; it does not rely on
an unstable live vendor download. The real public-data validation above is a
separate recorded run. Existing pricing, QuantLib, replay and benchmark checks
remain part of the full suite.
