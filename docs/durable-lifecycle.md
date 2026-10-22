# Contractual payment reconciliation and durable journals

`sofr_payment_schedule` now exposes the complete schedule used by the C++ SOFR
pricer, including payment lags. It does not drop old or settled coupons. The
Python `BasisContract` adapter builds a separate expected payment inventory for
each basis leg, with the trade ID, leg and due date in every reference.

On each due date, the adapter prices the contractual cash flows with available
fixings and refuses final obligations containing forecast observations. Signed
amounts come from those cash flows. Acceptance uses the latest contractual
payment date. Closure requires the complete inventory, settled payments and
matching final amounts; omitted coupons, wrong dates, extra payments and amount
mismatches are reported. Start the example before its first payment. Backdated
onboarding, amendments and discretionary termination are not supported by this
strict normal-maturity adapter.

```powershell
New-Item -ItemType Directory -Force results | Out-Null
.\.venv\Scripts\python.exe python/run_basis_lifecycle.py --module-dir build/release --out results/lifecycle --database results/lifecycle.sqlite
.\.venv\Scripts\python.exe python/test_lifecycle_store.py --module-dir build/release --out results/durable-validation
```

The demonstration appends each event to SQLite and can be rerun against the same
database: identical retries do not duplicate cash. It checks reopened clearing
and collateral snapshots against the independently replayed in-memory results.
Use a fresh database for a different contract; definitions are immutable.

## Transaction model

`LifecycleStore` opens a new connection per operation and uses `BEGIN IMMEDIATE`
to serialize writers. Within that transaction it replays the committed journals
into the C++ validators, validates incoming events, and appends them with a unique
`(stream,event_id)` key. A batch can contain both clearing and collateral streams;
it either commits entirely or rolls back. Use one batch for events that must
share a business transaction. The example's independently applied daily events
are individually durable, not one atomic daily transaction.

Reads use a transaction for a consistent snapshot. SQLite WAL mode and FULL
synchronous commits supply local-file durability subject to the operating
system/storage honoring flushes. There is a 30-second writer lock timeout;
callers must handle lock failures and retry the same event IDs. There is no
background retry or broker.

Collateral inception, all agreement fields and explicit holiday dates persist
with the stream. Strict clearing streams additionally persist the complete
contract-derived reference/date inventory. Their guards reject unknown trades,
unlisted obligations, mismatched final dates, discretionary termination and
closure with missing payments, even when callers bypass the workflow adapter.
The lower-level C++ ledger and a generic clearing stream remain available for
caller-supplied obligations.

Inventory guards cannot prove the authenticity of trade terms or fixings and
do not independently reprice caller-supplied amounts. Use `BasisContract` to
generate amounts and reconcile final-cash evidence. Preserve source trade and
fixing records separately; the database is an event store, not a market-data
archive or complete trade-capture system.

## Verification and operational limits

The test launches four independent processes writing 80 events, checks no lost
updates, conflicting and identical retries, failure rollback across streams,
reopening the database, and abrupt process death before commit. Separate tests
exercise missing/unpaid contractual coupons and incorrect final amounts.
See [the recorded results](results/durable-lifecycle/report.json).

This is a local persistent integration prototype, not a distributed service.
Replay currently rebuilds the entire stream for each append; long journals need
validated snapshots and performance work. No authentication, network API,
replication, schema migration or live CCP/payment connector is supplied. Keep
the database on local disk; do not assume WAL works on a network share. Use
SQLite's backup API for a live backup; copying only the main file while WAL
writes are active can omit committed events. Database possession permits direct
modification, so this is not a tamper-proof audit trail.
