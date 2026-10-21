#!/usr/bin/env python3
"""Runs benchmarks interleaved and judges them against a per-host baseline: regressions, progressions,
noise, and paired comparisons with hand-written references.

  bench_check.py run      --exe BENCH --out samples.json [--rounds 5] [--epochs N] [--filter TEXT]
  bench_check.py check    --results samples.json --baseline perf/baselines/<host>.json
  bench_check.py baseline --results samples.json --baseline perf/baselines/<host>.json
  bench_check.py auto     --exe BENCH --baseline ... [--rounds 3] [--epochs 5] [--report-only] [--record]
  bench_check.py accept   --results samples.json --baseline ... --reason TEXT (--benchmark NAME ... | --all-changed)
  bench_check.py gate     [--repo DIR] [--confirm-rounds 4]                      (the pre-push hook; reads .git/sub0perf/config.json)
  bench_check.py install-hooks --repo DIR --build-dir DIR --target NAME --exe PATH --baseline PATH  [--pin file=F,var=V,repo=owner/name ...]

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
import contextlib
import os
from pathlib import Path

from . import platform_proc
from .accept import accept_command, baseline_command
from .baseline import make_record, write_samples
from .common import read_json
from .confirm import confirm_regressions, noisy_names
from .gate import gate_command
from .gitstate import save_record
from .hooks import install_hooks_command, note_checkout_command
from .judge import evaluate_with
from .lock import LockTimeout, PerfLock
from .report import report
from .runner import fill_unmeasured, measure_all, rerunner, run_rounds
from .stats import rounds_needed


def run_command(args):
    samples, meta, positions, counters = measure_all(args.exe, args)
    write_samples(args.out, make_record(args.exe, samples, meta, positions, counters, args.epochs, args.seed))
    return 0


def check_command(args):
    record = read_json(args.results)
    _, _, regressed = report(record, args)
    return 0 if args.report_only or not regressed else 1


def auto_command(args):
    if os.environ.get("SUB0MATH_PERF_SKIP"):
        return 0  # the gate builds benchmarks itself and measures them once
    try:
        with PerfLock(Path(args.repo), "auto", args.lock_timeout) if args.repo else contextlib.nullcontext():
            return measure_and_judge(args)
    except LockTimeout as e:
        print(f"sub0perf: {e}")
        return 0 if args.report_only else 1


def measure_and_judge(args):
    samples, meta, positions, counters = measure_all(args.exe, args)
    record = make_record(args.exe, samples, meta, positions, counters, args.epochs, args.seed)
    # Benchmarks too noisy to judge get more rounds, up to --max-rounds in total.
    while len(meta) < args.max_rounds:
        rows, pair_rows, _ = evaluate_with(record, args.baseline, args, bool(args.filter))
        noisy = noisy_names(rows, pair_rows)
        if not noisy:
            break
        print(f"{len(noisy)} benchmark(s) too noisy; adding a round for them", flush=True)
        # Extra rounds re-measure only the noisy set; the others repeat their median so rounds stay aligned.
        before = {n: len(v) for n, v in samples.items()}
        run_rounds(args.exe, sorted(noisy), 1, args.epochs, args.max_load, args.settle, args.seed, len(meta), samples, meta,
                   positions, counters)
        fill_unmeasured(samples, positions, before)
    rerun = rerunner(args.exe, args, samples, meta, positions, counters)
    confirm_regressions(lambda: [evaluate_with(record, args.baseline, args, bool(args.filter))], rerun, args.confirm_rounds,
                        lambda names: rounds_needed(record, names))
    if args.out:
        write_samples(args.out, record)
    if args.record and args.target:
        save_record(Path(args.repo or "."), args.target, record, only_if_missing=True, exe=args.exe)
    _, _, regressed = report(record, args)
    return 0 if args.report_only or not regressed else 1


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
    parser.add_argument("--seed", type=int, default=1, help="shuffle seed of the rounds (recorded in the records)")
    parser.add_argument("--lock-timeout", type=float, default=900.0,
                        help="auto, gate: seconds to wait for another measuring run on this repository before giving up")
    parser.add_argument("--refs-from-stdin", action="store_true",
                        help="gate: read the pushed refs from stdin (pre-push hook); refuse a dirty tree or a HEAD that is not pushed")
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
    parser.add_argument("--pin", action="append", default=[], metavar="file=F,var=V[,repo=owner/name][,checkout=DIR][,name=N]",
                        help="install-hooks: a dependency pin (CMake `set(V <sha>)` in file F) the gate checks is on the dependency's "
                             "remote before a push that changes F; repeatable")
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
    platform_proc.set_pin_cpu(platform_proc.resolve_pin_cpu(args.cpu))
    if args.command == "gate":
        args.rounds = args.rounds if args.rounds != 5 else 3
        # Same method as baselines and records (default epochs), so absolute times compare like for like.
    commands = {"run": run_command, "check": check_command, "baseline": baseline_command, "auto": auto_command,
                "accept": accept_command, "gate": gate_command, "install-hooks": install_hooks_command,
                "note-checkout": note_checkout_command}
    return commands[args.command](args)
