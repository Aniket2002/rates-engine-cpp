# Development history

The v2.3.0 implementation has 21 commits in its ancestry. The
[commit history](https://github.com/Aniket2002/rates-engine-cpp/commits/main/)
records calibration, portfolio reuse, historical replay, OIS/multi-curve pricing,
SOFR lifecycle work, basis swaps and durable journals as separate changes.
Most subjects describe the implementation; `b351404` has the less descriptive
subject `update #1`. Existing published history is preserved.

## Existing version tags

| Tag | Scope and evidence |
| --- | --- |
| [v2.1.0](https://github.com/Aniket2002/rates-engine-cpp/tree/v2.1.0) | [OIS discounting and separate projection](multicurve.md), [verification](launch-validation.md) |
| [v2.2.0](https://github.com/Aniket2002/rates-engine-cpp/tree/v2.2.0) | [Seasoned SOFR and observation conventions](sofr-lifecycle.md), [verification](sofr-validation.md) |
| [v2.3.0](https://github.com/Aniket2002/rates-engine-cpp/releases/tag/v2.3.0) | [Hedge study, durable lifecycle and portable Windows binaries](release-notes-v2.3.0.md) |

Earlier v1/v2 measurements remain in the [execution report](execution_report.md)
and [portfolio study](v2.md). They describe development stages; this documentation
does not invent retrospective v1.0 or v2.0 releases.

For future changes, keep commits focused on a reviewable behavior or documentation
change and use descriptive subjects. Tag releases against tested commits, retaining
the workload, toolchain and provenance of their verification results. Repository
popularity is separate from the numerical and engineering evidence.
