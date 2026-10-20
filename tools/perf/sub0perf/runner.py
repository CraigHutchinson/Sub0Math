"""Measuring: one process per benchmark, interleaved rounds, calibration at the edges of every round."""

import json
import random
import statistics
import subprocess
import tempfile
from pathlib import Path

from . import platform_proc
from .common import CALIBRATION, group_of
from .load import wait_for_quiet

COUNTERS = {"instructions": "median(instructions)", "cycles": "median(cpucycles)",
            "branches": "median(branchinstructions)", "branch_misses": "median(branchmisses)"}


def measure_full(exe, args, scratch):
    """Runs the benchmark executable once; returns {name: (ns per op, {counter: per op})}.

    Counters are included only when the platform provides them (nanobench reports 0 otherwise).
    """
    out = Path(scratch) / "one.json"
    platform_proc.launch([exe, *args, "--json", str(out)])
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


def fill_unmeasured(samples, positions, lengths_before):
    """Gives every benchmark that got no new sample its median again, marked filled, so all keep one sample per round."""
    for n, v in samples.items():
        if len(v) == lengths_before[n]:
            v.append(statistics.median(v))
            positions.setdefault(n, []).append(None)  # filled, not measured


def extend_rounds(run, names, rounds, samples, meta, positions, counters):
    """Adds `rounds` rounds measuring only `names`; the other benchmarks repeat their median so rounds stay aligned.

    `run` has run_rounds' signature minus the process arguments, so a test can pass a stub that measures nothing.
    """
    for _ in range(rounds):
        before = {n: len(v) for n, v in samples.items()}
        run(sorted(names), 1, len(meta), samples, meta, positions, counters)
        fill_unmeasured(samples, positions, before)


def measure_all(exe, args):
    """A full run of every benchmark with the measuring options of parsed command-line `args`: (samples, meta, positions, counters)."""
    return run_rounds(exe, list_names(exe, args.filter), args.rounds, args.epochs, args.max_load, args.settle, args.seed)


def rerunner(exe, args, samples, meta, positions, counters):
    """`rerun(names, rounds)` that adds rounds for just `names` to these samples (see extend_rounds)."""
    def rerun(names, rounds):
        extend_rounds(lambda ns, r, first, *rest: run_rounds(exe, ns, r, args.epochs, args.max_load, args.settle, args.seed, first,
                                                              *rest), names, rounds, samples, meta, positions, counters)
    return rerun
