# v2.3.0: SOFR hedging, durable lifecycle and portable Windows binaries

This release connects computed sensitivities to a constrained hedge decision,
adds complete contractual payment checks and durable lifecycle journals, and
removes compiler-DLL PATH requirements from default Windows executables.

- A 5Y SOFR hedge study sizes 2Y/4Y/6Y positions under USD 1.5 million per-instrument
  limits and USD 1,000 lot rounding. Quote-DV01 norm falls 51.68%; assumed entry
  costs are USD 379.73. Ten full-repricing shocks show residual and nonlinear risk,
  including cases that worsen. QuantLib independently checks 44 instrument PVs
  and 28 quote sensitivities. JSON, CSV and PNG/SVG results are committed.
- C++ exposes the full lag-adjusted SOFR payment schedule. The basis workflow
  derives final obligations from realized coupons and reconciles the complete
  inventory and amounts before normal-maturity closure.
- An optional Python/SQLite journal wrapper serializes concurrent writers,
  atomically validates/appends batches across streams, rejects conflicting
  retries and replays persisted state. Strict contract inventories prevent
  missing-payment closure. Tests include four processes, 80 concurrent events
  and process death before commit.
- Firm OIS import policy adds execution-size, expiry and source/usage-reference
  checks. Actual executable quotes still require authorized external data;
  synthetic examples remain clearly labeled.
- Windows builds embed compiler runtimes by default. A PowerShell launcher
  builds/tests/runs the example, and CPack produces a CLI/library ZIP with example
  inputs, build information and runtime license notices.
- README encoding is repaired, committed terminal auto-approval is removed,
  and implementation walkthroughs explain calibration, accrual and collateral.

## Run

From a configured source checkout:

```powershell
.\run.ps1 -WithPython -Test
```

Or download and extract `rates-engine-2.3.0-Windows-AMD64.zip`, open PowerShell in
its extracted root and run:

```powershell
.\bin\rates_cli.exe --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/demo
```

The ZIP contains the native CLI tools and static library. Python workflows are
built from source for the chosen interpreter. The Windows ZIP targets x64 with
the Universal CRT; its C++ library uses the LLVM-MinGW ABI.

## Verification

Local Windows x64, LLVM-MinGW Clang 23.1.3, Python 3.14.0, QuantLib 1.44:

| Check | Result |
| --- | --- |
| Release, bindings enabled | 100/100 CTest tests passed |
| Debug, bindings enabled | 100/100 CTest tests passed |
| Full Release suite with system-only PATH | 100/100 passed |
| Independent hedge prices and quote risks | 72 comparisons passed |
| Durable lifecycle and contract checks | 20 passed |
| Hedge solver fixtures / market-data unit tests | 4 / 7 passed |
| Installed C++ consumer with system-only PATH | Passed |
| Extracted ZIP, relocated CLI tools and actual pricing | Passed |
| C++ formatting and clang-tidy build | Passed |

See [runtime evidence](results/runtime/verification.json),
[hedge results](results/hedging/report.json), and
[durable lifecycle results](results/durable-lifecycle/report.json).
The cross-platform workflow runs the new tests and requires independent
QuantLib hedge validation. Consult the release commit's actual Actions run for
remote results; local results alone do not establish cross-platform success.

This remains a deterministic swaps project. Options, stochastic models, XVA,
licensed executable feeds and a production clearing service are not claimed.
