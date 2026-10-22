# Rates Engine 2.3.0 portable CLI and C++ library

Extract the entire ZIP. Open PowerShell in the extracted folder on Windows x64:

```powershell
.\bin\rates_cli.exe --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/demo
.\bin\rates_batch.exe --help
.\bin\rates_simulate.exe --help
```

The example produces `results/demo/summary.csv`, curve/calibration/cash-flow CSVs
and quote/zero-node risk. These inputs are synthetic. The expected payer PV is
approximately 4,531.35 and signed parallel quote DV01 is -451.94 USD/bp.

Windows compiler runtimes are embedded in the default release; no compiler
installation or compiler PATH entry is needed to run these executables. Windows
system libraries, including the Universal CRT, are still required. On Linux/macOS
packages use `./bin/rates_cli` and the operating system's normal runtime support.

`BUILD-INFO.txt` records the compiler/platform. `include/` and `lib/` provide the
static C++ library and CMake package; consumers need an ABI-compatible compiler
and matching runtime. The Windows release uses LLVM-MinGW x64.

Python SOFR/basis, hedging and SQLite workflows are available in the
[source repository](https://github.com/Aniket2002/rates-engine-cpp). They require
building the optional Python extension for your interpreter and are not included
in this CLI ZIP. In a configured source checkout, run `./run.ps1 -WithPython -Test`.
See [build instructions](https://github.com/Aniket2002/rates-engine-cpp/blob/v2.3.0/docs/reproducibility.md)
and [model boundaries](https://github.com/Aniket2002/rates-engine-cpp/blob/v2.3.0/docs/limitations.md).

The engine is MIT licensed. Third-party runtime notices are in `licenses/`.
