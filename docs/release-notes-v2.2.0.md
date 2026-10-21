# v2.2.0 — SOFR lifecycle pricing and auditable market data

SOFR swaps can now mix realized and forecast overnight observations, price
seasoned and matured/unpaid coupons, and exclude settled cash flows. New APIs
support lookback, observation shift, lockout, spot settlement and payment lags,
accrued/clean PV, lag-aware OIS calibration and separate discount/projection risk.

The New York Fed adapter captures actual public SOFR fixings with immutable
raw-response archives, explicit local availability and revision history. The
OIS quote importer checks source, timestamps, units, calendar/conventions,
required tenors, bid/ask bounds, freshness and configurable rate/spread limits.
An offline example combines public fixings with clearly marked synthetic quotes.

Validation: 86 Release tests, 82 Debug tests, 6,892 independent lifecycle and
calibration comparisons, and 28 captured-fixing cash-flow/risk comparisons pass.
Formatting, clang-tidy, coverage and the installed consumer were checked.
An observation-period normalization defect was corrected during external validation.

The changes are split into focused commits. Existing v2.1 APIs keep their scope.
Public fixing capture does not provide executable OIS curves: those still require
an external quote provider. Contract amendments, novations, clearing, collateral
and production monitoring remain outside the implementation. See the
[lifecycle guide](https://github.com/Aniket2002/rates-engine-cpp/blob/v2.2.0/docs/sofr-lifecycle.md)
and [verification record](https://github.com/Aniket2002/rates-engine-cpp/blob/v2.2.0/docs/sofr-validation.md).
