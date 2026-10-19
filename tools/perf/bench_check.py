#!/usr/bin/env python3
"""Runs benchmarks interleaved and judges them against a per-host baseline: regressions, progressions,
noise, and paired comparisons with hand-written references.

  bench_check.py run      --exe BENCH --out samples.json [--rounds 5] [--epochs N] [--filter TEXT]
  bench_check.py check    --results samples.json --baseline perf/baselines/<host>.json
  bench_check.py baseline --results samples.json --baseline perf/baselines/<host>.json
  bench_check.py auto     --exe BENCH --baseline ... [--rounds 3] [--epochs 5] [--report-only] [--record]
  bench_check.py accept   --results samples.json --baseline ... --reason TEXT (--benchmark NAME ... | --all-changed)
  bench_check.py gate     [--repo DIR] [--confirm-rounds 4]                      (the pre-push hook; reads .git/sub0perf/config.json)
  bench_check.py install-hooks --repo DIR --build-dir DIR --target NAME --exe PATH --baseline PATH

How a run is made robust (`run`, and `auto` which runs then checks):

- Interleaved rounds. Every benchmark is measured once per round, each in its own short process
  (`--exact NAME`), in a freshly shuffled order every round (seeded, so a run can be repeated). Members of
  a group (names sharing the text before the first ", ") run back to back, in shuffled order, so a pair's
  ratio is taken from adjacent measurements. Slow drift (thermal, turbo, background work) then spreads
  over every benchmark instead of biasing the ones measured last.
- Calibration. A fixed dependent arithmetic chain (`BENCH --calibration`) is timed at the start and end
  of every round. Its time tracks the core's clock, so each round's timings are scaled by it, and its
  change from the first to the last round is reported as clock drift. A baseline records the
  calibration time too, so a whole run on a slower or throttled host is corrected before judging.
- System load. Before each round the CPU time used by other processes is sampled. Above --max-load the
  run waits (up to --settle seconds) for it to fall, then proceeds and marks the round as loaded.
- Variance. Each benchmark's spread across rounds is measured as a robust coefficient of variation
  (median absolute deviation x 1.4826 / median). A change counts only if it exceeds both the tolerance
  and three times the noise. A benchmark noisier than --max-cv is NOISY: `auto` gives it extra rounds
  (up to --max-rounds) and reports it as inconclusive if it stays noisy, rather than guessing.

Hardware counters. Where the OS exposes them (Linux perf_event; not Windows), nanobench reports
instructions, CPU cycles, branches and branch misses per op. They are kept with every record and
baseline. Instructions per op are nearly deterministic, so they are judged with a tight tolerance
(--instr-tolerance, 2%): a change that adds work shows even when timing noise would hide it. Cycles per
op are judged like time but do not depend on the clock speed. The report adds IPC and the branch-miss
rate, and pairs add the instruction ratio against the hand-written reference: exactly how much more
work the vocabulary does. A counter regression counts like a time regression.

Verdicts: REGRESSED (slower, significant), IMPROVED (faster, significant: a progression to record with
`baseline`), ok, NOISY, new, missing. Pairs: in every group containing a name with "hand-written" (the
fastest-known reference, never weakened), each other member's time is divided by the reference's in the
same round. That ratio is nearly independent of the host, so it is reported even with no baseline, and
compared with the baseline's ratio when there is one.

Local records and the pre-push gate. Every measurement of a clean working tree (the after-build check
with --record, and the gate) is kept as `.git/sub0perf/records/<target>/<commit>.json`, so the state a
branch was checked out at and the state about to be pushed are both on record, measured on this
machine. The pre-push hook (`gate`) builds each configured benchmark, measures HEAD, and blocks the
push on a regression against the committed baseline, or against the nearest earlier commit with a
local record (like for like: same machine, same session conditions), unless the regression has been
accepted. Accepting is explicit: `accept` moves the named benchmarks' baseline to the new measurement
and appends an entry (date, commit, reason, before and after) to the baseline's `accepted` log; commit
that file with the change. `git push --no-verify` bypasses the hook, as with any git hook.

Confirmation. A single short measurement is not enough evidence to block. In `auto` and `gate`, anything
judged REGRESSED on the first pass (a benchmark, a pair or a counter, against the baseline or the earlier
record) is measured again for --confirm-rounds more rounds (default 4; calibration as usual), and the
verdicts are recomputed from all rounds. Only regressions that persist block; the rest are reported as
"not confirmed". Where there are no controls (no hand-written reference), a change must also exceed 3x
the suite's own spread of changes (median absolute deviation across all benchmarks) to count, since an
environment shift cannot otherwise be told from a regression.

`check` and `auto` exit 1 on any regression unless --report-only. With GITHUB_STEP_SUMMARY set, the
report is appended to the job summary. Baselines from the earlier format (a number per benchmark) are
still read; they lack calibration and spread, so judging falls back to the suite's median drift.
"""

import argparse
import ctypes
import json
import os
import platform
import random
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

CALIBRATION = "__calibration"
REFERENCE_MARK = "hand-written"


# ── System load ─────────────────────────────────────────────────────────────────────────────────────

def _cpu_times():
    """Returns (busy, total) CPU time counters for the whole machine, or None if unavailable."""
    if sys.platform == "win32":
        class FileTime(ctypes.Structure):
            _fields_ = [("low", ctypes.c_uint32), ("high", ctypes.c_uint32)]
        idle, kernel, user = FileTime(), FileTime(), FileTime()
        if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
            return None
        value = lambda t: (t.high << 32) | t.low
        total = value(kernel) + value(user)  # kernel time includes idle time
        return total - value(idle), total
    try:
        fields = [int(x) for x in Path("/proc/stat").read_text().split("\n")[0].split()[1:]]
        idle = fields[3] + (fields[4] if len(fields) > 4 else 0)
        total = sum(fields)
        return total - idle, total
    except OSError:
        return None


def system_load(seconds=0.5):
    """Fraction of all CPU time spent busy over a short window (our own process is idle meanwhile)."""
    first = _cpu_times()
    if first is None:
        if hasattr(os, "getloadavg"):
            return min(1.0, os.getloadavg()[0] / (os.cpu_count() or 1))
        return 0.0
    time.sleep(seconds)
    second = _cpu_times()
    busy, total = second[0] - first[0], second[1] - first[1]
    return busy / total if total > 0 else 0.0


def wait_for_quiet(max_load, settle):
    """Waits up to `settle` seconds for the load to fall to `max_load`; returns (load, waited, quiet)."""
    start = time.monotonic()
    load = system_load()
    while load > max_load and time.monotonic() - start < settle:
        time.sleep(1.0)
        load = system_load()
    return load, time.monotonic() - start, load <= max_load


# ── Running ─────────────────────────────────────────────────────────────────────────────────────────

PIN_CPU = None  # set from --cpu: run every measurement on this one logical CPU


def _launch(cmd):
    """Starts a measurement process at raised priority, pinned to PIN_CPU when set, and waits for it.

    Pinning stops the scheduler moving the benchmark between cores mid-run (cold caches, different clocks);
    raised priority keeps ordinary background work from preempting it.
    """
    if sys.platform == "win32":
        high_priority = 0x00000080  # HIGH_PRIORITY_CLASS
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=high_priority)
        if PIN_CPU is not None:
            ctypes.windll.kernel32.SetProcessAffinityMask(int(proc._handle), ctypes.c_size_t(1 << PIN_CPU))
        code = proc.wait()
    else:
        def pin():
            if PIN_CPU is not None and hasattr(os, "sched_setaffinity"):
                os.sched_setaffinity(0, {PIN_CPU})
        code = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, preexec_fn=pin).returncode
    if code != 0:
        raise subprocess.CalledProcessError(code, cmd)


COUNTERS = {"instructions": "median(instructions)", "cycles": "median(cpucycles)",
            "branches": "median(branchinstructions)", "branch_misses": "median(branchmisses)"}


def measure_full(exe, args, scratch):
    """Runs the benchmark executable once; returns {name: (ns per op, {counter: per op})}.

    Counters are included only when the platform provides them (nanobench reports 0 otherwise).
    """
    out = Path(scratch) / "one.json"
    _launch([exe, *args, "--json", str(out)])
    data = json.loads(out.read_text(encoding="utf-8"))
    results = {}
    for r in data["results"]:
        batch = r["batch"] or 1.0
        counters = {k: r[f] / batch for k, f in COUNTERS.items() if r.get(f)}
        results[r["name"]] = (r["median(elapsed)"] / batch * 1e9, counters)
    return results


def measure(exe, args, scratch):
    """Runs the benchmark executable once; returns {name: ns per op}."""
    return {name: ns for name, (ns, _) in measure_full(exe, args, scratch).items()}


def list_names(exe, text_filter):
    args = [exe, "--list"] + (["--filter", text_filter] if text_filter else [])
    out = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def group_of(name):
    return name.split(", ", 1)[0]


def run_rounds(exe, names, rounds, epochs, max_load, settle, seed, first_round=0, samples=None, meta=None, positions=None,
               counters=None):
    samples = samples if samples is not None else {n: [] for n in names}
    counters = counters if counters is not None else {}
    positions = positions if positions is not None else {n: [] for n in names}
    meta = meta if meta is not None else []
    extra = ["--epochs", str(epochs)] if epochs else []
    groups = {}
    for n in names:
        groups.setdefault(group_of(n), []).append(n)
    with tempfile.TemporaryDirectory() as scratch:
        for r in range(first_round, first_round + rounds):
            rng = random.Random(seed * 1000003 + r)
            load, waited, quiet = wait_for_quiet(max_load, settle)
            cal_start = measure(exe, ["--calibration", *extra], scratch)[CALIBRATION]
            order = list(groups)
            rng.shuffle(order)
            sequence = []
            for g in order:
                members = list(groups[g])
                rng.shuffle(members)
                sequence += members
            for k, n in enumerate(sequence):
                ns, found = measure_full(exe, ["--exact", n, *extra], scratch)[n]
                samples.setdefault(n, []).append(ns)
                for key, value in found.items():
                    counters.setdefault(n, {}).setdefault(key, []).append(value)
                # Where in the round it ran (0 = start, 1 = end), to interpolate the clock between calibrations.
                positions.setdefault(n, []).append(round((k + 0.5) / len(sequence), 4))
            cal_end = measure(exe, ["--calibration", *extra], scratch)[CALIBRATION]
            meta.append({"round": r, "load": round(load, 4), "waited_s": round(waited, 1), "quiet": quiet,
                         "calibration_start_ns": cal_start, "calibration_end_ns": cal_end})
            print(f"round {r + 1}: load {load:.0%}{'' if quiet else ' (busy)'}, calibration {cal_start:,.0f}/{cal_end:,.0f} ns,"
                  f" {sum(len(m) for m in groups.values())} benchmarks", flush=True)
    return samples, meta, positions, counters


def run_command(args):
    names = list_names(args.exe, args.filter)
    samples, meta, positions, counters = run_rounds(args.exe, names, args.rounds, args.epochs, args.max_load, args.settle, args.seed)
    write_samples(args.out, args.exe, samples, meta, positions, args.epochs, counters)
    return 0


def write_samples(path, exe, samples, meta, positions, epochs=0, counters=None):
    record = {"format": 2, "exe": Path(exe).name, "epochs": epochs, "cpu": PIN_CPU, "counters": counters or {}, "host": platform.node(), "machine": platform.machine(),
              "system": f"{platform.system()} {platform.release()}", "rounds": meta, "samples": samples,
              "positions": positions}
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(record, indent=1) + "\n", encoding="utf-8", newline="\n")


# ── Statistics ──────────────────────────────────────────────────────────────────────────────────────

def robust_cv(values):
    if len(values) < 2:
        return 0.0
    m = statistics.median(values)
    mad = statistics.median(abs(v - m) for v in values)
    return 1.4826 * mad / m if m else 0.0


def summarise(record):
    """Per-benchmark median and spread after scaling each sample by the clock at the time it ran.

    The clock is the calibration time, interpolated linearly between the round's start and end
    calibrations at the sample's position in the round; a slow moment (high calibration) scales down.
    """
    rounds = record["rounds"]
    cal = [(r["calibration_start_ns"] + r["calibration_end_ns"]) / 2 for r in rounds]
    cal_median = statistics.median(cal)
    positions = record.get("positions", {})
    out = {}
    for name, values in record["samples"].items():
        where = positions.get(name, [])
        scaled = []
        for i, v in enumerate(values[:len(rounds)]):
            t = where[i] if i < len(where) else 0.5
            clock = rounds[i]["calibration_start_ns"] + t * (rounds[i]["calibration_end_ns"] - rounds[i]["calibration_start_ns"])
            scaled.append(v * cal_median / clock)
        found = record.get("counters", {}).get(name, {})
        out[name] = {"median_ns": statistics.median(scaled), "cv": robust_cv(scaled), "n": len(scaled), "rounds": scaled,
                     "counters": {k: {"median": statistics.median(v), "cv": robust_cv(v)} for k, v in found.items() if v}}
    drift = (rounds[-1]["calibration_end_ns"] / rounds[0]["calibration_start_ns"] - 1.0) if rounds else 0.0
    return out, cal_median, robust_cv(cal), drift


def pair_ratios(stats):
    """For each group with a hand-written reference: member/reference per round, then median and spread."""
    groups = {}
    for name in stats:
        groups.setdefault(group_of(name), []).append(name)
    pairs = {}
    for g, members in groups.items():
        refs = [m for m in members if REFERENCE_MARK in m]
        if len(refs) != 1 or len(members) < 2:
            continue
        ref = refs[0]
        for m in members:
            if m == ref:
                continue
            per_round = [a / b for a, b in zip(stats[m]["rounds"], stats[ref]["rounds"]) if b > 0]
            if per_round:
                pairs[m] = {"reference": ref, "ratio": statistics.median(per_round), "cv": robust_cv(per_round)}
                mi = stats[m]["counters"].get("instructions")
                ri = stats[ref]["counters"].get("instructions")
                if mi and ri and ri["median"] > 0:
                    pairs[m]["instruction_ratio"] = mi["median"] / ri["median"]
    return pairs


# ── Baselines and judging ───────────────────────────────────────────────────────────────────────────

def load_baseline(path):
    return baseline_tuple(json.loads(Path(path).read_text(encoding="utf-8")))


def baseline_tuple(data):
    benchmarks = {}
    for name, v in data.get("benchmarks", {}).items():
        benchmarks[name] = v if isinstance(v, dict) else {"median_ns": v, "cv": None}
    return benchmarks, data.get("calibration_ns"), data.get("pairs", {})


def baseline_data(record):
    """A format-2 baseline object built from a samples record."""
    stats, cal, _, _ = summarise(record)
    pairs = pair_ratios(stats)
    return {"format": 2, "host": record.get("host", platform.node()), "machine": record.get("machine", platform.machine()),
            "system": record.get("system", f"{platform.system()} {platform.release()}"),
            "note": "Medians per op after calibration scaling, with robust CV across interleaved rounds. "
                    "Regenerate deliberately; say why in the commit. Regressions are accepted with `accept`.",
            "calibration_ns": round(cal, 3), "epochs": record.get("epochs", 0), "cpu": record.get("cpu"),
            "benchmarks": {n: {"median_ns": round(s["median_ns"], 3), "cv": round(s["cv"], 4), "rounds": s["n"],
                               **{k: round(c["median"], 2) for k, c in s["counters"].items()}}
                           for n, s in sorted(stats.items())},
            "pairs": {n: {"reference": p["reference"], "ratio": round(p["ratio"], 4),
                          **({"instruction_ratio": round(p["instruction_ratio"], 4)} if "instruction_ratio" in p else {})}
                      for n, p in sorted(pairs.items())},
            "accepted": []}


def write_baseline(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def baseline_command(args):
    record = json.loads(Path(args.results).read_text(encoding="utf-8"))
    data = baseline_data(record)
    path = Path(args.baseline)
    if path.is_file():
        old = json.loads(path.read_text(encoding="utf-8"))
        if not args.reason:
            print("bench_check: a baseline exists; re-recording it needs --reason (it is logged in the file)")
            return 2
        data["accepted"] = old.get("accepted", []) + [{
            "date": time.strftime("%Y-%m-%d"), "commit": git_head(Path(args.baseline).parent) or "",
            "reason": args.reason, "benchmarks": "all (re-recorded)"}]
    write_baseline(path, data)
    print(f"wrote {len(data['benchmarks'])} benchmarks and {len(data['pairs'])} pairs to {args.baseline}")
    return 0


def accept_command(args):
    """Moves the named (or every changed) benchmark's baseline to the measured value, with a logged reason."""
    record = json.loads(Path(args.results).read_text(encoding="utf-8"))
    fresh = baseline_data(record)
    path = Path(args.baseline)
    data = json.loads(path.read_text(encoding="utf-8")) if path.is_file() else baseline_data(record)
    if not isinstance(next(iter(data.get("benchmarks", {}).values()), {}), dict):
        print("bench_check: the baseline is in the old format; re-record it with `baseline --reason ...` first")
        return 2
    if args.all_changed:
        rows, pair_rows, env = evaluate(record, path, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute, False,
                                        args.instr_tolerance)
        names = ([r[0] for r in rows if r[5] in ("REGRESSED", "IMPROVED")] + [p[0] for p in pair_rows if p[4] in ("REGRESSED", "IMPROVED")]
                 + [row["name"] for row in env["counter_rows"] if row["verdicts"]])
    else:
        names = args.benchmark
    unknown = [n for n in names if n not in fresh["benchmarks"]]
    if unknown or not names:
        print(f"bench_check: nothing to accept{'; not measured: ' + ', '.join(unknown) if unknown else ''}")
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


def judge(change, noise, tolerance, cv, max_cv, spread=0.0):
    """Verdict for a change. `spread` is the suite's own spread of changes, used only where there are no controls."""
    if cv > max_cv:
        return "NOISY"
    threshold = max(tolerance, 3.0 * noise, 3.0 * spread)
    if change > threshold:
        return "REGRESSED"
    if change < -threshold:
        return "IMPROVED"
    return "ok"


def evaluate(record, baseline_path, tolerance, pair_tolerance, max_cv, absolute, filtered=False, instr_tolerance=0.02):
    stats, cal, cal_cv, cal_drift = summarise(record)
    pairs = pair_ratios(stats)
    if isinstance(baseline_path, dict):
        have_baseline = True
        base, base_cal, base_pairs = baseline_tuple(baseline_path)
    else:
        have_baseline = Path(baseline_path).is_file()
        base, base_cal, base_pairs = load_baseline(baseline_path) if have_baseline else ({}, None, {})

    common = [n for n in stats if n in base]
    if absolute:
        factor, mode = 1.0, "absolute"
    elif base_cal:
        factor, mode = cal / base_cal, f"scaled by calibration ({cal / base_cal - 1.0:+.1%} host speed vs baseline)"
    elif len(common) >= 3:
        factor = statistics.median(stats[n]["median_ns"] / base[n]["median_ns"] for n in common)
        mode = f"relative to the suite's median drift ({factor - 1.0:+.1%}); baseline has no calibration"
    else:
        factor, mode = 1.0, "absolute"

    # Controls: hand-written references do not use the library under test, so whatever moves them beyond
    # the clock is the environment (cache and memory contention, other load). Their median change is
    # factored out; a library regression cannot hide in it.
    controls = [n for n in common if REFERENCE_MARK in n]
    environment_shift = None
    if not absolute and controls:
        control = statistics.median(stats[n]["median_ns"] / (base[n]["median_ns"] * factor) for n in controls)
        factor *= control
        environment_shift = control - 1.0
        mode += f"; {len(controls)} control(s) (hand-written references) moved {environment_shift:+.1%} beyond that, factored out"
    elif not absolute and base:
        mode += "; no control benchmarks (add a hand-written reference to enable environment correction)"

    # Without controls nothing separates a library change from an environment shift, so the spread of
    # changes across the whole suite (median absolute deviation) is an extra noise floor: one benchmark
    # must stand out from the others, not just from its baseline.
    changes = {n: stats[n]["median_ns"] / (base[n]["median_ns"] * factor) - 1.0 for n in common}
    suite_spread = None
    if not controls and len(changes) >= 3:
        centre = statistics.median(changes.values())
        suite_spread = statistics.median(abs(c - centre) for c in changes.values())
    spread_floor = suite_spread or 0.0

    rows = []
    for name, s in sorted(stats.items()):
        if name not in base:
            rows.append((name, None, s, None, None, "new"))
            continue
        b = base[name]
        raw = s["median_ns"] / b["median_ns"] - 1.0
        change = changes[name]
        noise = max(s["cv"], b["cv"] or 0.0)
        rows.append((name, b, s, raw, change, judge(change, noise, tolerance, s["cv"], max_cv, spread_floor)))
    # When the controls (which do not use the library) moved by more than the tolerance, the environment
    # changed too much for absolute times to support a verdict either way: report them as ENVIRONMENT
    # (inconclusive). Pair ratios and instruction counts are relative to the same environment and still judge.
    environment_unstable = environment_shift is not None and abs(environment_shift) > tolerance
    if environment_unstable:
        rows = [(n, b, st, raw, ch, "ENVIRONMENT" if v in ("REGRESSED", "IMPROVED") else v)
                for (n, b, st, raw, ch, v) in rows]
    if not filtered:  # a filtered run selects a subset; the rest are not missing
        for name in sorted(set(base) - set(stats)):
            rows.append((name, base[name], None, None, None, "missing"))

    counter_rows = []
    for name, s in sorted(stats.items()):
        c = s["counters"]
        if not c:
            continue
        b = base.get(name, {})
        row = {"name": name, "now": {k: v["median"] for k, v in c.items()}, "verdicts": []}
        for key, tol in (("instructions", instr_tolerance), ("cycles", tolerance)):
            if key in c and b.get(key):
                change = c[key]["median"] / b[key] - 1.0
                row[key + "_change"] = change
                verdict = judge(change, c[key]["cv"], tol, 0.0, 1.0)
                if verdict in ("REGRESSED", "IMPROVED"):
                    row["verdicts"].append(f"{key} {verdict}")
        counter_rows.append(row)

    pair_rows = []
    for name, p in sorted(pairs.items()):
        bp = base_pairs.get(name)
        if bp:
            change = p["ratio"] / bp["ratio"] - 1.0
            verdict = judge(change, p["cv"], pair_tolerance, p["cv"], max_cv)
        else:
            change, verdict = None, "new"
        pair_rows.append((name, p, bp, change, verdict))

    base_epochs = (baseline_path.get("epochs", 0) if isinstance(baseline_path, dict)
                   else json.loads(Path(baseline_path).read_text(encoding="utf-8")).get("epochs", 0) if have_baseline else 0)
    base_data = baseline_path if isinstance(baseline_path, dict) else (
        json.loads(Path(baseline_path).read_text(encoding="utf-8")) if have_baseline else {})
    method_notes = []
    if record.get("epochs", 0) != base_epochs:
        method_notes.append(f"epochs differ (now {record.get('epochs', 0) or 'default'}, baseline {base_epochs or 'default'})")
    if record.get("cpu") is not None and base_data.get("cpu") is not None and record.get("cpu") != base_data.get("cpu"):
        method_notes.append(f"pinned CPU differs (now {record.get('cpu')}, baseline {base_data.get('cpu')}; cores of a hybrid CPU differ in speed)")
    env = {"method": None if not method_notes else
           "; ".join(method_notes) + ": absolute times are not like for like; pair ratios still are",
           "rounds": len(record["rounds"]), "busy_rounds": sum(1 for r in record["rounds"] if not r["quiet"]),
           "max_load": max((r["load"] for r in record["rounds"]), default=0.0), "calibration_ns": cal,
           "calibration_cv": cal_cv, "clock_drift": cal_drift, "mode": mode, "baseline": have_baseline,
           "counter_rows": counter_rows, "instr_tolerance": instr_tolerance, "environment_shift": environment_shift,
           "tolerance": tolerance, "suite_spread": suite_spread}
    if method_notes:  # absolute times measured differently cannot support a verdict either way
        rows = [(n, b, st, raw, ch, "ENVIRONMENT" if v in ("REGRESSED", "IMPROVED") else v)
                for (n, b, st, raw, ch, v) in rows]
    return rows, pair_rows, env


def render(rows, pair_rows, env, baseline_path, tolerance, pair_tolerance):
    pct = lambda x: "-" if x is None else f"{x:+.1%}"
    num = lambda x: "-" if x is None else f"{x:,.1f}"
    lines = [f"**Environment:** {env['rounds']} interleaved rounds; load max {env['max_load']:.0%}"
             f" ({env['busy_rounds']} round(s) above the limit); calibration {env['calibration_ns']:,.0f} ns,"
             f" spread {env['calibration_cv']:.1%}, clock drift first to last {env['clock_drift']:+.1%}."]
    shift = env.get("environment_shift")
    if shift is not None and abs(shift) > env.get("tolerance", 0.1) / 2:
        lines.append(f"> The environment shifted {shift:+.1%} beyond the clock between the two measurements (seen in the"
                     " controls, which do not use the library). It is factored out, but absolute verdicts are less"
                     " certain than usual; pair ratios and instruction counts are unaffected.")
    if env.get("method"):
        lines.append(f"> Method mismatch: {env['method']}.")
    if abs(env["clock_drift"]) > 0.05:
        lines.append(f"> Clock drift of {env['clock_drift']:+.1%} during the run (thermal or power state); rounds are"
                     " scaled by calibration, but treat close calls with care.")
    if not env["baseline"]:
        lines.append(f"No baseline at `{baseline_path}`: run `bench_check.py baseline` on this host to adopt the gate."
                     " Pair ratios below are still meaningful without one.")
    else:
        lines.append(f"Judged {env['mode']}; tolerance {tolerance:.0%} or 3x the noise, whichever is larger.")
        if env.get("suite_spread") is not None:
            lines.append(f"No controls: suite spread {env['suite_spread']:.1%} used as an extra noise floor"
                         f" (a change must also exceed 3x it, {3 * env['suite_spread']:.1%}).")

    def section(title, wanted):
        picked = [r for r in rows if r[5] in wanted]
        if not picked:
            return
        lines.append(f"\n**{title}**\n")
        lines.append("| Benchmark | Baseline ns | Now ns | Raw | Judged | Spread | Verdict |")
        lines.append("|---|---:|---:|---:|---:|---:|---|")
        for name, b, s, raw, change, v in picked:
            spread = "-" if not s else format(s["cv"], ".1%")
            base_ns = num(b["median_ns"] if b else None)
            now_ns = num(s["median_ns"] if s else None)
            lines.append(f"| {name} | {base_ns} | {now_ns} | {pct(raw)} | {pct(change)} | {spread} | {v} |")

    section("Regressions", {"REGRESSED"})
    section("Progressions (record them: run the baseline target and commit it, saying why)", {"IMPROVED"})
    section("Inconclusive: too noisy to judge", {"NOISY"})
    section("Inconclusive: the environment shifted beyond the tolerance (see the controls); pairs and counters still judge",
            {"ENVIRONMENT"})
    section("Unchanged, new or missing", {"ok", "new", "missing"})

    counter_rows = env.get("counter_rows", [])
    if counter_rows:
        lines.append(f"\n**Hardware counters per op** (instructions tolerance {env['instr_tolerance']:.0%};"
                     " cycles do not depend on clock speed)\n")
        lines.append("| Benchmark | Instructions | Change | Cycles | Change | IPC | Branch misses | Verdict |")
        lines.append("|---|---:|---:|---:|---:|---:|---:|---|")
        for row in counter_rows:
            now = row["now"]
            ins, cyc = now.get("instructions"), now.get("cycles")
            ipc = f"{ins / cyc:.2f}" if ins and cyc else "-"
            miss = f"{now['branch_misses'] / now['branches']:.2%}" if now.get("branches") and "branch_misses" in now else "-"
            lines.append(f"| {row['name']} | {num(ins)} | {pct(row.get('instructions_change'))} | {num(cyc)}"
                         f" | {pct(row.get('cycles_change'))} | {ipc} | {miss} | {', '.join(row['verdicts']) or 'ok'} |")
    else:
        lines.append("\nHardware counters: not available on this platform (Linux perf_event provides instructions and cycles).")

    if pair_rows:
        lines.append(f"\n**Pairs against hand-written references** (ratio > 1 means slower than the reference;"
                     f" pair tolerance {pair_tolerance:.0%})\n")
        lines.append("| Benchmark | Reference | Ratio now | Baseline ratio | Change | Spread | Instruction ratio | Verdict |")
        lines.append("|---|---|---:|---:|---:|---:|---:|---|")
        for name, p, bp, change, v in pair_rows:
            base_ratio = "-" if not bp else format(bp["ratio"], ".3f") + "x"
            instr = format(p["instruction_ratio"], ".3f") + "x" if "instruction_ratio" in p else "-"
            lines.append(f"| {name} | {p['reference']} | {p['ratio']:.3f}x | {base_ratio} | {pct(change)} | {p['cv']:.1%} | {instr} | {v} |")
    return "\n".join(lines)


def report(record, args):
    rows, pair_rows, env = evaluate(record, args.baseline, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute,
                                    bool(getattr(args, "filter", "")), args.instr_tolerance)
    text = render(rows, pair_rows, env, args.baseline, args.tolerance, args.pair_tolerance)
    print(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"### {record.get('exe', 'Benchmarks')} against {Path(args.baseline).name}\n\n{text}\n\n")
    counter_verdicts = [v for row in env["counter_rows"] for v in row["verdicts"]]
    regressed = (sum(r[5] == "REGRESSED" for r in rows) + sum(p[4] == "REGRESSED" for p in pair_rows)
                 + sum(v.endswith("REGRESSED") for v in counter_verdicts))
    improved = (sum(r[5] == "IMPROVED" for r in rows) + sum(p[4] == "IMPROVED" for p in pair_rows)
                + sum(v.endswith("IMPROVED") for v in counter_verdicts))
    noisy = sum(r[5] in ("NOISY", "ENVIRONMENT") for r in rows) + sum(p[4] == "NOISY" for p in pair_rows)
    print(f"\nperf: {regressed} regressed, {improved} improved, {noisy} inconclusive")
    return rows, pair_rows, regressed


def check_command(args):
    record = json.loads(Path(args.results).read_text(encoding="utf-8"))
    _, _, regressed = report(record, args)
    return 0 if args.report_only or not regressed else 1


def extend_rounds(run, names, rounds, samples, meta, positions, counters):
    """Adds `rounds` rounds measuring only `names`; the other benchmarks repeat their median so rounds stay aligned.

    `run` has run_rounds' signature minus the process arguments, so a test can pass a stub that measures nothing.
    """
    for _ in range(rounds):
        before = {n: len(v) for n, v in samples.items()}
        run(sorted(names), 1, len(meta), samples, meta, positions, counters)
        for n, v in samples.items():
            if len(v) == before[n]:
                v.append(statistics.median(v))
                positions.setdefault(n, []).append(0.5)


def regressions_in(evaluations):
    """From evaluate() results [(rows, pair_rows, env)]: (every regressed name, the names to re-measure).

    Covers benchmarks, pairs and counter rows. Re-measuring a pair needs its reference too.
    """
    flagged, remeasure = set(), set()
    for rows, pair_rows, env in evaluations:
        for r in rows:
            if r[5] == "REGRESSED":
                flagged.add(r[0])
                remeasure.add(r[0])
        for p in pair_rows:
            if p[4] == "REGRESSED":
                flagged.add(p[0])
                remeasure |= {p[0], p[1]["reference"]}
        for row in env["counter_rows"]:
            if any(v.endswith("REGRESSED") for v in row["verdicts"]):
                flagged.add(row["name"])
                remeasure.add(row["name"])
    return flagged, remeasure


def confirm_regressions(evaluate_all, remeasure_rounds, confirm_rounds):
    """Judges, and when something regressed re-measures just that and judges again with all rounds.

    `evaluate_all()` returns the evaluate() results to inspect; `remeasure_rounds(names, n)` adds rounds.
    Returns (first-pass regressions, regressions that persist).
    """
    first, remeasure = regressions_in(evaluate_all())
    if not first or confirm_rounds <= 0:
        return first, first
    print(f"sub0perf: {len(first)} regression(s) on the first pass ({', '.join(sorted(first))});"
          f" confirming with {confirm_rounds} more round(s) for {len(remeasure)} benchmark(s)", flush=True)
    remeasure_rounds(remeasure, confirm_rounds)
    final, _ = regressions_in(evaluate_all())
    print(f"sub0perf: confirmed: {', '.join(sorted(first & final)) or 'none'}")
    print(f"sub0perf: not confirmed: {', '.join(sorted(first - final)) or 'none'}")
    if final - first:
        print(f"sub0perf: also regressed after confirmation: {', '.join(sorted(final - first))}")
    return first, final


def auto_command(args):
    if os.environ.get("SUB0MATH_PERF_SKIP"):
        return 0  # the gate builds benchmarks itself and measures them once
    names = list_names(args.exe, args.filter)
    samples, meta, positions, counters = run_rounds(args.exe, names, args.rounds, args.epochs, args.max_load, args.settle, args.seed)
    record = {"rounds": meta, "samples": samples, "positions": positions, "counters": counters, "exe": Path(args.exe).name,
              "epochs": args.epochs, "cpu": PIN_CPU,
              "host": platform.node(), "machine": platform.machine(), "system": f"{platform.system()} {platform.release()}"}
    # Benchmarks too noisy to judge get more rounds, up to --max-rounds in total.
    while len(meta) < args.max_rounds:
        rows, pair_rows, _ = evaluate(record, args.baseline, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute,
                                      bool(args.filter))
        noisy = {r[0] for r in rows if r[5] == "NOISY"} | {p[0] for p in pair_rows if p[4] == "NOISY"}
        noisy |= {p[1]["reference"] for p in pair_rows if p[4] == "NOISY"}
        if not noisy:
            break
        print(f"{len(noisy)} benchmark(s) too noisy; adding a round for them", flush=True)
        # Extra rounds re-measure only the noisy set; the others repeat their median so rounds stay aligned.
        before = {n: len(v) for n, v in samples.items()}
        run_rounds(args.exe, sorted(noisy), 1, args.epochs, args.max_load, args.settle, args.seed, len(meta), samples, meta,
                   positions, counters)
        for n, v in samples.items():
            if len(v) == before[n]:
                v.append(statistics.median(v))
                positions[n].append(0.5)
    def rerun(names, n):
        extend_rounds(lambda ns, r, first, *rest: run_rounds(args.exe, ns, r, args.epochs, args.max_load, args.settle, args.seed,
                                                              first, *rest), names, n, samples, meta, positions, counters)
    confirm_regressions(lambda: [evaluate(record, args.baseline, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute,
                                          bool(args.filter), args.instr_tolerance)], rerun, args.confirm_rounds)
    if args.out:
        write_samples(args.out, args.exe, samples, meta, positions, args.epochs, counters)
    if args.record and args.target:
        save_record(Path(args.repo or "."), args.target, record, only_if_missing=True)
    _, _, regressed = report(record, args)
    return 0 if args.report_only or not regressed else 1


# ── Git: local records and the pre-push gate ───────────────────────────────────────────────────────

def git(repo, *args):
    result = subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def git_head(repo):
    return git(repo, "rev-parse", "HEAD")


def tree_clean(repo):
    """True when no tracked file differs from HEAD (untracked files do not change what is built)."""
    return git(repo, "status", "--porcelain", "--untracked-files=no") == ""


def perf_dir(repo):
    common = git(repo, "rev-parse", "--git-common-dir")
    if common is None:
        return None
    base = Path(common) if Path(common).is_absolute() else Path(repo) / common
    return base / "sub0perf"


def log_event(repo, event):
    d = perf_dir(repo)
    if d is None:
        return
    d.mkdir(parents=True, exist_ok=True)
    event = {"time": time.strftime("%Y-%m-%dT%H:%M:%S"), **event}
    with open(d / "log.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps(event) + "\n")


def save_record(repo, target, record, only_if_missing=False):
    """Keeps a clean tree's measurement as the record for its commit."""
    d = perf_dir(repo)
    head = git_head(repo)
    if d is None or head is None or not tree_clean(repo):
        return None
    path = d / "records" / target / f"{head}.json"
    if only_if_missing and path.is_file():
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps({**record, "commit": head}, indent=1) + "\n", encoding="utf-8", newline="\n")
    log_event(repo, {"event": "record", "target": target, "commit": head})
    return path


def reference_record(repo, target, upstream=None, limit=200):
    """The record of the nearest earlier commit (the upstream first, when it has one)."""
    d = perf_dir(repo)
    if d is None:
        return None, None
    records = d / "records" / target
    candidates = ([upstream] if upstream else []) + (git(repo, "rev-list", f"--max-count={limit}", "HEAD~1") or "").split()
    for sha in candidates:
        if sha and (records / f"{sha}.json").is_file():
            return sha, json.loads((records / f"{sha}.json").read_text(encoding="utf-8"))
    return None, None


def accepted_since(repo, baseline_rel, reference_sha):
    """Benchmarks with an acceptance in HEAD's baseline that the reference commit's baseline lacks."""
    def entries(text):
        if not text:
            return []
        return json.loads(text).get("accepted", [])
    now = entries(Path(repo, baseline_rel).read_text(encoding="utf-8") if Path(repo, baseline_rel).is_file() else "")
    before = entries(git(repo, "show", f"{reference_sha}:{baseline_rel}") if reference_sha else "")
    names = set()
    for e in now[len(before):]:
        if isinstance(e.get("benchmarks"), dict):
            names |= set(e["benchmarks"])
        else:
            names.add("*")
    return names


def gate_command(args):
    repo = Path(args.repo or git(".", "rev-parse", "--show-toplevel") or ".")
    d = perf_dir(repo)
    config_path = d / "config.json" if d else None
    if not config_path or not config_path.is_file():
        print("sub0perf: no benchmarks configured for this repository; nothing to gate")
        return 0
    config = json.loads(config_path.read_text(encoding="utf-8"))
    head = git_head(repo)
    clean = tree_clean(repo)
    upstream = git(repo, "rev-parse", "@{upstream}")
    blocked = []
    for target, t in config.get("targets", {}).items():
        print(f"sub0perf: pre-push gate for {target} at {head[:9]}{'' if clean else ' (working tree has uncommitted changes)'}")
        command = ["cmake", "--build", t["build_dir"], "--target", target] + (["--config", t["config"]] if t.get("config") else [])
        build = subprocess.run(command, env={**os.environ, "SUB0MATH_PERF_SKIP": "1"})
        if build.returncode != 0:
            print(f"sub0perf: building {target} failed; push blocked")
            blocked.append(target)
            continue
        exe = t["exe"]
        names = list_names(exe, "")
        samples, meta, positions, counters = run_rounds(exe, names, args.rounds, args.epochs, args.max_load, args.settle, args.seed)
        record = {"rounds": meta, "samples": samples, "positions": positions, "counters": counters, "exe": Path(exe).name,
                  "epochs": args.epochs, "cpu": PIN_CPU,
                  "host": platform.node(), "machine": platform.machine(), "system": f"{platform.system()} {platform.release()}"}
        args.baseline = t["baseline"]
        ref_sha, ref = reference_record(repo, target, upstream)

        def both(record=record, ref=ref, baseline=t["baseline"]):
            found = [evaluate(record, baseline, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute, False,
                              args.instr_tolerance)]
            if ref:
                found.append(evaluate(record, baseline_data(ref), args.tolerance, args.pair_tolerance, args.max_cv, args.absolute,
                                      False, args.instr_tolerance))
            return found

        def rerun(names, n, exe=exe, samples=samples, meta=meta, positions=positions, counters=counters):
            extend_rounds(lambda ns, r, first, *rest: run_rounds(exe, ns, r, args.epochs, args.max_load, args.settle, args.seed,
                                                                  first, *rest), names, n, samples, meta, positions, counters)

        # One noisy measurement never blocks a push: whatever regressed is re-measured, and only what persists counts.
        confirm_regressions(both, rerun, args.confirm_rounds)
        save_record(repo, target, record)  # the record before push (with any confirmation rounds)
        print("\n### Against the committed baseline\n")
        rows, pair_rows, regressed_base = report(record, args)
        regressed_ref = []
        if ref:
            print(f"\n### Against the local record of {ref_sha[:9]} (like for like on this machine)\n")
            ref_rows, ref_pairs, env = evaluate(record, baseline_data(ref), args.tolerance, args.pair_tolerance, args.max_cv, args.absolute,
                                                False, args.instr_tolerance)
            print(render(ref_rows, ref_pairs, env, f"record {ref_sha[:9]}", args.tolerance, args.pair_tolerance))
            regressed_ref = ([r[0] for r in ref_rows if r[5] == "REGRESSED"] + [p[0] for p in ref_pairs if p[4] == "REGRESSED"]
                             + [row["name"] for row in env["counter_rows"] if any(v.endswith("REGRESSED") for v in row["verdicts"])])
        else:
            print("\nsub0perf: no local record of an earlier commit; it is made by the first clean build after a checkout")
        baseline_rel = os.path.relpath(t["baseline"], repo).replace("\\", "/")
        accepted = accepted_since(repo, baseline_rel, ref_sha)
        unaccepted = [n for n in regressed_ref if n not in accepted and "*" not in accepted]
        if regressed_base or unaccepted:
            blocked.append(target)
            names_text = ", ".join(sorted(set(unaccepted) | {r[0] for r in rows if r[5] == "REGRESSED"}
                                         | {p[0] for p in pair_rows if p[4] == "REGRESSED"}))
            print(f"\nsub0perf: push blocked by regressions in {target}: {names_text}")
            print("  Fix them, or accept them explicitly and commit the baseline:")
            print(f'    python "{Path(__file__).resolve()}" accept --results "{d / "records" / target / (head + ".json")}"'
                  f' --baseline "{t["baseline"]}" --all-changed --reason "why this is acceptable"')
        log_event(repo, {"event": "gate", "target": target, "commit": head, "reference": ref_sha,
                         "blocked": target in blocked, "regressed_vs_baseline": regressed_base, "unaccepted_vs_record": unaccepted})
    return 1 if blocked else 0


HOOK_MARK = "# sub0perf hook"


def install_hooks_command(args):
    repo = Path(args.repo)
    d = perf_dir(repo)
    if d is None:
        print(f"sub0perf: {repo} is not a git repository; hooks not installed")
        return 0
    d.mkdir(parents=True, exist_ok=True)
    config_path = d / "config.json"
    config = json.loads(config_path.read_text(encoding="utf-8")) if config_path.is_file() else {"targets": {}}
    config["targets"][args.target] = {"build_dir": str(Path(args.build_dir).resolve()), "exe": str(Path(args.exe).resolve()),
                                      "baseline": str(Path(args.baseline).resolve()), "config": args.config or ""}
    config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8", newline="\n")
    hooks = Path(git(repo, "rev-parse", "--git-path", "hooks") or "")
    if not hooks.is_absolute():
        hooks = repo / hooks
    hooks.mkdir(parents=True, exist_ok=True)
    python = Path(sys.executable).as_posix()
    script = Path(__file__).resolve().as_posix()
    bodies = {
        "pre-push": f'#!/bin/sh\n{HOOK_MARK}: performance regression gate (tools/perf/bench_check.py gate)\n'
                    f'"{python}" "{script}" gate --repo "$(git rev-parse --show-toplevel)"\n',
        "post-checkout": f'#!/bin/sh\n{HOOK_MARK}: notes the checkout; the first clean build records its measurement\n'
                         f'[ "$3" = "1" ] && "{python}" "{script}" note-checkout --repo "$(git rev-parse --show-toplevel)"\nexit 0\n',
    }
    for name, body in bodies.items():
        path = hooks / name
        if path.is_file() and HOOK_MARK not in path.read_text(encoding="utf-8", errors="replace"):
            print(f"sub0perf: {path} exists and is not ours; add this line to it yourself:\n  {body.splitlines()[2]}")
            continue
        path.write_text(body, encoding="utf-8", newline="\n")
        try:
            path.chmod(0o755)
        except OSError:
            pass
    return 0


def note_checkout_command(args):
    repo = Path(args.repo or ".")
    head = git_head(repo)
    log_event(repo, {"event": "checkout", "commit": head})
    d = perf_dir(repo)
    have = d and any((d / "records").glob(f"*/{head}.json")) if d else False
    if not have:
        print(f"sub0perf: checked out {head[:9]}; its benchmark record is made by the first clean build")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["run", "check", "baseline", "auto", "accept", "gate", "install-hooks", "note-checkout"])
    parser.add_argument("--exe", help="benchmark executable (run, auto)")
    parser.add_argument("--out", help="samples file to write (run; optional for auto)")
    parser.add_argument("--results", help="samples file from `run` (check, baseline)")
    parser.add_argument("--baseline", help="perf/baselines/<host>.json")
    parser.add_argument("--filter", default="", help="only benchmarks whose name contains this text")
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--max-rounds", type=int, default=0, help="auto: total rounds allowed for noisy benchmarks (default 2x)")
    parser.add_argument("--epochs", type=int, default=0, help="override nanobench epochs (quicker, noisier)")
    parser.add_argument("--confirm-rounds", type=int, default=4,
                        help="auto, gate: extra rounds for just the benchmarks that regressed; only regressions that persist count (0 = off)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-load", type=float, default=0.20, help="busy fraction of all CPUs above which a round waits")
    parser.add_argument("--settle", type=float, default=30.0, help="seconds to wait for load to fall")
    parser.add_argument("--tolerance", type=float, default=0.10)
    parser.add_argument("--pair-tolerance", type=float, default=0.05)
    parser.add_argument("--instr-tolerance", type=float, default=0.02, help="instructions per op change that counts")
    parser.add_argument("--max-cv", type=float, default=0.05, help="spread above which a benchmark is inconclusive")
    parser.add_argument("--absolute", action="store_true", help="no calibration or drift correction")
    parser.add_argument("--report-only", action="store_true", help="never fail; report only")
    parser.add_argument("--reason", help="why a baseline is re-recorded or a change accepted (logged in the file)")
    parser.add_argument("--benchmark", nargs="+", default=[], help="accept: benchmarks to move to the new measurement")
    parser.add_argument("--all-changed", action="store_true", help="accept: every regressed or improved benchmark")
    parser.add_argument("--record", action="store_true", help="auto: keep a clean tree's measurement as its commit's record")
    parser.add_argument("--repo", help="git repository (auto --record, gate, install-hooks)")
    parser.add_argument("--target", help="benchmark target name (auto --record, install-hooks)")
    parser.add_argument("--build-dir", help="install-hooks: build directory the gate builds in")
    parser.add_argument("--config", help="install-hooks: build configuration for multi-config generators")
    parser.add_argument("--cpu", type=int, default=-1,
                        help="logical CPU to pin measurements to (default: the last one; -2 = do not pin)")
    args = parser.parse_args()
    if not args.max_rounds:
        args.max_rounds = args.rounds * 2
    needs = {"run": ["exe", "out"], "check": ["results", "baseline"], "baseline": ["results", "baseline"], "auto": ["exe", "baseline"],
             "accept": ["results", "baseline", "reason"], "gate": [], "install-hooks": ["repo", "build_dir", "target", "exe", "baseline"],
             "note-checkout": []}
    if args.command == "accept" and not args.reason:
        args.reason = os.environ.get("SUB0MATH_PERF_REASON")
    missing = [f"--{k.replace('_', '-')}" for k in needs[args.command] if not getattr(args, k)]
    if missing:
        parser.error(f"{args.command} needs {' '.join(missing)}")
    if args.exe:
        args.exe = str(Path(args.exe).resolve())
    args.reason = args.reason or os.environ.get("SUB0MATH_PERF_REASON")  # lets build targets pass a reason
    global PIN_CPU
    PIN_CPU = None if args.cpu == -2 else ((os.cpu_count() or 1) - 1 if args.cpu == -1 else args.cpu)
    if args.command == "gate":
        args.rounds = args.rounds if args.rounds != 5 else 3
        # Same method as baselines and records (default epochs), so absolute times compare like for like.
    commands = {"run": run_command, "check": check_command, "baseline": baseline_command, "auto": auto_command,
                "accept": accept_command, "gate": gate_command, "install-hooks": install_hooks_command,
                "note-checkout": note_checkout_command}
    return commands[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
