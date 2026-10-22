"""Contract-derived basis payment inventory and final-cash reconciliation.

Use before the first contractual payment. Termination/closeout is intentionally
excluded: this adapter enforces complete, normal contractual maturity.
"""
import math


class BasisContract:
    def __init__(self, r, trade_id, effective, maturity, notional, spread,
                 base_conventions, other_conventions, side=None):
        if not trade_id:
            raise ValueError("Nonempty trade ID required")
        self.r, self.trade_id = r, trade_id
        side = side if side is not None else r.Side.Payer
        self.sign = 1 if side == r.Side.Payer else -1
        self.trade = r.BasisSwap(effective, maturity, notional, spread, side,
                                 base_conventions, other_conventions)
        self.expected = {}
        for leg, c in [("base", base_conventions), ("other", other_conventions)]:
            swap = r.SofrSwap(effective, maturity, notional, spread, side, c)
            for period in r.sofr_payment_schedule(swap):
                key = self.reference(leg, str(period.payment))
                if key in self.expected:
                    raise ValueError("Payment-reference collision")
                self.expected[key] = str(period.payment)
        self.final_payment = max(self.expected.values())

    def reference(self, leg, due):
        return f"{self.trade_id}/{leg}/{due}"

    def due_events(self, discount, base, other, base_fixings, other_fixings, at):
        """Generate final obligations from fully realized coupons due today."""
        r = self.r
        result = r.price_basis(discount, base, other, self.trade, base_fixings, other_fixings, at, True)
        day = str(at)[:10]
        events = []
        for leg, value, sign in [("base", result.base_leg, -self.sign), ("other", result.other_leg, self.sign)]:
            for cf in value.coupons:
                due = str(cf.period.payment)
                if due != day:
                    continue
                if not cf.observations or not all(o.realized for o in cf.observations):
                    raise ValueError("Cannot register a final obligation containing forecasts")
                reference = self.reference(leg, due)
                amount = sign * (cf.floating_amount + (cf.fixed_amount if leg == "base" else 0.))
                events.append(r.ClearingEvent("register/" + reference, at,
                              r.ClearingEventKind.RegisterPayment, self.trade_id, reference, amount, r.Date(due)))
        return events

    def reconcile(self, ledger, final_amounts=None):
        payments = {key: p for key, p in ledger.payments.items() if p.trade == self.trade_id}
        missing = sorted(set(self.expected) - set(payments))
        unexpected = sorted(set(payments) - set(self.expected))
        mismatched = sorted(key for key, p in payments.items() if key in self.expected and
                            (str(p.due) != self.expected[key] or not math.isfinite(p.amount)))
        if final_amounts is not None:
            if set(final_amounts) != set(self.expected):
                raise ValueError("Final cash evidence must cover the complete contract")
            mismatched += [key for key, p in payments.items() if key in final_amounts and
                           (not math.isfinite(final_amounts[key]) or abs(p.amount-final_amounts[key]) > 1e-8)]
        unresolved = sorted(key for key, p in payments.items() if not p.settled or p.cancelled)
        return dict(complete=not (missing or unexpected or mismatched or unresolved), missing=missing,
                    unexpected=unexpected, mismatched=mismatched, unresolved=unresolved,
                    expected_count=len(self.expected), registered_count=len(payments),
                    settled_cash=math.fsum(p.amount for p in payments.values() if p.settled))

    def close_event(self, ledger, at, final_amounts):
        if not self.reconcile(ledger, final_amounts)["complete"]:
            raise ValueError("Contract payments have not fully reconciled")
        return self.r.ClearingEvent("close/" + self.trade_id, at, self.r.ClearingEventKind.Close, self.trade_id)
