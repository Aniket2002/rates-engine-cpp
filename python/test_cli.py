"""CLI parsing failures and configurable-convention end-to-end checks."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile

cli = str(Path(sys.argv[1]).resolve())
root = Path(__file__).resolve().parents[1]


def run(arguments):
    return subprocess.run([cli] + arguments, cwd=root, capture_output=True, text=True)


assert run(["--help"]).returncode == 0
for options in (["--unknown", "x"], ["--fixed-months", "0"], ["--fixed-months", "3.5"],
                ["--valuation", "2025-02-29"], ["--bump-bps", "nan"], ["--eom", "maybe"],
                ["--quotes", "missing-file.csv"], ["--fixed-dc", "unknown"], ["--swap"]):
    result = run(options)
    assert result.returncode != 0 and "rates_cli:" in result.stderr, (options, result)
with tempfile.TemporaryDirectory() as name:
    directory = Path(name)
    broken = directory / "bad.csv"
    for contents in ("wrong,header\n", "type,label,maturity,rate\n", "type,label,maturity,rate\ndeposit,1M,2025-02-02,nan\n",
                     "type,label,maturity,rate\ndeposit,1M,2025-02-02,0.03x\n"):
        broken.write_text(contents)
        result = run(["--quotes", str(broken), "--out", str(directory / "bad-output")])
        assert result.returncode != 0 and "rates_cli:" in result.stderr
    result = run(["--fixed-months", "12", "--floating-months", "6", "--fixed-dc", "ACT365F",
                  "--floating-dc", "ACT365F", "--deposit-dc", "ACT365F", "--adjustment", "following",
                  "--eom", "false", "--holidays", "2025-02-03", "--out", str(directory / "custom")])
    assert result.returncode == 0, result.stderr
    with (directory / "custom/curve.csv").open() as stream:
        nodes = list(csv.DictReader(stream))
    assert nodes[1]["date"] == "2025-02-04"  # Weekend plus explicit holiday.
    with (directory / "custom/cashflows.csv").open() as stream:
        flows = list(csv.DictReader(stream))
    assert len([row for row in flows if row["leg"] == "fixed"]) == 5
    assert len([row for row in flows if row["leg"] == "floating"]) == 10
print("CLI contract: malformed inputs rejected; custom schedule/day-count/holiday scenario passed")
