# Architecture and engineering choices

```
date + conventions -> schedule + swap
date + conventions -> DiscountCurve
schedule + DiscountCurve -> swap_pricer
market quotes + swap_pricer -> CurveBootstrapper
CurveBootstrapper + swap_pricer -> RiskEngine
library -> CLI / benchmarks / optional pybind11 module
```

Public APIs live in `include/rates`; library translation units live in `src`.
Financial pricing code has no filesystem or CSV dependency. The separate
`HistoricalQuoteStore` module owns indexed quote-file access. The CLI owns strict input
parsing, console presentation and structured exports. Schedule generation is
independent of both curves and valuation. The optional Python extension calls
the same compiled library; `reference_pricer.py` is a separate validation-only
implementation and is never used by the extension.

`DiscountCurve` owns its nodes and precomputed times/log discounts and has no
mutating public methods. It inserts a time-zero anchor, validates sorted input
and uses binary search for interpolation. Public financial scalars distinguish
discount factors, simple rates, continuous zero rates, year fractions and
basis points. They enforce finite construction; domain-specific positivity and
ordering are checked by the consuming APIs. Public result scalars use documented
decimal/currency units. Exceptions preserve failures instead of sentinel zeros.

Calibration and risk use local value copies, so callers' quote sets and trades
are preserved on success and failure. Const pricing operations do not use global
state. The library can be called concurrently on independent result objects;
the optional QuantLib validator sets QuantLib's process-wide evaluation date
and is deliberately sequential.

We use bisection in C++ because its bracket and convergence behavior are easy
to audit. Python uses safeguarded Newton and telescoping PV as independent
paths. Small vectors are favored over abstraction-heavy instrument hierarchies.
Bootstrapping prepares schedules once per instrument, outside its root solve.
`RiskScenarios` owns immutable base/shocked curves, and `PortfolioRiskEngine`
shares immutable schedules for equal dates and conventions. Pricing still creates
cash-flow vectors; the Linux profiles expose remaining allocation costs.
The [v2 design](v2.md) describes replay, aggregation and benchmark boundaries.

`basis.hpp` composes seasoned overnight legs with separate histories/conventions.
Its bootstrap solves the other projection from basis instruments; risk recalibrates
the discount → base projection → basis projection dependency chain. `lifecycle.hpp`
contains clearing and cash-collateral ledgers. They validate events on a copied
state before committing, retain journals and support idempotent replay. They have
no network or database dependencies. The [contractual lifecycle guide](basis-clearing-collateral.md)
defines ordering, cash signs and caller-supplied clearing decisions.

The [v2.1 extension](multicurve.md) adds a two-curve `price` overload; single-curve
calls delegate with the same curve in both roles. `build_forward` shares the
solver while discounting on an exogenous curve. `quote_pillar` centralizes exact
zero versus adjusted contract dates. OIS specializes conventions and uses
future overnight telescoping under zero-lag assumptions. `multi_curve_risk`
recalibrates forwarding after discount quote shocks and reports both coordinates.

The `rates_engine` static target requires C++20 for the standard Gregorian
chrono calendar. Most financial calculations use C++17-compatible constructs,
but a C++17 build is not supported. Installed clients use
`find_package(rates_engine CONFIG REQUIRED)` and `rates::rates_engine`.
The export follows the [CMake package guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html).

GoogleTest is found as an installed package or fetched at tag `v1.17.0`.
Library-only builds need no test, Python or QuantLib dependencies. Python
bindings are opt-in. On MinGW their C++ runtime is linked statically to avoid
Python's restricted extension DLL search path; ordinary executables use the
toolchain runtime DLLs on PATH. Sanitizers are explicitly restricted to supported
Unix GCC/Clang builds. CI covers GCC, Clang, MSVC, macOS and a sanitizer job.
