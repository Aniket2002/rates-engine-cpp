[CmdletBinding()]
param(
    [switch]$Test,
    [switch]$WithPython,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
$originalPath = $env:PATH
Push-Location $PSScriptRoot
try {
    $portable = Join-Path $PSScriptRoot '.tools/llvm-mingw-20261006-ucrt-x86_64/bin'
    $venv = Join-Path $PSScriptRoot '.venv/Scripts'
    if (Test-Path -LiteralPath $venv) { $env:PATH = "$venv;$env:PATH" }
    $compilerArgs = @()
    if (Test-Path -LiteralPath "$portable/clang++.exe") {
        $env:PATH = "$portable;$env:PATH"
        $compilerArgs = @("-DCMAKE_CXX_COMPILER=$portable/clang++.exe")
    } elseif (-not (Get-Command cl, g++, clang++ -ErrorAction SilentlyContinue)) {
        throw 'A C++20 compiler is required. Open an x64 Visual Studio Developer PowerShell, or follow docs/reproducibility.md.'
    }
    foreach ($tool in @('cmake', 'ninja', 'python')) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
            throw "Missing build tool: $tool. See docs/reproducibility.md."
        }
    }
    $preset = $Configuration.ToLowerInvariant()
    $python = (Get-Command python).Source
    $bindingArgs = @('-DRATES_BUILD_PYTHON=OFF')
    if ($WithPython) {
        $pybind = & $python -m pybind11 --cmakedir
        if ($LASTEXITCODE -ne 0) { throw 'Install bindings/test dependencies: python -m pip install pybind11 QuantLib "scipy>=1.14,<2"' }
        $bindingArgs = @('-DRATES_BUILD_PYTHON=ON', "-Dpybind11_DIR=$pybind")
    }
    & cmake --preset $preset @compilerArgs @bindingArgs "-DPython3_EXECUTABLE=$python" -DRATES_STATIC_RUNTIME=ON
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
    & cmake --build --preset $preset --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    if ($Test) {
        & ctest --preset $preset
        if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    }
    & "$PSScriptRoot/build/$preset/rates_cli.exe" --quotes examples/synthetic_quotes.csv --swap examples/sample_swap.csv --out results/demo
    if ($LASTEXITCODE -ne 0) { throw 'Example valuation failed' }
    Write-Host "Ready: build/$preset/rates_cli.exe. Example output: results/demo/summary.csv"
} finally {
    $env:PATH = $originalPath
    Pop-Location
}
