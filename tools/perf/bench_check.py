#!/usr/bin/env python3
"""Runs benchmarks interleaved and judges them against a per-host baseline: regressions, progressions,
noise, and paired comparisons with hand-written references.

  bench_check.py run      --exe BENCH --out samples.json [--rounds 5] [--epochs N] [--filter TEXT]
  bench_check.py check    --results samples.json --baseline perf/baselines/<host>.json
  bench_check.py baseline --results samples.json --baseline perf/baselines/<host>.json
  bench_check.py auto     --exe BENCH --baseline ... [--rounds 3] [--epochs 5] [--report-only]

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

Verdicts: REGRESSED (slower, significant), IMPROVED (faster, significant: a progression to record with
`baseline`), ok, NOISY, new, missing. Pairs: in every group containing a name with "hand-written" (the
fastest-known reference, never weakened), each other member's time is divided by the reference's in the
same round. That ratio is nearly independent of the host, so it is reported even with no baseline, and
compared with the baseline's ratio when there is one.

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


def measure(exe, args, scratch):
    """Runs the benchmark executable once; returns {name: ns per op} from its nanobench JSON."""
    out = Path(scratch) / "one.json"
    _launch([exe, *args, "--json", str(out)])
    data = json.loads(out.read_text(encoding="utf-8"))
    return {r["name"]: r["median(elapsed)"] / r["batch"] * 1e9 for r in data["results"]}


def list_names(exe, text_filter):
    args = [exe, "--list"] + (["--filter", text_filter] if text_filter else [])
    out = subprocess.run(args, check=True, capture_output=True, text=True).stdout
    return [line.strip() for line in out.splitlines() if line.strip()]


def group_of(name):
    return name.split(", ", 1)[0]


def run_rounds(exe, names, rounds, epochs, max_load, settle, seed, first_round=0, samples=None, meta=None, positions=None):
    samples = samples if samples is not None else {n: [] for n in names}
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
                samples.setdefault(n, []).append(measure(exe, ["--exact", n, *extra], scratch)[n])
                # Where in the round it ran (0 = start, 1 = end), to interpolate the clock between calibrations.
                positions.setdefault(n, []).append(round((k + 0.5) / len(sequence), 4))
            cal_end = measure(exe, ["--calibration", *extra], scratch)[CALIBRATION]
            meta.append({"round": r, "load": round(load, 4), "waited_s": round(waited, 1), "quiet": quiet,
                         "calibration_start_ns": cal_start, "calibration_end_ns": cal_end})
            print(f"round {r + 1}: load {load:.0%}{'' if quiet else ' (busy)'}, calibration {cal_start:,.0f}/{cal_end:,.0f} ns,"
                  f" {sum(len(m) for m in groups.values())} benchmarks", flush=True)
    return samples, meta, positions


def run_command(args):
    names = list_names(args.exe, args.filter)
    samples, meta, positions = run_rounds(args.exe, names, args.rounds, args.epochs, args.max_load, args.settle, args.seed)
    write_samples(args.out, args.exe, samples, meta, positions)
    return 0


def write_samples(path, exe, samples, meta, positions):
    record = {"format": 2, "exe": Path(exe).name, "host": platform.node(), "machine": platform.machine(),
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
        out[name] = {"median_ns": statistics.median(scaled), "cv": robust_cv(scaled), "n": len(scaled), "rounds": scaled}
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
    return pairs


# ── Baselines and judging ───────────────────────────────────────────────────────────────────────────

def load_baseline(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    benchmarks = {}
    for name, v in data.get("benchmarks", {}).items():
        benchmarks[name] = v if isinstance(v, dict) else {"median_ns": v, "cv": None}
    return benchmarks, data.get("calibration_ns"), data.get("pairs", {})


def baseline_command(args):
    record = json.loads(Path(args.results).read_text(encoding="utf-8"))
    stats, cal, cal_cv, _ = summarise(record)
    pairs = pair_ratios(stats)
    out = {"format": 2, "host": record["host"], "machine": record["machine"], "system": record["system"],
           "note": "Medians per op after calibration scaling, with robust CV across interleaved rounds. "
                   "Regenerate deliberately; say why in the commit.",
           "calibration_ns": round(cal, 3),
           "benchmarks": {n: {"median_ns": round(s["median_ns"], 3), "cv": round(s["cv"], 4), "rounds": s["n"]}
                          for n, s in sorted(stats.items())},
           "pairs": {n: {"reference": p["reference"], "ratio": round(p["ratio"], 4)} for n, p in sorted(pairs.items())}}
    Path(args.baseline).parent.mkdir(parents=True, exist_ok=True)
    Path(args.baseline).write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"wrote {len(stats)} benchmarks and {len(pairs)} pairs to {args.baseline}")
    return 0


def judge(change, noise, tolerance, cv, max_cv):
    if cv > max_cv:
        return "NOISY"
    threshold = max(tolerance, 3.0 * noise)
    if change > threshold:
        return "REGRESSED"
    if change < -threshold:
        return "IMPROVED"
    return "ok"


def evaluate(record, baseline_path, tolerance, pair_tolerance, max_cv, absolute, filtered=False):
    stats, cal, cal_cv, cal_drift = summarise(record)
    pairs = pair_ratios(stats)
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

    rows = []
    for name, s in sorted(stats.items()):
        if name not in base:
            rows.append((name, None, s, None, None, "new"))
            continue
        b = base[name]
        raw = s["median_ns"] / b["median_ns"] - 1.0
        change = s["median_ns"] / (b["median_ns"] * factor) - 1.0
        noise = max(s["cv"], b["cv"] or 0.0)
        rows.append((name, b, s, raw, change, judge(change, noise, tolerance, s["cv"], max_cv)))
    if not filtered:  # a filtered run selects a subset; the rest are not missing
        for name in sorted(set(base) - set(stats)):
            rows.append((name, base[name], None, None, None, "missing"))

    pair_rows = []
    for name, p in sorted(pairs.items()):
        bp = base_pairs.get(name)
        if bp:
            change = p["ratio"] / bp["ratio"] - 1.0
            verdict = judge(change, p["cv"], pair_tolerance, p["cv"], max_cv)
        else:
            change, verdict = None, "new"
        pair_rows.append((name, p, bp, change, verdict))

    env = {"rounds": len(record["rounds"]), "busy_rounds": sum(1 for r in record["rounds"] if not r["quiet"]),
           "max_load": max((r["load"] for r in record["rounds"]), default=0.0), "calibration_ns": cal,
           "calibration_cv": cal_cv, "clock_drift": cal_drift, "mode": mode, "baseline": have_baseline}
    return rows, pair_rows, env


def render(rows, pair_rows, env, baseline_path, tolerance, pair_tolerance):
    pct = lambda x: "-" if x is None else f"{x:+.1%}"
    num = lambda x: "-" if x is None else f"{x:,.1f}"
    lines = [f"**Environment:** {env['rounds']} interleaved rounds; load max {env['max_load']:.0%}"
             f" ({env['busy_rounds']} round(s) above the limit); calibration {env['calibration_ns']:,.0f} ns,"
             f" spread {env['calibration_cv']:.1%}, clock drift first to last {env['clock_drift']:+.1%}."]
    if abs(env["clock_drift"]) > 0.05:
        lines.append(f"> Clock drift of {env['clock_drift']:+.1%} during the run (thermal or power state); rounds are"
                     " scaled by calibration, but treat close calls with care.")
    if not env["baseline"]:
        lines.append(f"No baseline at `{baseline_path}`: run `bench_check.py baseline` on this host to adopt the gate."
                     " Pair ratios below are still meaningful without one.")
    else:
        lines.append(f"Judged {env['mode']}; tolerance {tolerance:.0%} or 3x the noise, whichever is larger.")

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
    section("Unchanged, new or missing", {"ok", "new", "missing"})

    if pair_rows:
        lines.append(f"\n**Pairs against hand-written references** (ratio > 1 means slower than the reference;"
                     f" pair tolerance {pair_tolerance:.0%})\n")
        lines.append("| Benchmark | Reference | Ratio now | Baseline ratio | Change | Spread | Verdict |")
        lines.append("|---|---|---:|---:|---:|---:|---|")
        for name, p, bp, change, v in pair_rows:
            base_ratio = "-" if not bp else format(bp["ratio"], ".3f") + "x"
            lines.append(f"| {name} | {p['reference']} | {p['ratio']:.3f}x | {base_ratio} | {pct(change)} | {p['cv']:.1%} | {v} |")
    return "\n".join(lines)


def report(record, args):
    rows, pair_rows, env = evaluate(record, args.baseline, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute,
                                    bool(getattr(args, "filter", "")))
    text = render(rows, pair_rows, env, args.baseline, args.tolerance, args.pair_tolerance)
    print(text)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"### {record.get('exe', 'Benchmarks')} against {Path(args.baseline).name}\n\n{text}\n\n")
    regressed = sum(r[5] == "REGRESSED" for r in rows) + sum(p[4] == "REGRESSED" for p in pair_rows)
    improved = sum(r[5] == "IMPROVED" for r in rows) + sum(p[4] == "IMPROVED" for p in pair_rows)
    noisy = sum(r[5] == "NOISY" for r in rows) + sum(p[4] == "NOISY" for p in pair_rows)
    print(f"\nperf: {regressed} regressed, {improved} improved, {noisy} inconclusive")
    return rows, pair_rows, regressed


def check_command(args):
    record = json.loads(Path(args.results).read_text(encoding="utf-8"))
    _, _, regressed = report(record, args)
    return 0 if args.report_only or not regressed else 1


def auto_command(args):
    names = list_names(args.exe, args.filter)
    samples, meta, positions = run_rounds(args.exe, names, args.rounds, args.epochs, args.max_load, args.settle, args.seed)
    record = {"rounds": meta, "samples": samples, "positions": positions, "exe": Path(args.exe).name}
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
                   positions)
        for n, v in samples.items():
            if len(v) == before[n]:
                v.append(statistics.median(v))
                positions[n].append(0.5)
    if args.out:
        write_samples(args.out, args.exe, samples, meta, positions)
    _, _, regressed = report(record, args)
    return 0 if args.report_only or not regressed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["run", "check", "baseline", "auto"])
    parser.add_argument("--exe", help="benchmark executable (run, auto)")
    parser.add_argument("--out", help="samples file to write (run; optional for auto)")
    parser.add_argument("--results", help="samples file from `run` (check, baseline)")
    parser.add_argument("--baseline", help="perf/baselines/<host>.json")
    parser.add_argument("--filter", default="", help="only benchmarks whose name contains this text")
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--max-rounds", type=int, default=0, help="auto: total rounds allowed for noisy benchmarks (default 2x)")
    parser.add_argument("--epochs", type=int, default=0, help="override nanobench epochs (quicker, noisier)")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-load", type=float, default=0.20, help="busy fraction of all CPUs above which a round waits")
    parser.add_argument("--settle", type=float, default=30.0, help="seconds to wait for load to fall")
    parser.add_argument("--tolerance", type=float, default=0.10)
    parser.add_argument("--pair-tolerance", type=float, default=0.05)
    parser.add_argument("--max-cv", type=float, default=0.05, help="spread above which a benchmark is inconclusive")
    parser.add_argument("--absolute", action="store_true", help="no calibration or drift correction")
    parser.add_argument("--report-only", action="store_true", help="never fail; report only")
    parser.add_argument("--cpu", type=int, default=-1,
                        help="logical CPU to pin measurements to (default: the last one; -2 = do not pin)")
    args = parser.parse_args()
    if not args.max_rounds:
        args.max_rounds = args.rounds * 2
    needs = {"run": ["exe", "out"], "check": ["results", "baseline"], "baseline": ["results", "baseline"], "auto": ["exe", "baseline"]}
    missing = [f"--{k}" for k in needs[args.command] if not getattr(args, k)]
    if missing:
        parser.error(f"{args.command} needs {' '.join(missing)}")
    if args.exe:
        args.exe = str(Path(args.exe).resolve())
    global PIN_CPU
    PIN_CPU = None if args.cpu == -2 else ((os.cpu_count() or 1) - 1 if args.cpu == -1 else args.cpu)
    return {"run": run_command, "check": check_command, "baseline": baseline_command, "auto": auto_command}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
