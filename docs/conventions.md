# Supported conventions and units

| Item | Default and supported alternatives |
| --- | --- |
| Date | Gregorian ISO `YYYY-MM-DD`, inclusive years 1900..2199 |
| Curve time | Actual/365 Fixed from valuation |
| Deposit | Simple rate, Actual/360, effective on valuation |
| Fixed leg | Six calendar months, European 30/360 |
| Floating leg | Three calendar months, Actual/360, forward-looking |
| Calendar | Saturday/Sunday weekends plus an explicit holiday set |
| Business-day adjustment | Modified Following; Following or Unadjusted optional |
| Schedule | Forward generation anchored to original effective date |
| Stub | Short final stub, computed using actual start/end dates |
| Month end | Preserve calendar month end by default; configurable |
| Payment | At adjusted accrual end, zero lag |
| Quote | Decimal annualized rate, not a percentage or bp value |
| Risk bump | Positive basis points; default 1 |
| PV | Unspecified currency units consistent with notional |

There are no named regional holiday calendars or automatic holiday inference.
For non-Unadjusted schedules the effective date must already be a business
day; the engine rejects it rather than moving the contractual start silently.
The valuation anchor must likewise satisfy the selected convention for
bootstrapping. All accrual boundaries and payments are adjusted together.
Tenor month advances always refer to the original unadjusted effective date,
avoiding February-induced drift. If adjustment collapses or reverses a period,
the schedule is rejected.

European 30/360 replaces each day-of-month by `min(day,30)`; it has no special
February end rule. It is not US 30/360 or 30E/360 ISDA. Actual/365F always divides
the actual day difference by 365, including leap years. Reverse day-count
intervals are rejected; identical endpoints return zero, but a coupon requires
strictly positive accrual.

`Conventions` independently configures fixed/floating payment intervals in
integer calendar months (1..120), the three instrument/leg day counts,
calendar, business-day rule and end-of-month preservation. The CLI exposes
each setting. Market quotes and the demonstrated trade share one convention
set; library clients may configure a trade differently from calibration.

Future-effective swaps are supported, provided all dates are in the curve.
Trades beginning before valuation are rejected because historical fixings and
accrued cash flows are not implemented. Valuation-date effective swaps use
forward-looking coupons even for their first period. This synthetic convention
differs from real instruments that may already have today's fixing.

No currency-specific convention is implied. QuantLib validation selects a USD
currency object only to construct its synthetic index; no USD market-calendar
or settlement assumptions are imported.
