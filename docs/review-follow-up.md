# Review follow-up (22 October 2026)

| Review item | Action and evidence |
| --- | --- |
| README encoding | Replaced malformed minus/multiplication sequences with plain ASCII |
| Terminal auto-approval | Removed `chat.tools.terminal.autoApprove` from repository settings |
| Executable market realism | Added optional firm-data policy, expiry/size/provenance tests and an explicit [data-status record](market-data-status.md); actual executable quotes remain externally dependent |
| Integrated hedge study | Implemented constrained sizing, lot rounding, costs, key-rate residuals and ten full-repricing shocks, with independent QuantLib validation and charts |
| Numerical stress/conventions | Added positive/negative large shocks and bump convergence; retained the existing broad holiday/day-count/stub/observation-convention suite |
| Contractual payment integration | Shared full payment schedule with pricing, generated final basis obligations, checked inventory and amounts before closure |
| Persistence and concurrency | Added transactional SQLite streams, strict payment-inventory guards, multiprocess tests, restart replay and rollback checks; documented local-prototype limits |
| New tagged release | Prepared v2.3.0 source, release notes and a tested portable Windows ZIP |
| Explain the implementation | Added a source-linked walkthrough of basis calibration, downstream recalibration, SOFR accrual and collateral accounting |
| More derivatives/stochastic models | Followed the review's advice to retain scope; no superficial Hull-White or Bermudan implementation |
| Run without compiler DLL errors | Embedded Windows compiler runtimes; tested 100 Release tests, relocated binaries and extracted ZIP with system-only PATH |

No licensed quote feed or live clearing connection is available in this project.
Those facts cannot be changed by generating sample numbers or adding a database.
The implementation and documentation keep that distinction visible.

## LinkedIn readiness follow-up (27 October 2026)

- Published [v2.3.0](https://github.com/Aniket2002/rates-engine-cpp/releases/tag/v2.3.0).
  Downloaded the ZIP and checksum file without GitHub authentication, matched the
  recorded SHA-256, and ran extracted pricing and relocated CLI tools with only
  Windows system directories on `PATH`. See the
  [public-release verification](results/runtime/release-verification.json).
- Reorganized the README around current capabilities and one release-results
  table, with build/release badges and zero-curve/DV01 plots near the top.
  Moved the detailed walkthroughs and historical suite counts to
  [worked examples](worked-examples.md). The existing
  [SOFR hedge chart](charts/hedging.png) is the visual for sharing the hedge study.
- Removed the headline speedup and described shared scenarios as a design-choice
  experiment. The [benchmark evidence](v2.md#linux-replay-and-profiling-evidence)
  identifies the repeated setup in the baseline and has no QuantLib timing claim.
- Promoted existing OIS discounting and separate projection APIs; reran their
  independent QuantLib validator on 27 October 2026: all 230 comparisons passed.
  Kept Treasury proxy data distinct from swap-market calibration and moved the
  peripheral ETF simulator link into the worked-examples guide.
- Linked the actual [development history](development-history.md) and existing
  release tags, preserving published commits.
- Corrected Windows runtime instructions in the architecture and benchmark guides.
- Retained [v2.3.0 CI evidence](results/ci-v2.3.0.json), labeled the older snapshot,
  and separated [current and historical coverage](coverage.md), including LLVM's
  retained diagnostic warning.
