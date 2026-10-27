"""Tests for plan: fitting a quick gate to a time budget, rotation and coverage, and what overruns the budget."""

import tempfile
import unittest
from pathlib import Path

from .. import confirm, judge, plan
from ..baseline import baseline_data
from .helpers import record

# Four pair-group members (a library benchmark and its hand-written reference, twice), the rest unpaired.
PAIRS = ["dot, Length", "dot, hand-written", "seg, Length", "seg, hand-written"]
LOOSE = [f"Loose {i:02d}" for i in range(20)]
ALL = PAIRS + LOOSE


def state_with(cost=1.0, overhead=2.0, measured=None, pushes=0):
    return {"pushes": pushes, "costs": {n: cost for n in ALL}, "round_overhead": overhead, "benchmarks": measured or {}}


class MandatoryTest(unittest.TestCase):
    def test_pair_groups_and_controls_only(self):
        self.assertEqual(plan.mandatory_names(ALL), sorted(PAIRS))

    def test_a_group_without_reference_is_not_mandatory(self):
        self.assertEqual(plan.mandatory_names(["a, x", "a, y", "b, hand-written", "b, z"]), ["b, hand-written", "b, z"])


class PlanFitTest(unittest.TestCase):
    def test_fits_the_budget_and_keeps_the_reserve(self):
        p = plan.plan_quick(ALL, state_with(), "c1", 60.0)
        # 3 rounds x (2 s + 4 pair members x 1 s) = 18 s; the usable 45 s leaves 27 s = 9 loose benchmarks at 3 s each
        self.assertEqual(len(p.rotating), 9)
        self.assertLessEqual(p.estimate, 60.0 * (1 - plan.RESERVE))
        self.assertEqual(p.rounds, 3)
        self.assertEqual(len(p.mandatory) + len(p.rotating) + len(p.skipped), len(ALL))

    def test_pairs_and_controls_always_included_however_small_the_budget(self):
        for budget in (1000.0, 30.0, 5.0, 1.0):
            p = plan.plan_quick(ALL, state_with(), "c1", budget)
            self.assertTrue(set(PAIRS) <= set(p.selected), budget)
            self.assertEqual(p.rounds, plan.QUICK_ROUNDS, budget)

    def test_overrun_is_reported_when_even_the_minimum_does_not_fit(self):
        p = plan.plan_quick(ALL, state_with(), "c1", 10.0)  # the pairs need 18 s
        self.assertEqual(p.rotating, [])
        self.assertAlmostEqual(p.overrun, 8.0)
        self.assertTrue(any("over the 10 s budget by 8 s" in note for note in p.notes))

    def test_a_big_budget_measures_everything(self):
        p = plan.plan_quick(ALL, state_with(), "c1", 10000.0)
        self.assertEqual(sorted(p.selected), sorted(ALL))
        self.assertEqual(p.skipped, [])

    def test_expensive_benchmarks_are_skipped_but_cheaper_ones_still_fit(self):
        state = state_with()
        state["costs"]["Loose 00"] = 500.0
        p = plan.plan_quick(ALL, state, "c1", 60.0)
        self.assertNotIn("Loose 00", p.selected)
        self.assertGreater(len(p.rotating), 5)

    def test_unknown_costs_use_the_probe_default(self):
        state = {"pushes": 0, "costs": {}, "benchmarks": {}, "round_overhead": 2.0}
        p = plan.plan_quick(ALL, state, "c1", 60.0, default_cost=2.0)  # 3 x (2 + 8) = 30 s; 15 s left = 2 at 6 s
        self.assertEqual(len(p.rotating), 2)

    def test_describe_reports_counts_and_coverage(self):
        state = state_with()
        p = plan.plan_quick(ALL, state, "c1", 60.0)
        text = "\n".join(plan.describe_plan(p, len(ALL), state, "c1"))
        self.assertIn(f"{len(p.selected)}/{len(ALL)}", text)
        self.assertIn("all covered within 3 push(es)", text)  # 20 loose, 9 per push
        self.assertIn("stalest", text)


class RotationTest(unittest.TestCase):
    def run_pushes(self, budget, pushes):
        state = state_with()
        seen = []
        for k in range(pushes):
            commit = f"commit{k}"
            p = plan.plan_quick(ALL, state, commit, budget)
            plan.mark_measured(state, p.selected, commit, date="2026-10-27T10:00:00")
            seen.append(p)
        return state, seen

    def test_every_benchmark_is_covered_within_the_announced_number_of_pushes(self):
        bound = plan.plan_quick(ALL, state_with(), "commit0", 60.0).pushes_to_cover
        _, seen = self.run_pushes(60.0, bound)
        self.assertEqual(set().union(*(p.selected for p in seen)), set(ALL))

    def test_staleness_priority_goes_to_what_was_skipped_last_time(self):
        state, seen = self.run_pushes(60.0, 1)
        skipped = set(seen[0].skipped)
        second = plan.plan_quick(ALL, state, "commit1", 60.0)
        self.assertTrue(set(second.rotating) <= skipped)

    def test_deterministic_for_a_commit_and_different_between_commits(self):
        a = plan.plan_quick(ALL, state_with(), "same", 60.0)
        b = plan.plan_quick(ALL, state_with(), "same", 60.0)
        c = plan.plan_quick(ALL, state_with(), "other", 60.0)
        self.assertEqual(a.selected, b.selected)
        self.assertNotEqual(a.rotating, c.rotating)

    def test_mark_measured_counts_pushes_and_records_commit_and_date(self):
        state = plan.default_state()
        plan.mark_measured(state, ["x"], "abc", date="2026-10-27T09:00:00")
        plan.mark_measured(state, ["y"], "def", date="2026-10-28T09:00:00")
        self.assertEqual(state["pushes"], 2)
        self.assertEqual(state["benchmarks"]["x"], {"push": 1, "commit": "abc", "date": "2026-10-27T09:00:00"})
        self.assertEqual(state["benchmarks"]["y"]["push"], 2)

    def test_state_round_trips(self):
        with tempfile.TemporaryDirectory() as tmp:
            perf = Path(tmp)
            self.assertEqual(plan.load_state(perf, "T")["pushes"], 0)
            state = plan.default_state()
            plan.mark_measured(state, ["x"], "abc")
            plan.save_state(perf, "T", state)
            self.assertEqual(plan.load_state(perf, "T")["benchmarks"]["x"]["commit"], "abc")

    def test_absorb_timings_keeps_median_costs(self):
        state = plan.default_state()
        plan.absorb_timings(state, {"bench": {"a": [1.0, 3.0, 2.0]}, "overhead": [4.0, 6.0]})
        self.assertEqual(state["costs"]["a"], 2.0)
        self.assertEqual(state["round_overhead"], 5.0)


class BudgetTest(unittest.TestCase):
    def test_remaining_and_overrun_follow_the_clock(self):
        now = [100.0]
        b = plan.Budget(60.0, clock=lambda: now[0])
        self.assertTrue(b.affords(60.0))
        now[0] = 150.0
        self.assertAlmostEqual(b.remaining(), 10.0)
        self.assertFalse(b.affords(11.0))
        self.assertEqual(b.overrun(), 0.0)
        now[0] = 175.0
        self.assertAlmostEqual(b.overrun(), 15.0)

    def test_no_budget_affords_everything(self):
        b = plan.Budget(None, clock=lambda: 0.0)
        self.assertTrue(b.affords(1e9))
        self.assertEqual(b.overrun(), 0.0)


class OverrunTest(unittest.TestCase):
    """A quick gate never skips confirming a regression because of the budget, and skips an inconclusive retry that does not fit."""

    def setUp(self):
        self.baseline = baseline_data(record({"Bench 0": 1000.0, "Bench 1": 1000.0}))
        self.rec = record({"Bench 0": 1500.0, "Bench 1": 1000.0})  # Bench 0 looks regressed

    def evaluate_all(self):
        return [judge.evaluate(self.rec, self.baseline, 0.10, 0.05, 0.05, False)]

    def test_regression_is_confirmed_even_with_no_budget_left(self):
        calls = []
        now = [0.0]
        budget = plan.Budget(10.0, clock=lambda: now[0])
        now[0] = 50.0  # already 40 s over

        def rerun(names, rounds):
            calls.append((sorted(names), rounds))
            now[0] += 30.0

        first, _ = confirm.confirm_regressions(self.evaluate_all, rerun, 4, None)
        self.assertEqual(first, {"Bench 0"})
        self.assertEqual(len(calls), 1)  # re-measured despite the exhausted budget
        self.assertGreater(budget.overrun(), 40.0)  # and the overrun is there to report

    def test_inconclusive_retry_is_skipped_when_it_does_not_fit(self):
        noisy = record({"Bench 0": 1000.0, "Bench 1": 1000.0}, jitter=0.30)
        evaluate_all = lambda: [judge.evaluate(noisy, self.baseline, 0.10, 0.05, 0.05, False)]
        ran = []
        first, final = confirm.retry_inconclusive(evaluate_all, lambda n, r: ran.append(r), 3, None, lambda names, r: False)
        self.assertTrue(first)
        self.assertEqual(ran, [])
        self.assertEqual(first, final)
        confirm.retry_inconclusive(evaluate_all, lambda n, r: ran.append(r), 3, None, lambda names, r: True)
        self.assertEqual(ran, [3])


class SubsetJudgingTest(unittest.TestCase):
    def test_benchmarks_not_measured_this_push_are_not_missing(self):
        base = baseline_data(record({"a": 1000.0, "b": 1000.0, "c": 1000.0}))
        rec = record({"a": 1000.0})
        args = type("A", (), {"tolerance": 0.1, "pair_tolerance": 0.05, "max_cv": 0.05, "absolute": False, "instr_tolerance": 0.02,
                              "filter": "", "subset": True})
        rows, _, _ = judge.evaluate_with(rec, base, args)
        self.assertEqual({r[0]: r[5] for r in rows}, {"a": "ok"})
        args.subset = False
        rows, _, _ = judge.evaluate_with(rec, base, args)
        self.assertIn("missing", {r[5] for r in rows})


if __name__ == "__main__":
    unittest.main()
