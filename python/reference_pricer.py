"""Independent scalar reference; standard library only, never calls C++ bindings.

Uses Python datetime, log-linear discount interpolation, a safeguarded Newton
calibration, and the single-curve telescoping identity for floating-leg PV.
"""
from bisect import bisect_left
from calendar import monthrange
from dataclasses import dataclass
from datetime import date, timedelta
import math


@dataclass(frozen=True)
class Conventions:
    fixed_months: int = 6
    floating_months: int = 3
    fixed_dc: str = "30E360"
    floating_dc: str = "ACT360"
    deposit_dc: str = "ACT360"
    adjustment: str = "modified-following"
    eom: bool = True
    holidays: tuple = ()


DEFAULT = Conventions()


def add_months(start, months, eom=True):
    ordinal = start.year * 12 + start.month - 1 + months
    year, month = divmod(ordinal, 12)
    month += 1
    last = monthrange(year, month)[1]
    day = last if eom and start.day == monthrange(start.year, start.month)[1] else min(start.day, last)
    return date(year, month, day)


def adjust(day, conventions=DEFAULT):
    if conventions.adjustment == "unadjusted":
        return day
    def closed(d):
        return d.weekday() >= 5 or d in conventions.holidays
    candidate = day
    while closed(candidate):
        candidate += timedelta(days=1)
    if conventions.adjustment == "modified-following" and candidate.month != day.month:
        candidate = day
        while closed(candidate):
            candidate -= timedelta(days=1)
    return candidate


def fraction(start, end, convention):
    if convention == "ACT360":
        return (end - start).days / 360
    if convention == "ACT365F":
        return (end - start).days / 365
    if convention == "30E360":
        return (360 * (end.year - start.year) + 30 * (end.month - start.month)
                + min(end.day, 30) - min(start.day, 30)) / 360
    raise ValueError(convention)


def schedule(start, maturity, months, dc, conventions=DEFAULT):
    periods = []
    previous = start
    offset = months
    while True:
        raw = min(add_months(start, offset, conventions.eom), maturity)
        end = adjust(raw, conventions)
        if end <= previous:
            raise ValueError("Collapsed reference schedule")
        periods.append((previous, end, fraction(previous, end, dc)))
        if raw == maturity:
            return periods
        previous = end
        offset += months


@dataclass
class Curve:
    valuation: date
    nodes: list  # [(date, positive DF)], excluding anchor

    def discount(self, day):
        if day == self.valuation:
            return 1.0
        dates = [self.valuation] + [d for d, _ in self.nodes]
        dfs = [1.0] + [df for _, df in self.nodes]
        j = bisect_left(dates, day)
        if j == 0 or j == len(dates):
            raise ValueError("Reference curve extrapolation")
        if dates[j] == day:
            return dfs[j]
        weight = (day - dates[j-1]).days / (dates[j] - dates[j-1]).days
        return math.exp((1-weight) * math.log(dfs[j-1]) + weight * math.log(dfs[j]))


def price(curve, effective, maturity, notional, coupon, side="payer", conventions=DEFAULT):
    fixed = schedule(effective, maturity, conventions.fixed_months, conventions.fixed_dc, conventions)
    annuity = math.fsum(alpha * curve.discount(end) for _, end, alpha in fixed)
    floating_unit = curve.discount(effective) - curve.discount(adjust(maturity, conventions))
    fixed_pv = notional * coupon * annuity
    floating_pv = notional * floating_unit
    return {"fixed_pv": fixed_pv, "floating_pv": floating_pv,
            "pv": (floating_pv-fixed_pv) * (1 if side == "payer" else -1),
            "par_rate": floating_unit / annuity, "annuity": annuity}


def bootstrap(valuation, quotes, conventions=DEFAULT):
    nodes = []
    for quote in sorted(quotes, key=lambda q: adjust(q["maturity"], conventions)):
        end = adjust(quote["maturity"], conventions)
        rate = quote["rate"]
        if quote["type"] == "deposit":
            df = 1 / (1 + rate * fraction(valuation, end, conventions.deposit_dc))
        else:
            def objective(x):
                candidate = Curve(valuation, nodes + [(end, math.exp(x))])
                return price(candidate, valuation, quote["maturity"], 1, 0, conventions=conventions)["par_rate"] - rate

            lo, hi = -50.0, 50.0
            if objective(lo) * objective(hi) > 0:
                raise ValueError("Reference root cannot be bracketed")
            x = -rate * fraction(valuation, end, "ACT365F")
            for _ in range(200):
                value = objective(x)
                if abs(value) <= 2e-15:
                    break
                if value > 0:
                    lo = x
                else:
                    hi = x
                epsilon = 1e-5
                derivative = (objective(x+epsilon)-objective(x-epsilon))/(2*epsilon)
                candidate = x - value/derivative if derivative else math.inf
                x = candidate if lo < candidate < hi else (lo+hi)/2
            else:
                raise RuntimeError("Independent Newton calibration failed")
            df = math.exp(x)
        nodes.append((end, df))
    return Curve(valuation, nodes)


def quote_risk(valuation, quotes, trade, bucket=None, bump=1.0):
    def value(sign):
        shocked = [dict(q, rate=q["rate"] + (sign*bump*1e-4 if bucket is None or i == bucket else 0))
                   for i, q in enumerate(quotes)]
        return price(bootstrap(valuation, shocked, trade.get("conventions", DEFAULT)), **trade)["pv"]
    return (value(-1)-value(1))/(2*bump)


def zero_risk(curve, trade, index, bump=1.0):
    def value(sign):
        nodes = list(curve.nodes)
        day, df = nodes[index]
        nodes[index] = (day, df*math.exp(-sign*bump*1e-4*fraction(curve.valuation, day, "ACT365F")))
        return price(Curve(curve.valuation, nodes), **trade)["pv"]
    return (value(-1)-value(1))/(2*bump)
