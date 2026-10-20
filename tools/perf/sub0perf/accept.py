"""Recording a baseline, and accepting changes into it with a logged reason (the baseline's `accepted` list)."""

import time
from pathlib import Path

from .baseline import baseline_data, write_baseline
from .common import read_json
from .gitstate import git_head
from .judge import evaluate_with, names_with
from .runner import COUNTERS
from .stats import measured_count


def baseline_command(args):
    record = read_json(args.results)
    data = baseline_data(record)
    path = Path(args.baseline)
    if path.is_file():
        old = read_json(path)
        if not args.reason:
            print("bench_check: a baseline exists; re-recording it needs --reason (it is logged in the file)")
            return 2
        data["accepted"] = old.get("accepted", []) + [{
            "date": time.strftime("%Y-%m-%d"), "commit": git_head(path.parent) or "",
            "reason": args.reason, "benchmarks": "all (re-recorded)"}]
    write_baseline(path, data)
    print(f"wrote {len(data['benchmarks'])} benchmarks and {len(data['pairs'])} pairs to {args.baseline}")
    return 0


def without_data(record, names):
    """The names the samples record has no measured sample for (absent, empty, or only filled-in copies)."""
    return [n for n in names if measured_count(record, n) == 0]


def accept_command(args):
    """Moves the named (or every changed) benchmark's baseline to the measured value, with a logged reason."""
    record = read_json(args.results)
    fresh = baseline_data(record)
    path = Path(args.baseline)
    data = read_json(path) if path.is_file() else baseline_data(record)
    if not isinstance(next(iter(data.get("benchmarks", {}).values()), {}), dict):
        print("bench_check: the baseline is in the old format; re-record it with `baseline --reason ...` first")
        return 2
    if args.all_changed:
        rows, pair_rows, env = evaluate_with(record, path, args, False)
        names = names_with(rows, pair_rows, env, "REGRESSED", "IMPROVED")
    else:
        names = args.benchmark
    if not names:
        print("bench_check: nothing to accept")
        return 2
    # Without a measured sample there is nothing to move the baseline to: it would keep its old value and log no change.
    empty = without_data(record, names)
    if empty:
        print(f"bench_check: no measured samples in {args.results} for: {', '.join(empty)}\n"
              f"  measure them first, e.g. `run --exe ... --filter \"<name or group>\" --out samples.json`, then accept from that file")
        return 2
    scale = (data.get("calibration_ns") or fresh["calibration_ns"]) / fresh["calibration_ns"]
    changes = {}
    for n in names:
        new = dict(fresh["benchmarks"][n])
        new["median_ns"] = round(new["median_ns"] * scale, 3)  # expressed at the baseline's clock
        old = data["benchmarks"].get(n, {}).get("median_ns")
        changes[n] = {"from_ns": old, "to_ns": new["median_ns"],
                      "change": None if not old else round(new["median_ns"] / old - 1.0, 4)}
        for key in COUNTERS:
            if key in new:
                before = data["benchmarks"].get(n, {}).get(key)
                changes[n][key] = {"from": before, "to": new[key],
                                   "change": None if not before else round(new[key] / before - 1.0, 4)}
        data["benchmarks"][n] = new
        if n in fresh["pairs"]:
            changes[n]["pair_ratio"] = {"from": data.get("pairs", {}).get(n, {}).get("ratio"), "to": fresh["pairs"][n]["ratio"]}
            data.setdefault("pairs", {})[n] = fresh["pairs"][n]
    data.setdefault("accepted", []).append({"date": time.strftime("%Y-%m-%d"), "commit": git_head(path.parent) or "",
                                            "reason": args.reason, "benchmarks": changes})
    write_baseline(path, data)
    for n, c in changes.items():
        delta = "new" if c["change"] is None else f"time {c['change']:+.1%}"
        if c.get("instructions", {}).get("change") is not None:
            delta += f", instructions {c['instructions']['change']:+.1%}"
        print(f"accepted {n}: {delta}")
    print(f"logged in {path}; commit it with the change, and say why")
    return 0
