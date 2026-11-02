# Contributing

Start with the [documentation index](docs/README.md), [architecture](docs/architecture.md)
and [model boundaries](docs/limitations.md). Keep changes focused on supported
pricing, risk, lifecycle or research behavior.

## Build and test

Install CMake 3.25+, Ninja, a C++20 compiler and Python 3.10+. The
[reproduction guide](docs/reproducibility.md) covers compiler setup and Windows
Developer shells. Run commands from the repository root.

On Windows, after installing the prerequisites into the chosen Python environment:

```powershell
.\run.ps1 -WithPython -Test
.\run.ps1 -WithPython -Test -Configuration Debug
```

On Linux/macOS, activate a virtual environment, then:

```sh
python -m pip install ninja pybind11 QuantLib "scipy>=1.14,<2"
cmake --preset release -DRATES_BUILD_PYTHON=ON -Dpybind11_DIR="$(python -m pybind11 --cmakedir)" -DPython3_EXECUTABLE="$(python -c 'import sys; print(sys.executable)')"
cmake --build --preset release --parallel 4
ctest --preset release
```

Use the `debug` preset for Debug. GoogleTest is fetched on the first test build.
SciPy and QuantLib are required by the Python-enabled test suite; Matplotlib is
only needed to regenerate charts. Native builds can omit bindings. A library
build with `BUILD_TESTING=OFF` needs neither Python nor GoogleTest.

## Validate a change

- Add a regression case for a bug, including a rejected input or an independent
  expected result. Avoid tests that merely repeat the implementation.
- For pricing/risk changes, run the relevant `python/validate_*.py` script with
  `--require-quantlib`, as well as CTest. CI commands are in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).
- Format changed C++ files with the repository's `.clang-format`; CI uses
  `clang-format==23.1.3`. Run `clang-format --dry-run --Werror` on those files.
  Static analysis and sanitizer instructions are in the reproduction guide.
- Check documentation links, example commands, units, risk signs and data labels.
  Keep synthetic inputs clearly distinguished from captured market observations.

## Changes and evidence

Describe the problem, resulting behavior and checks run in each pull request.
Update [CHANGELOG.md](CHANGELOG.md) for user-visible changes. Report compiler,
platform and dependency versions when a failure depends on the environment.

Use ignored `build/` and `results/` directories for generated output. Committed
`docs/results/` records are historical evidence: do not overwrite them with a
new run or delete them just to tidy the tree. New published measurements should
identify their inputs, source revision, toolchain and limitations.

Main currently targets **2.4.0-dev**. CMake exports numeric package compatibility
as 2.4.0; package names and build information carry the development suffix.
Release preparation must align the CMake version/status, package documentation,
changelog and tested release artifacts before tagging.

## Report a problem

Open a [GitHub issue](https://github.com/Aniket2002/rates-engine-cpp/issues) with
the revision, platform, reproduction command, minimal inputs, expected behavior
and actual output. Remove private trade data and credentials from examples.
