# Changelog

## Unreleased — 2.4.0-dev

- Added curve ambiguity bounds, exported witness curves, numerical optimality
  checks, a quote-neutral hedge experiment and conditional quote ranking.
  See the [research guide](docs/curve-ambiguity-research.md).
- Retained and independently repriced every forward-bound/spread-sweep witness;
  removed artificial zero-rate endpoints from the ambiguity chart, clarified
  its PV-witness labels and derived hedge chart captions from report values.
- Improved vanilla/OIS bootstrap performance with safeguarded secant steps and
  reuse of the trial curve. See the [solver study](docs/solver-review.md).
- Fixed the C++ date constructor accepting oversized month/day values after
  narrowing; invalid components now fail before constructing a calendar date.
- Fixed the single-trade CLI accepting trailing empty CSV columns by sharing
  the batch parser. Regression checks cover quote/trade files and LF/CRLF input.
- Added a native basis-risk test with an analytical expected DV01, par-instrument
  recalibration invariants, receiver/scaling checks and invalid bump rejection.
- Added a quick start, documentation index and contributor guide; repaired SOFR
  documentation encoding and removed the redundant review recap/editor setting.
- Development packages identify themselves as `2.4.0-dev`; published v2.3.0
  assets and historical measurements remain unchanged.

## Published releases

| Version | Changes and recorded validation |
| --- | --- |
| [v2.3.0](https://github.com/Aniket2002/rates-engine-cpp/releases/tag/v2.3.0) | [SOFR hedging, durable lifecycle and portable Windows binaries](docs/release-notes-v2.3.0.md) |
| v2.2.0 | [Seasoned SOFR and observation conventions](docs/release-notes-v2.2.0.md) |
| v2.1.0 | [OIS discounting and separate projection](docs/release-notes-v2.1.0.md) |
