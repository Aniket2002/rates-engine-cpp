"""Extract whole-process instruction counts and live-heap peaks from Valgrind."""
import argparse
import json
from pathlib import Path
import re


def summarize(root):
    results = {}
    for mode in ("cached", "uncached", "independent"):
        cpu = (root / f"callgrind-{mode}.out").read_text()
        heap = (root / f"massif-{mode}.out").read_text()
        instructions = re.search(r"^summary: (\d+)\s*$", cpu, re.MULTILINE)
        if instructions is None:
            raise ValueError(f"Missing Callgrind instruction total for {mode}")
        blocks = heap.split("snapshot=")[1:]
        peaks = []
        for block in blocks:
            live = int(re.search(r"^mem_heap_B=(\d+)$", block, re.MULTILINE)[1])
            extra = int(re.search(r"^mem_heap_extra_B=(\d+)$", block, re.MULTILINE)[1])
            peaks.append((live, live + extra))
        if not peaks:
            raise ValueError(f"Missing Massif snapshots for {mode}")
        results[mode] = dict(instructions=int(instructions[1]),
                             peak_live_heap_bytes=max(p[0] for p in peaks),
                             peak_heap_including_overhead_bytes=max(p[1] for p in peaks))
    return dict(scope="whole process: common setup, initial cached calculation, warm-up and one measured 100-trade invocation",
                caveat="Callgrind instructions are not CPU cycles; Massif live heap is not total allocated bytes",
                modes=results)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    (args.directory / "profile-summary.json").write_text(json.dumps(summarize(args.directory), indent=2)+"\n")
