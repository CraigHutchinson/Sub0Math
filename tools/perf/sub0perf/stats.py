"""Statistics over samples records: robust spread, calibration-scaled medians, pair ratios."""

import statistics

from .common import REFERENCE_MARK, group_of


def robust_cv(values):
    if len(values) < 2:
        return 0.0
    m = statistics.median(values)
    mad = statistics.median(abs(v - m) for v in values)
    return 1.4826 * mad / m if m else 0.0


def measured_count(record, name):
    """How many of a benchmark's samples were measured (not filled in by copying a median to keep rounds aligned)."""
    values = record["samples"].get(name, [])
    where = record.get("positions", {}).get(name, [])
    return sum(1 for i in range(len(values)) if not (i < len(where) and where[i] is None))


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
        if not values:
            continue  # nothing measured (accept reports it)
        where = positions.get(name, [])
        scaled, measured = [], []
        for i, v in enumerate(values[:len(rounds)]):
            filled = i < len(where) and where[i] is None
            measured.append(not filled)
            t = 0.5 if filled or i >= len(where) else where[i]
            clock = rounds[i]["calibration_start_ns"] + t * (rounds[i]["calibration_end_ns"] - rounds[i]["calibration_start_ns"])
            scaled.append(v * cal_median / clock)
        found = record.get("counters", {}).get(name, {})
        out[name] = {"median_ns": statistics.median(scaled), "cv": robust_cv(scaled), "n": len(scaled), "rounds": scaled,
                     "measured": measured,
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
            # Only rounds where both were measured: a filled median beside a fresh measurement is not adjacent.
            per_round = [a / b for a, b, ma, mb in zip(stats[m]["rounds"], stats[ref]["rounds"],
                                                     stats[m].get("measured", []), stats[ref].get("measured", []))
                         if b > 0 and ma and mb]
            if per_round:
                pairs[m] = {"reference": ref, "ratio": statistics.median(per_round), "cv": robust_cv(per_round)}
                mi = stats[m]["counters"].get("instructions")
                ri = stats[ref]["counters"].get("instructions")
                if mi and ri and ri["median"] > 0:
                    pairs[m]["instruction_ratio"] = mi["median"] / ri["median"]
    return pairs
