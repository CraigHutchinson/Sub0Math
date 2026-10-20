"""Verdicts: benchmarks against a baseline, pairs against their references, hardware counters, environment controls."""

import statistics
from pathlib import Path

from .baseline import baseline_tuple
from .common import REFERENCE_MARK, read_json
from .stats import pair_ratios, summarise


def threshold(noise, tolerance, spread=0.0):
    """How far a change must go to count: the tolerance, or three times the noise or the suite's spread."""
    return max(tolerance, 3.0 * noise, 3.0 * spread)


def judge(change, noise, tolerance, cv, max_cv, spread=0.0):
    """Verdict for a change. `spread` is the suite's own spread of changes, used only where there are no controls."""
    if cv > max_cv:
        return "NOISY"
    limit = threshold(noise, tolerance, spread)
    if change > limit:
        return "REGRESSED"
    if change < -limit:
        return "IMPROVED"
    return "ok"


def judge_modes(verdict, modes, reference, limit):
    """Demotes a REGRESSED or IMPROVED verdict to NOISY when the samples sit at two levels and only one level moved.

    `modes` is stats.modes() of the samples (None when they are not bimodal); `reference` is what they are compared
    with (the baseline value at the current clock, or the baseline ratio) and `limit` the change that counts.
    A regression needs the fast level to be slower than the reference by more than the limit, an improvement the
    slow level to be faster: a shift of one level between processes is a bimodal benchmark, not a change. Both
    levels moving is a real change and keeps its verdict.
    """
    if modes is None or reference <= 0:
        return verdict
    low, high, _ = modes
    if verdict == "REGRESSED" and low / reference - 1.0 <= limit:
        return "NOISY"
    if verdict == "IMPROVED" and high / reference - 1.0 >= -limit:
        return "NOISY"
    return verdict


def _inconclusive(rows):
    """Absolute verdicts that cannot stand (the environment or the method differs) become ENVIRONMENT."""
    return [(n, b, st, raw, ch, "ENVIRONMENT" if v in ("REGRESSED", "IMPROVED") else v) for (n, b, st, raw, ch, v) in rows]


def evaluate(record, baseline, tolerance, pair_tolerance, max_cv, absolute, filtered=False, instr_tolerance=0.02):
    """Judges a samples record against a baseline (a path, or a baseline object).

    Returns (rows, pair_rows, env). A row is (name, baseline entry, stats, raw change, judged change, verdict).
    """
    stats, cal, cal_cv, cal_drift = summarise(record)
    pairs = pair_ratios(stats)
    if isinstance(baseline, dict):
        have_baseline, base_data = True, baseline
    else:
        have_baseline = Path(baseline).is_file()
        base_data = read_json(baseline) if have_baseline else {}
    base, base_cal, base_pairs = baseline_tuple(base_data)

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
        verdict = judge(change, noise, tolerance, s["cv"], max_cv, spread_floor)
        verdict = judge_modes(verdict, s["modes"], b["median_ns"] * factor, threshold(noise, tolerance, spread_floor))
        rows.append((name, b, s, raw, change, verdict))
    # When the controls (which do not use the library) moved by more than the tolerance, the environment
    # changed too much for absolute times to support a verdict either way: report them as ENVIRONMENT
    # (inconclusive). Pair ratios and instruction counts are relative to the same environment and still judge.
    if environment_shift is not None and abs(environment_shift) > tolerance:
        rows = _inconclusive(rows)
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
            verdict = judge_modes(verdict, p["modes"], bp["ratio"], threshold(p["cv"], pair_tolerance))
        else:
            change, verdict = None, "new"
        pair_rows.append((name, p, bp, change, verdict))

    method_notes = []
    base_epochs = base_data.get("epochs", 0)
    if record.get("epochs", 0) != base_epochs:
        method_notes.append(f"epochs differ (now {record.get('epochs', 0) or 'default'}, baseline {base_epochs or 'default'})")
    if "cpu" in record and "cpu" in base_data and record["cpu"] != base_data["cpu"]:  # None = unpinned; absent = unknown
        def shown(cpu):
            return "unpinned" if cpu is None else cpu
        method_notes.append(f"pinned CPU differs (now {shown(record['cpu'])}, baseline {shown(base_data['cpu'])};"
                            f" cores of a hybrid CPU differ in speed)")
    env = {"method": None if not method_notes else
           "; ".join(method_notes) + ": absolute times are not like for like; pair ratios still are",
           "rounds": len(record["rounds"]), "busy_rounds": sum(1 for r in record["rounds"] if not r["quiet"]),
           "max_load": max((r["load"] for r in record["rounds"]), default=0.0), "calibration_ns": cal,
           "calibration_cv": cal_cv, "clock_drift": cal_drift, "mode": mode, "baseline": have_baseline,
           "counter_rows": counter_rows, "instr_tolerance": instr_tolerance, "environment_shift": environment_shift,
           "tolerance": tolerance, "suite_spread": suite_spread}
    if method_notes:  # absolute times measured differently cannot support a verdict either way
        rows = _inconclusive(rows)
    return rows, pair_rows, env


def evaluate_with(record, baseline, args, filtered=False):
    """evaluate() with the judging options of parsed command-line `args`."""
    return evaluate(record, baseline, args.tolerance, args.pair_tolerance, args.max_cv, args.absolute, filtered,
                    args.instr_tolerance)


def verdict_hits(rows, pair_rows, env, verdicts):
    """Every (name, verdict) among the benchmark rows, pair rows and counter verdicts whose verdict is in `verdicts`.

    A counter verdict reads "instructions REGRESSED"; a counter row with two verdicts counts twice.
    """
    verdicts = set(verdicts)
    hits = [(r[0], r[5]) for r in rows if r[5] in verdicts]
    hits += [(p[0], p[4]) for p in pair_rows if p[4] in verdicts]
    hits += [(row["name"], v.split()[-1]) for row in env["counter_rows"] for v in row["verdicts"] if v.split()[-1] in verdicts]
    return hits


def names_with(rows, pair_rows, env, *verdicts):
    """The distinct names (in order of appearance) that have one of the verdicts."""
    return list(dict.fromkeys(name for name, _ in verdict_hits(rows, pair_rows, env, verdicts)))
