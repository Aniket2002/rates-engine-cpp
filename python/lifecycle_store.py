"""SQLite transaction boundary around deterministic C++ lifecycle journals.

Each operation opens its own connection. Writers serialize using BEGIN IMMEDIATE;
validation and all stream appends commit together. Replay avoids stale caches.
Designed for a local disk, not a network filesystem or distributed CCP service.
"""
from contextlib import contextmanager
import json
from pathlib import Path
import sqlite3


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


class LifecycleStore:
    def __init__(self, path, r):
        self.path, self.r = str(Path(path).resolve()), r
        with self.connection() as db:
            db.execute("PRAGMA journal_mode=WAL")
            db.executescript("""
                CREATE TABLE IF NOT EXISTS streams (
                    name TEXT PRIMARY KEY, definition TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS events (
                    sequence INTEGER PRIMARY KEY AUTOINCREMENT,
                    stream TEXT NOT NULL REFERENCES streams(name),
                    event_id TEXT NOT NULL, payload TEXT NOT NULL,
                    UNIQUE(stream,event_id));
            """)

    @contextmanager
    def connection(self):
        db = sqlite3.connect(self.path, timeout=30, isolation_level=None)
        try:
            db.execute("PRAGMA foreign_keys=ON")
            db.execute("PRAGMA synchronous=FULL")
            yield db
        finally:
            db.close()  # Rolls back an unfinished transaction, including validation failures.

    def create_stream(self, name, definition):
        if not isinstance(name, str) or not name.strip():
            raise ValueError("Nonempty stream name required")
        raw = canonical(definition)
        self.empty(json.loads(raw))  # Validate the persisted definition before insertion.
        with self.connection() as db:
            db.execute("BEGIN IMMEDIATE")
            old = db.execute("SELECT definition FROM streams WHERE name=?", (name,)).fetchone()
            if old and old[0] != raw:
                raise ValueError("Conflicting stream definition")
            if not old:
                db.execute("INSERT INTO streams VALUES (?,?)", (name, raw))
            db.execute("COMMIT")

    def empty(self, definition):
        r = self.r
        if definition.get("version") != 1:
            raise ValueError("Unsupported stream schema")
        if definition.get("kind") == "clearing" and set(definition) in ({"version", "kind"}, {"version", "kind", "contracts"}):
            if "contracts" in definition:
                if not definition["contracts"]:
                    raise ValueError("Contract inventory cannot be empty")
                for trade, payments in definition["contracts"].items():
                    if not trade or not payments:
                        raise ValueError("Contract and payment inventory required")
                    for reference, due in payments.items():
                        if not reference or str(r.Date(due)) != due:
                            raise ValueError("Invalid contractual payment reference/date")
            return r.ClearingLedger()
        if definition.get("kind") == "collateral" and set(definition) == {"version", "kind", "inception", "agreement"}:
            agreement = r.CollateralAgreement()
            allowed = {"receive_threshold", "post_threshold", "minimum_transfer", "rounding", "settlement_days",
                       "holidays", "interest_day_count"}
            if set(definition["agreement"]) != allowed:
                raise ValueError("Persist every agreement field explicitly")
            for key, value in definition["agreement"].items():
                if key == "holidays":
                    agreement.calendar = r.Calendar({r.Date(d) for d in value})
                elif key == "interest_day_count":
                    agreement.interest_day_count = getattr(r.DayCount, value)
                else:
                    setattr(agreement, key, value)
            return r.CollateralAccount(r.Date(definition["inception"]), agreement)
        raise ValueError("Unsupported stream definition")

    def encode(self, event):
        row = dict(id=event.id, at=str(event.at), kind=str(event.kind).split(".")[-1], reference=event.reference)
        if isinstance(event, self.r.ClearingEvent):
            row.update(trade=event.trade, amount=event.amount, due=str(event.due) if event.due else None)
        elif isinstance(event, self.r.CollateralEvent):
            row.update(value=event.value)
        else:
            raise ValueError("Unsupported event type")
        return canonical(row)

    def decode(self, row, kind):
        r = self.r
        row = json.loads(row)
        if kind == "clearing":
            return r.ClearingEvent(row["id"], r.Timestamp(row["at"]), getattr(r.ClearingEventKind, row["kind"]),
                                   row["trade"], row["reference"], row["amount"], r.Date(row["due"]) if row["due"] else None)
        return r.CollateralEvent(row["id"], r.Timestamp(row["at"]), getattr(r.CollateralEventKind, row["kind"]),
                                 row["reference"], row["value"])

    def replay(self, db, name):
        found = db.execute("SELECT definition FROM streams WHERE name=?", (name,)).fetchone()
        if not found:
            raise ValueError("Unknown stream")
        definition = json.loads(found[0])
        state = self.empty(definition)
        for payload, in db.execute("SELECT payload FROM events WHERE stream=? ORDER BY sequence", (name,)):
            event = self.decode(payload, definition["kind"])
            self.guard_contract(state, definition, event)
            state.apply(event)
        return state, definition

    def guard_contract(self, state, definition, event):
        """Persisted complete inventory prevents direct callers from bypassing closure checks."""
        contracts = definition.get("contracts")
        if contracts is None:
            return
        if event.trade not in contracts:
            raise ValueError("Trade missing from immutable contractual inventory")
        expected = contracts[event.trade]
        kind = str(event.kind).split(".")[-1]
        if kind == "Accept" and str(event.due) != max(expected.values()):
            raise ValueError("Acceptance final date differs from the contract")
        if kind == "RegisterPayment" and (event.reference not in expected or str(event.due) != expected[event.reference]):
            raise ValueError("Payment does not match the contractual inventory")
        if kind == "Terminate":
            raise ValueError("Contractual streams support normal maturity only, not discretionary closeout")
        if kind == "Close":
            payments = {key: p for key, p in state.payments.items() if p.trade == event.trade}
            if set(payments) != set(expected) or any(not p.settled or p.cancelled for p in payments.values()):
                raise ValueError("Complete contractual schedule has not settled")

    def snapshot(self, name):
        with self.connection() as db:
            db.execute("BEGIN")
            state, _ = self.replay(db, name)
            db.execute("COMMIT")
            return state

    def append(self, entries):
        """Atomically append (stream,event) pairs; retries return False for duplicates."""
        with self.connection() as db:
            db.execute("BEGIN IMMEDIATE")
            states, changed = {}, []
            for name, event in entries:
                if name not in states:
                    states[name] = self.replay(db, name)
                state, definition = states[name]
                raw = self.encode(event)
                parsed = self.decode(raw, definition["kind"])  # Reject events of the wrong stream type.
                self.guard_contract(state, definition, parsed)
                applied = state.apply(parsed)
                if applied:
                    db.execute("INSERT INTO events(stream,event_id,payload) VALUES (?,?,?)", (name, event.id, raw))
                changed.append(applied)
            db.execute("COMMIT")
            return changed
