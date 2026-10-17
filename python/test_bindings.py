"""Exercise C++ bindings against an analytical one-period trade."""
import math
import sys
sys.path.insert(0, sys.argv[1])
import pyrates as r

v = r.Date("2025-01-02")
maturity = v.add_months(12)
c = r.Conventions()
c.fixed_months = 12
c.fixed_day_count = r.DayCount.ACT365F
c.deposit_day_count = r.DayCount.ACT365F
c.business_day = r.BusinessDay.Unadjusted
curve = r.DiscountCurve(v, [(maturity, 0.95)])
swap = r.Swap(v, maturity, 1e6, 0.04, conventions=c)
result = r.price(curve, swap)
assert abs(result.pv - 12000) < 1e-8
assert abs(curve.zero(maturity) + math.log(0.95)) < 1e-14
assert len(result.fixed_flows) == 1
quotes = [r.MarketQuote(r.Instrument.Deposit, "1Y", maturity, 0.05)]
b = r.CurveBootstrapper(c)
calibration = b.build(v, quotes)
assert calibration.diagnostics[0].converged
assert abs(calibration.curve.discount(maturity)-1/1.05) < 1e-14
risk = r.RiskEngine(b)
expected = (1e6*(1-1.04/1.0499) - 1e6*(1-1.04/1.0501))/2
assert abs(risk.parallel_quote_dv01(v, quotes, swap)-expected) < 1e-8
assert abs(risk.quote_risk(v, quotes, swap)[0].dv01-expected) < 1e-8
assert risk.zero_risk(curve, swap)[0].dv01 < 0
try:
    curve.discount(v.add_months(13))
except IndexError:
    pass
else:
    raise AssertionError("C++ extrapolation exception was not propagated")
print("Python bindings: analytical price, curve, calibration, both risk coordinates and exceptions passed")
