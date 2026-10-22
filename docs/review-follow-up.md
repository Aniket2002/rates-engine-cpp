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
