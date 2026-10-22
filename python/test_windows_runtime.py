"""Run relocated Windows binaries with only the operating system on PATH."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32":
        raise SystemExit("This check requires Windows")
    env = dict(os.environ)
    env["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
    env.pop("PYTHONPATH", None)
    with tempfile.TemporaryDirectory(prefix="rates portable ") as name:
        directory = Path(name)
        for executable in ["rates_cli", "rates_batch", "rates_simulate"]:
            target = directory / (executable + ".exe")
            shutil.copy2(args.bin_dir / target.name, target)
            subprocess.run([str(target), "--help"], cwd=directory, env=env,
                           check=True, capture_output=True, text=True, timeout=30)
        subprocess.run([str(directory / "rates_cli.exe"),
                        "--quotes", str(args.source_dir.resolve() / "examples/synthetic_quotes.csv"),
                        "--swap", str(args.source_dir.resolve() / "examples/sample_swap.csv"),
                        "--out", str(directory / "priced")], cwd=directory, env=env,
                       check=True, capture_output=True, text=True, timeout=30)
        if not (directory / "priced/summary.csv").is_file():
            raise AssertionError("Relocated CLI did not produce a valuation")
        modules = list(args.bin_dir.glob("pyrates*.pyd"))
        if modules:
            for module in modules:
                shutil.copy2(module, directory / module.name)
            subprocess.run([sys.executable, "-E", "-c",
                            "import pyrates; assert str(pyrates.Date('2025-01-02')) == '2025-01-02'"],
                           cwd=directory, env=env, check=True, capture_output=True, text=True, timeout=30)
    print("Portable runtime: relocated CLI/batch/simulator and available Python module passed with system-only PATH")


if __name__ == "__main__":
    main()
