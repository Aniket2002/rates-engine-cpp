# Reproduction commands

Run from the repository root. Local build/output/dependency directories are
ignored by Git. Recorded results are deliberate copies under `docs/results`.

## Linux/macOS or Windows Developer shell

Install CMake, a C++20 compiler, Ninja and Python 3.10+. Then:

```sh
python -m venv .venv
# POSIX activation; on PowerShell use .\.venv\Scripts\Activate.ps1
. .venv/bin/activate
python -m pip install ninja pybind11 QuantLib clang-format==23.1.3 clang-tidy==22.1.8
cmake --preset release -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build --preset release --parallel 4
ctest --preset release
cmake --preset debug -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build --preset debug --parallel 4
ctest --preset debug
./build/release/rates_cli --out results/demo
python python/validate_results.py --cli build/release/rates_cli --output results/validation --require-quantlib
./build/release/rates_benchmark results/benchmark.csv 25
```

Use a Developer shell for MSVC so `cl`, SDK includes and libraries are available.
On Windows use `.exe` executable suffixes and PowerShell syntax below.

## Exact local Windows toolchain

The execution report used portable LLVM-MinGW `20261006-ucrt-x86_64`
(Clang 23.1.3), downloaded from the
[upstream release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261006)
into `.tools/llvm-mingw-20261006-ucrt-x86_64`, plus Python 3.14.0. No global
compiler installation was changed. From PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install ninja pybind11==3.1.0 QuantLib==1.44 clang-format==23.1.3
$env:PATH = "$PWD\.tools\llvm-mingw-20261006-ucrt-x86_64\bin;$PWD\.venv\Scripts;$env:PATH"
cmake --preset release -DCMAKE_CXX_COMPILER=clang++ "-DPython3_EXECUTABLE=$PWD/.venv/Scripts/python.exe"
cmake --build --preset release --parallel 4
ctest --preset release
cmake --preset debug -DCMAKE_CXX_COMPILER=clang++ "-DPython3_EXECUTABLE=$PWD/.venv/Scripts/python.exe"
cmake --build --preset debug --parallel 4
ctest --preset debug
.\build\release\rates_cli.exe --out results/demo
.\.venv\Scripts\python.exe python/validate_results.py --cli build/release/rates_cli.exe --output results/validation --require-quantlib
.\build\release\rates_benchmark.exe results/benchmark.csv 25
```

The compiler's `bin` directory must remain on PATH when running standalone
executables, because LLVM-MinGW uses libc++/libunwind DLLs. The optional Python
module embeds these runtimes and avoids a separate DLL-path requirement.

## Bindings

PowerShell:

```powershell
$pybindDirectory = .\.venv\Scripts\python.exe -m pybind11 --cmakedir
cmake --preset release -DRATES_BUILD_PYTHON=ON "-Dpybind11_DIR=$pybindDirectory"
cmake --build --preset release --parallel 4
ctest --preset release
.\.venv\Scripts\python.exe python/test_bindings.py build/release
```

POSIX equivalent:

```sh
cmake --preset release -DRATES_BUILD_PYTHON=ON -Dpybind11_DIR="$(python -m pybind11 --cmakedir)"
cmake --build --preset release
ctest --preset release
```

## Installation/export check

```powershell
cmake --install build/release --prefix install
cmake -S examples/consumer -B build/consumer -G Ninja -DCMAKE_CXX_COMPILER=clang++ "-DCMAKE_PREFIX_PATH=$PWD/install"
cmake --build build/consumer
.\build\consumer\consumer.exe
```

For MSVC omit the compiler override. For POSIX use `./build/consumer/consumer`.

## Formatting/static checks and sanitizers

```powershell
$sourceFiles = rg --files include src apps tests benchmarks python examples -g '*.hpp' -g '*.cpp'
.\.venv\Scripts\clang-format.exe --dry-run --Werror $sourceFiles
cmake -S . -B build/static -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DRATES_CLANG_TIDY=ON
cmake --build build/static --parallel 4
```

Use the toolchain's own clang-tidy on LLVM-MinGW: an unrelated Windows
clang-tidy may not discover the target's standard-library headers. On Linux,
CI uses pip's pinned clang-tidy with the installed system Clang.

On Linux/macOS only:

```sh
cmake --preset sanitizers -DCMAKE_CXX_COMPILER=clang++
cmake --build --preset sanitizers --parallel 4
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --preset sanitizers
```

CI defines all platform checks in `.github/workflows/ci.yml`. A workflow's
presence is not evidence that it passed; consult the execution report and the
actual GitHub Actions run for measured status.
