"""Measure LLVM source-based line/branch coverage of src/ and include/rates/.

Runs CTest (including CLI-based validation) in a fresh profile directory; no stale
profiles are reused. Test/dependency/CLI code is excluded from the library totals.
Failures are retained in the output and cause nonzero exit after report generation.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--llvm-cov", default="llvm-cov")
    parser.add_argument("--llvm-profdata", default="llvm-profdata")
    args = parser.parse_args()
    output = args.build_dir.resolve() / "coverage"
    profiles = output / ("profiles-" + uuid.uuid4().hex)
    profiles.mkdir(parents=True)
    env = dict(os.environ, LLVM_PROFILE_FILE=str(profiles / "%p-%m.profraw"))
    tests = subprocess.run(["ctest", "--test-dir", str(args.build_dir), "--output-on-failure"],
                           env=env, capture_output=True, text=True)
    (output / "ctest.log").write_text(tests.stdout + tests.stderr, encoding="utf-8")
    print(tests.stdout)
    raw = list(profiles.glob("*.profraw"))
    if not raw:
        raise RuntimeError("No profiles produced; configure with RATES_COVERAGE=ON")
    profile = output / "merged.profdata"
    subprocess.run([args.llvm_profdata, "merge", "-sparse", *map(str, raw), "-o", str(profile)], check=True)
    sources = sorted(ROOT.joinpath("src").glob("*.cpp")) + sorted(ROOT.joinpath("include/rates").glob("*.hpp"))
    common = [str(args.tests.resolve()), "-object", str(args.cli.resolve()),
              "-instr-profile=" + str(profile), *map(str, sources)]
    export = subprocess.check_output([args.llvm_cov, "export", *common], text=True)
    (output / "coverage.json").write_text(export, encoding="utf-8")
    data = json.loads(export)["data"][0]
    files = []
    for file in data["files"]:
        relative = Path(file["filename"]).resolve().relative_to(ROOT).as_posix()
        missed = []
        for branch in file.get("branches", []):
            if branch[4] == 0 or branch[5] == 0:
                missed.append(dict(line=branch[0], column=branch[1], true_count=branch[4], false_count=branch[5]))
        files.append(dict(file=relative, summary=file["summary"], uncovered_branches=missed))
    summary = dict(scope="src/*.cpp and include/rates/*.hpp; excludes tests, CLI, bindings and dependencies",
                   test_exit_code=tests.returncode, totals=data["totals"], files=files,
                   llvm_version=subprocess.check_output([args.llvm_cov, "--version"], text=True).strip())
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    report = subprocess.check_output([args.llvm_cov, "report", *common], text=True)
    (output / "report.txt").write_text(report, encoding="utf-8")
    print(report)
    show = [args.llvm_cov, "show", *common, "-show-line-counts-or-regions", "-show-branches=count"]
    (output / "uncovered.txt").write_text(subprocess.check_output(show, text=True), encoding="utf-8")
    subprocess.run([*show, "-format=html", "-output-dir=" + str(output / "html")], check=True)
    return tests.returncode


if __name__ == "__main__":
    sys.exit(main())
