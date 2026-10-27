"""Planning a quick gate: which benchmarks, and how many rounds, fit a time budget.

A quick gate measures every pair group (a library benchmark and its hand-written reference, which are also the
environment controls) in every push, at a minimum round count, so a verdict on them means something. The remaining
benchmarks rotate: the ones measured longest ago go first, ties are broken by a hash of the commit and the name (so the
choice is deterministic for a commit and varies between commits), until the budget is used. What was measured when is
kept in `.git/sub0perf/coverage/<target>.json` together with the measured cost of a sample, so the next plan is
made from timings, not guesses.
"""

import hashlib
import json
import math
import statistics
import time
from dataclasses import dataclass, field

from .common import REFERENCE_MARK, atomic_write_text, group_of, read_json

QUICK_ROUNDS = 3        # minimum (and default) interleaved rounds of the always-measured set: the least that gives a median and a spread
RESERVE = 0.25          # share of the budget kept back for confirmation and retry passes
FALLBACK_COST = 1.0     # seconds per sample when nothing is known and no probe was made
FALLBACK_ROUND_OVERHEAD = 2.0  # two steady calibrations per round, seconds


def mandatory_names(names):
    """Every member of a group that has a hand-written reference: the pairs and the controls. Always measured in quick."""
    groups = {group_of(n) for n in names if REFERENCE_MARK in n}
    return sorted(n for n in names if group_of(n) in groups)


def tiebreak(commit, name):
    return hashlib.sha1(f"{commit}:{name}".encode("utf-8")).hexdigest()


def rotation_order(names, benchmarks, commit):
    """`names` by staleness: never measured first, then the least recently measured; deterministic for a commit."""
    return sorted(names, key=lambda n: (benchmarks.get(n, {}).get("push", -1), tiebreak(commit, n)))


@dataclass
class Plan:
    rounds: int
    selected: list
    mandatory: list
    rotating: list              # the rotating benchmarks chosen this time
    skipped: list               # the rotating benchmarks not chosen
    estimate: float             # predicted seconds of the planned pass
    budget: float
    reserve: float              # seconds kept for confirmation and retry passes
    overrun: float = 0.0        # seconds by which the mandatory set alone exceeds the budget
    pushes_to_cover: int = 0    # pushes until every rotating benchmark has had a turn; 0 = none rotate, -1 = never at this budget
    notes: list = field(default_factory=list)


def round_cost(names, costs, overhead, default_cost):
    return overhead + sum(costs.get(n, default_cost) for n in names)


def default_cost_of(costs):
    return statistics.median(costs.values()) if costs else FALLBACK_COST


def plan_quick(names, state, commit, budget, rounds=QUICK_ROUNDS, reserve=RESERVE, default_cost=None):
    """A Plan for measuring `names` within `budget` seconds. `state` is the coverage state (see load_state)."""
    costs = state.get("costs", {})
    overhead = state.get("round_overhead", FALLBACK_ROUND_OVERHEAD)
    if default_cost is None:
        default_cost = default_cost_of(costs)
    must = mandatory_names(names)
    keep = budget * reserve
    spend = budget - keep
    base = rounds * round_cost(must, costs, overhead, default_cost)
    estimate = base
    chosen, skipped = [], []
    notes = []
    rest = rotation_order([n for n in names if n not in set(must)], state.get("benchmarks", {}), commit)
    for n in rest:
        extra = rounds * costs.get(n, default_cost)
        if estimate + extra <= spend:
            chosen.append(n)
            estimate += extra
        else:
            skipped.append(n)
    overrun = max(0.0, base - budget)
    if overrun:
        notes.append(f"the always-measured pairs and controls alone are estimated at {base:.0f} s, over the {budget:.0f} s "
                     f"budget by {overrun:.0f} s; they are measured anyway")
    if not rest:
        cover = 0
    elif chosen:
        cover = math.ceil(len(rest) / len(chosen))
    else:
        cover = -1
    return Plan(rounds, sorted(must + chosen), must, chosen, skipped, estimate, budget, keep, overrun, cover, notes)


def describe_plan(plan, total, state, commit):
    """The lines printed at the start of a quick gate: the plan and the coverage it gives."""
    benchmarks = state.get("benchmarks", {})
    pushes = state.get("pushes", 0)
    rest = len(plan.rotating) + len(plan.skipped)
    lines = [f"sub0perf: plan: {len(plan.selected)}/{total} benchmarks x {plan.rounds} rounds, estimated {plan.estimate:.0f} s of a "
             f"{plan.budget:.0f} s budget ({plan.reserve:.0f} s kept for confirmation and retries)",
             f"sub0perf:   always measured: {len(plan.mandatory)} (every pair with its hand-written reference, and the controls); "
             f"rotating this push: {len(plan.rotating)} of {rest}"]
    lines += [f"sub0perf:   {note}" for note in plan.notes]
    if rest:
        if plan.pushes_to_cover > 0:
            lines.append(f"sub0perf:   coverage: measured {len(plan.selected)}/{total} now; all covered within {plan.pushes_to_cover} push(es)")
        else:
            lines.append("sub0perf:   coverage: the rotating benchmarks never fit this budget; raise --budget, or run the full level")
        after = {n: dict(v) for n, v in benchmarks.items()}
        for n in plan.rotating:
            after[n] = {"push": pushes + 1}
        stalest = rotation_order(plan.rotating + plan.skipped, after, commit)[0]
        last = after.get(stalest, {}).get("push", -1)
        when = "never measured" if last < 0 else f"last measured {pushes + 1 - last} push(es) ago"
        lines.append(f"sub0perf:   stalest after this push: {stalest} ({when})")
    return lines


def default_state():
    return {"pushes": 0, "benchmarks": {}, "costs": {}, "round_overhead": FALLBACK_ROUND_OVERHEAD}


def state_path(perf, target):
    return perf / "coverage" / f"{target}.json"


def load_state(perf, target):
    path = state_path(perf, target) if perf else None
    if path is not None and path.is_file():
        try:
            return {**default_state(), **read_json(path)}
        except (OSError, ValueError):
            pass
    return default_state()


def save_state(perf, target, state):
    if perf:
        atomic_write_text(state_path(perf, target), json.dumps(state, indent=1) + "\n")


def mark_measured(state, names, commit, date=None):
    """Counts a push and notes `names` as measured by it. Returns the push number."""
    state["pushes"] = state.get("pushes", 0) + 1
    stamp = date or time.strftime("%Y-%m-%dT%H:%M:%S")
    for n in names:
        state.setdefault("benchmarks", {})[n] = {"push": state["pushes"], "commit": commit, "date": stamp}
    return state["pushes"]


def absorb_timings(state, timings):
    """Keeps the measured cost of a sample (median wall seconds of its process) and of a round's calibration overhead."""
    for name, seconds in (timings.get("bench") or {}).items():
        if seconds:
            state.setdefault("costs", {})[name] = round(statistics.median(seconds), 3)
    if timings.get("overhead"):
        state["round_overhead"] = round(statistics.median(timings["overhead"]), 3)


def new_timings():
    return {"bench": {}, "overhead": []}


class Budget:
    """A wall-clock allowance. `seconds` None means unlimited. The clock is replaceable for tests."""

    def __init__(self, seconds, clock=time.monotonic):
        self.seconds, self.clock, self.start = seconds, clock, clock()

    def elapsed(self):
        return self.clock() - self.start

    def remaining(self):
        return math.inf if self.seconds is None else self.seconds - self.elapsed()

    def affords(self, seconds):
        return seconds <= self.remaining()

    def overrun(self):
        return 0.0 if self.seconds is None else max(0.0, self.elapsed() - self.seconds)


def pass_estimate(names, rounds, state, default_cost=None):
    """Predicted seconds of `rounds` more rounds over just `names`."""
    costs = state.get("costs", {})
    if default_cost is None:
        default_cost = default_cost_of(costs)
    return rounds * round_cost(names, costs, state.get("round_overhead", FALLBACK_ROUND_OVERHEAD), default_cost)
