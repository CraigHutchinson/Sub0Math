#!/usr/bin/env python3
"""Compares nanobench JSON results with a committed baseline and fails on regressions.

  bench_check.py check    --results run.json --baseline perf/baselines/<host>.json [--tolerance 0.10]
  bench_check.py baseline --results run.json --baseline perf/baselines/<host>.json

Each benchmark's median time per op is compared. A benchmark slower than baseline by more than the
tolerance fails the check; faster is reported, never failed. Baselines are per host: timings from
one machine say nothing about another. A missing baseline is reported and passes, so a new host can
adopt the gate by running `baseline` once and committing the file.

With GITHUB_STEP_SUMMARY set, a Markdown table is appended to the job summary.
"""

import argparse
import json
import os
import platform
import sys
from pathlib import Path


def load_results(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    out = {}
    for result in data["results"]:
        out[result["name"]] = {
            "median_ns": result["median(elapsed)"] / result["batch"] * 1e9,
            "error": result.get("medianAbsolutePercentError(elapsed)", 0.0),
        }
    return out


def baseline(args):
    results = load_results(args.results)
    record = {
        "host": platform.node(),
        "machine": platform.machine(),
        "system": f"{platform.system()} {platform.release()}",
        "note": "Median ns per op from nanobench. Regenerate deliberately; say why in the commit.",
        "benchmarks": {name: round(r["median_ns"], 3) for name, r in sorted(results.items())},
    }
    Path(args.baseline).parent.mkdir(parents=True, exist_ok=True)
    Path(args.baseline).write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {len(results)} benchmarks to {args.baseline}")
    return 0


def check(args):
    results = load_results(args.results)
    path = Path(args.baseline)
    if not path.is_file():
        print(f"no baseline at {path}; run `bench_check.py baseline` on this host to adopt the gate")
        return 0
    base = json.loads(path.read_text(encoding="utf-8"))["benchmarks"]
    rows, failures = [], 0
    for name, r in sorted(results.items()):
        if name not in base:
            rows.append((name, None, r["median_ns"], None, "new"))
            continue
        change = r["median_ns"] / base[name] - 1.0
        verdict = "REGRESSED" if change > args.tolerance else ("faster" if change < -args.tolerance else "ok")
        failures += verdict == "REGRESSED"
        rows.append((name, base[name], r["median_ns"], change, verdict))
    for name in sorted(set(base) - set(results)):
        rows.append((name, base[name], None, None, "missing"))

    lines = [f"| Benchmark | Baseline ns/op | Now ns/op | Change | Verdict (tolerance {args.tolerance:.0%}) |",
             "|---|---:|---:|---:|---|"]
    for name, b, n, c, v in rows:
        fmt = lambda x: "—" if x is None else f"{x:,.1f}"
        lines.append(f"| {name} | {fmt(b)} | {fmt(n)} | {'—' if c is None else f'{c:+.1%}'} | {v} |")
    report = "\n".join(lines)
    print(report)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"### Benchmarks against {path.name}\n\n{report}\n\n")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["check", "baseline"])
    parser.add_argument("--results", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--tolerance", type=float, default=0.10)
    args = parser.parse_args()
    return check(args) if args.command == "check" else baseline(args)


if __name__ == "__main__":
    sys.exit(main())
