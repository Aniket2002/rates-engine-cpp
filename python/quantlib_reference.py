"""Independent QuantLib schedules, day counts and quote-helper calibration.

Helper bootstrapping receives ONLY original market quotes and conventions,
never engine discounts. Stub trades use explicitly matched calendar-date EOM
rules; helper cases require regular tenor-aligned calibration swaps.
"""
from datetime import date
from reference_pricer import DEFAULT


def qdate(day):
    import QuantLib as ql
    return ql.Date(day.day, day.month, day.year)


def settings(conventions):
    import QuantLib as ql
    calendar = ql.BespokeCalendar("rates-validation")
    calendar.addWeekend(ql.Saturday)
    calendar.addWeekend(ql.Sunday)
    for holiday in conventions.holidays:
        calendar.addHoliday(qdate(holiday))
    rule = {"following": ql.Following, "modified-following": ql.ModifiedFollowing,
            "unadjusted": ql.Unadjusted}[conventions.adjustment]
    dc = {"ACT360": ql.Actual360(), "ACT365F": ql.Actual365Fixed(),
          "30E360": ql.Thirty360(ql.Thirty360.European)}
    return calendar, rule, dc


def periods(start, maturity, months, day_count, conventions=DEFAULT):
    import QuantLib as ql
    calendar, rule, dc = settings(conventions)
    # Engine EOM means calendar EOM followed by adjustment, not business EOM.
    raw = ql.Schedule(qdate(start), qdate(maturity), ql.Period(months, ql.Months),
                      ql.NullCalendar(), ql.Unadjusted, ql.Unadjusted,
                      ql.DateGeneration.Forward, conventions.eom)
    days = [calendar.adjust(d, rule) for d in raw]
    return [(date(a.year(), a.month(), a.dayOfMonth()), date(b.year(), b.month(), b.dayOfMonth()),
             dc[day_count].yearFraction(a, b)) for a, b in zip(days, days[1:])]


def price(curve, trade, conventions=DEFAULT):
    import QuantLib as ql
    calendar, _, dc = settings(conventions)
    qcurve = ql.DiscountCurve([qdate(curve.valuation)] + [qdate(d) for d, _ in curve.nodes],
                              [1.0] + [df for _, df in curve.nodes], ql.Actual365Fixed(), calendar)
    return price_on_curve(qcurve, curve.valuation, trade, conventions)


def price_on_curve(qcurve, valuation, trade, conventions=DEFAULT):
    import QuantLib as ql
    _, _, dc = settings(conventions)
    fixed = periods(trade["effective"], trade["maturity"], conventions.fixed_months, conventions.fixed_dc, conventions)
    floating = periods(trade["effective"], trade["maturity"], conventions.floating_months, conventions.floating_dc, conventions)
    notional, coupon = trade["notional"], trade["coupon"]
    annuity = sum(alpha * qcurve.discount(qdate(end)) for _, end, alpha in fixed)
    fixed_flows = [ql.SimpleCashFlow(notional * coupon * alpha, qdate(end)) for _, end, alpha in fixed]
    floating_flows = [ql.SimpleCashFlow(notional * alpha * qcurve.forwardRate(qdate(start), qdate(end),
                      dc[conventions.floating_dc], ql.Simple).rate(), qdate(end)) for start, end, alpha in floating]
    handle = ql.YieldTermStructureHandle(qcurve)
    fixed_pv = ql.CashFlows.npv(fixed_flows, handle, False, qdate(valuation), qdate(valuation))
    floating_pv = ql.CashFlows.npv(floating_flows, handle, False, qdate(valuation), qdate(valuation))
    return dict(fixed_pv=fixed_pv, floating_pv=floating_pv,
                pv=(floating_pv-fixed_pv)*(1 if trade.get("side", "payer") == "payer" else -1),
                par_rate=floating_pv/notional/annuity)


def bootstrap(valuation, quotes, conventions=DEFAULT):
    import QuantLib as ql
    ql.Settings.instance().evaluationDate = qdate(valuation)
    calendar, rule, dc = settings(conventions)
    index = ql.IborIndex("Synthetic", ql.Period(conventions.floating_months, ql.Months), 0,
                        ql.USDCurrency(), calendar, rule, conventions.eom, dc[conventions.floating_dc])
    frequencies = {1: ql.Monthly, 3: ql.Quarterly, 6: ql.Semiannual, 12: ql.Annual}
    helpers = []
    for quote in quotes:
        end = quote["maturity"]
        months = (end.year-valuation.year)*12+end.month-valuation.month
        if months <= 0:
            raise ValueError("QuantLib helper case needs a positive month tenor")
        tenor = ql.Period(months, ql.Months)
        if quote["type"] == "deposit":
            helper = ql.DepositRateHelper(quote["rate"], tenor, 0, calendar, rule, conventions.eom, dc[conventions.deposit_dc])
        else:
            if months % conventions.fixed_months or months % conventions.floating_months:
                raise ValueError("Rate-helper calibration case must have regular swaps; use separate stub validation")
            helper = ql.SwapRateHelper(quote["rate"], tenor, calendar, frequencies[conventions.fixed_months], rule,
                       dc[conventions.fixed_dc], index, ql.QuoteHandle(), ql.Period(0, ql.Days),
                       ql.YieldTermStructureHandle(), 0, ql.Pillar.MaturityDate, ql.Date(), conventions.eom, False, rule)
        expected = calendar.adjust(qdate(end), rule)
        if helper.pillarDate() != expected or helper.earliestDate() != qdate(valuation):
            raise ValueError("QuantLib helper dates differ from requested quote contract")
        helpers.append(helper)
    curve = ql.PiecewiseLogLinearDiscount(qdate(valuation), helpers, ql.Actual365Fixed(), ql.IterativeBootstrap(1e-14))
    curve.discount(helpers[-1].pillarDate())  # Force calibration; retain any failure.
    return curve, helpers
