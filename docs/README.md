# Documentation

Start with [worked examples](worked-examples.md) to run a valuation, or
[reproducibility](reproducibility.md) to configure a build. The root
[README](../README.md) summarizes the project; [CONTRIBUTING](../CONTRIBUTING.md)
explains development checks.

## Current guides

| Topic | Guide |
| --- | --- |
| Research question and reproduction | [Curve ambiguity](curve-ambiguity-research.md), available in 2.4.0-dev source |
| Curve construction and pricing | [Methodology](methodology.md), [conventions](conventions.md), [multi-curve pricing](multicurve.md) |
| Risk and hedge decisions | [Risk methodology](risk_methodology.md), [SOFR hedge study](hedging-study.md) |
| Overnight rates | [SOFR lifecycle](sofr-lifecycle.md), [basis, clearing and collateral](basis-clearing-collateral.md) |
| Persistent contractual journals | [Durable lifecycle](durable-lifecycle.md) |
| Code and C++ consumers | [Architecture](architecture.md), [implementation walkthrough](implementation-walkthrough.md), [consumer example](../examples/consumer) |
| Data and supported scope | [Market-data status](market-data-status.md), [real historical data](real-historical-data.md), [limitations](limitations.md) |
| Replay operations | [Runbook](../ops/runbook.md), [separate ETF simulator](trading-simulation.md) |

## Validation and research evidence

Reports identify their own revisions and configurations. A historical test count
or coverage percentage is not a measurement of today's source.

| Evidence | Guide |
| --- | --- |
| Numerical validation | [Mathematical validation](mathematical_validation.md), [SOFR validation](sofr-validation.md) |
| Coverage and known instrumentation gaps | [Coverage](coverage.md) |
| Calibration performance | [Solver study](solver-review.md) |
| Scenario reuse and profiling | [Portfolio study](v2.md), [original benchmark methodology](benchmarks.md) |
| Valuation interpretation | [Model-validation case study](model-validation-case-study.md) |
| Recorded artifacts | [Results](results), [chart provenance](charts/manifest.json) |

## Releases and historical records

[Changelog](../CHANGELOG.md) separates unreleased changes from published
[v2.3.0](release-notes-v2.3.0.md), [v2.2.0](release-notes-v2.2.0.md) and
[v2.1.0](release-notes-v2.1.0.md). [Development history](development-history.md)
links the actual tags and commits.

The [original execution report](execution_report.md),
[validation upgrade](validation-upgrade.md) and
[launch validation](launch-validation.md) describe earlier development stages.
They remain available to explain the retained results; use the current guides
above for setup and supported behavior.
