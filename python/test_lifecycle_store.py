"""Durability, process concurrency, atomic rollback and contractual completeness."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from contract_workflow import BasisContract
from lifecycle_store import LifecycleStore
from run_basis_lifecycle import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module-dir", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--worker")
    parser.add_argument("--database", type=Path)
    args = parser.parse_args()
    sys.path.insert(0, str(args.module_dir.resolve()))
    import pyrates as r
    at = r.Timestamp("2025-01-02T16:00:00Z")
    def event(id, kind, trade="t", reference="", amount=0., due=None):
        return r.ClearingEvent(id, at, getattr(r.ClearingEventKind, kind), trade, reference,
                               amount, r.Date(due) if due else None)
    if args.worker is not None:
        store = LifecycleStore(args.database, r)
        for i in range(10):
            key = args.worker + "/" + str(i)
            store.append([("concurrent", event("s/"+key, "Submit", key)),
                          ("concurrent", event("a/"+key, "Accept", key, due="2025-02-03"))])
        return
    checks = []
    def check(name, condition):
        if not condition:
            raise AssertionError(name)
        checks.append(name)
    def rejected(action):
        try:
            action()
        except (ValueError, RuntimeError, KeyError, TypeError):
            return True
        return False
    with tempfile.TemporaryDirectory(prefix="rates lifecycle ") as temporary:
        database = Path(temporary)/"journal.sqlite"
        report = run(args.module_dir, database)
        again = run(args.module_dir, database)
        check("deterministic retry and reopened durable replay", report == again and report["durable_replay"])
        check("contract inventory and final cash reconciled", report["reconciliation"]["complete"])
        store = LifecycleStore(database, r)
        store.create_stream("concurrent", dict(version=1, kind="clearing"))
        store.create_stream("atomic", dict(version=1, kind="clearing"))
        processes = [subprocess.Popen([sys.executable, __file__, "--module-dir", str(args.module_dir.resolve()),
                     "--database", str(database), "--worker", str(i)], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     text=True) for i in range(4)]
        for process in processes:
            stdout, stderr = process.communicate(timeout=60)
            if process.returncode:
                raise AssertionError(stderr)
            check("concurrent process completed", process.returncode == 0)
        state = store.snapshot("concurrent")
        check("four writers: no lost or duplicated events", len(state.journal) == 80 and len(state.trades) == 40)
        check("idempotent retry", store.append([("concurrent", event("s/0/0", "Submit", "0/0"))]) == [False])
        check("conflicting retry rejected", rejected(lambda: store.append([("concurrent", event("s/0/0", "Submit", "other"))])))
        check("atomic cross-stream validation rollback", rejected(lambda: store.append([
            ("atomic", event("new", "Submit")), ("concurrent", event("bad", "Close", "0/0"))])))
        check("failed transaction left no event", len(store.snapshot("atomic").journal) == 0)
        check("immutable stream configuration", rejected(lambda: store.create_stream("atomic", dict(version=2, kind="clearing"))))
        # Abrupt process death after a valid INSERT but before COMMIT must not publish it.
        payload = store.encode(event("crash", "Submit"))
        code = ("import sqlite3,os,sys; d=sqlite3.connect(sys.argv[1]); d.execute('BEGIN IMMEDIATE'); "
                "d.execute('INSERT INTO events(stream,event_id,payload) VALUES (?,?,?)', "
                "('atomic','crash',sys.argv[2])); os._exit(17)")
        process = subprocess.run([sys.executable, "-c", code, str(database), payload], timeout=30)
        check("uncommitted process death rolls back", process.returncode == 17 and len(store.snapshot("atomic").journal) == 0)
        c = r.SofrConventions()
        c.payment_months, c.payment_lag_days = 1, 2
        contract = BasisContract(r, "contract", r.Date("2025-01-02"), r.Date("2025-03-02"), 1e6, .001, c, c)
        ledger = r.ClearingLedger()
        ledger.apply(event("submit", "Submit", "contract"))
        ledger.apply(event("accept", "Accept", "contract", due=contract.final_payment))
        store.create_stream("strict", dict(version=1, kind="clearing", contracts={"contract": contract.expected}))
        store.append([("strict", e) for e in ledger.journal])
        evidence = {key: 1. for key in contract.expected}
        end = r.Timestamp(contract.final_payment + "T16:00:00Z")
        check("omitted full schedule blocks closure", rejected(lambda: contract.close_event(ledger, end, evidence)))
        check("direct durable closure cannot bypass inventory", rejected(lambda: store.append([
            ("strict", r.ClearingEvent("bypass", end, r.ClearingEventKind.Close, "contract"))])))
        check("unexpected payment rejected by durable inventory", rejected(lambda: store.append([
            ("strict", event("extra", "RegisterPayment", "contract", "unknown", 1., contract.final_payment))])))
        for key, due in contract.expected.items():
            ledger.apply(event("r/"+key, "RegisterPayment", "contract", key, 1., due))
        check("registered but unpaid coupons block closure", rejected(lambda: contract.close_event(ledger, end, evidence)))
        for key in contract.expected:
            ledger.apply(r.ClearingEvent("p/"+key, end, r.ClearingEventKind.SettlePayment, "contract", key))
        check("full schedule settled", contract.reconcile(ledger, evidence)["complete"])
        altered = dict(evidence)
        altered[next(iter(altered))] = 2.
        check("incorrect final cash blocks closure", rejected(lambda: contract.close_event(ledger, end, altered)))
        ledger.apply(contract.close_event(ledger, end, evidence))
        check("reconciled contract can close", ledger.trades["contract"].state == r.ClearingState.Closed)
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out/"report.json").write_text(json.dumps(dict(passed=True, checks=checks), indent=2)+"\n", encoding="utf-8")
    print(f"Durable lifecycle: {len(checks)} checks passed")


if __name__ == "__main__":
    main()
