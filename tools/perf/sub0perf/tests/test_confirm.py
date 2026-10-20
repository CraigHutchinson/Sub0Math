"""Tests for confirm."""

import unittest

from ..baseline import baseline_data
from .. import confirm, judge, runner
from .helpers import record, ROUNDS, NAMES


class ConfirmationTest(unittest.TestCase):
    def setUp(self):
        self.base = {n: 1000.0 + 50 * i for i, n in enumerate(NAMES)}
        self.baseline = baseline_data(record(self.base))
        self.now = dict(self.base)
        self.now["Bench 1"] *= 1.50  # looks regressed on the first pass
        self.now["Bench 4"] *= 1.50
        self.rec = record(self.now)

    def evaluate_all(self):
        return [judge.evaluate(self.rec, self.baseline, 0.10, 0.05, 0.05, False)]

    def stub(self, truth):
        """A run function that appends samples from `truth` (name -> ns) for the requested names only."""
        calls = []

        def run(names, rounds, first_round, samples, meta, positions, counters):
            calls.append((list(names), first_round))
            for n in names:
                samples[n].append(truth[n])
                positions[n].append(0.5)
            meta.append({"round": first_round, "load": 0.0, "waited_s": 0.0, "quiet": True,
                         "calibration_start_ns": 1000.0, "calibration_end_ns": 1000.0})
        return run, calls

    def test_extend_rounds_keeps_alignment(self):
        run, calls = self.stub(self.base)
        rec = self.rec
        runner.extend_rounds(run, {"Bench 1"}, 2, rec["samples"], rec["rounds"], rec["positions"], rec["counters"])
        self.assertEqual(len(rec["rounds"]), ROUNDS + 2)
        self.assertEqual([c[1] for c in calls], [ROUNDS, ROUNDS + 1])
        self.assertEqual(calls[0][0], ["Bench 1"])
        for n, v in rec["samples"].items():
            self.assertEqual(len(v), ROUNDS + 2, n)
            self.assertEqual(len(rec["positions"][n]), ROUNDS + 2, n)

    def test_noise_is_not_confirmed(self):
        run, calls = self.stub(self.base)  # the re-measurement shows no regression
        rec = self.rec
        first, final = confirm.confirm_regressions(
            self.evaluate_all,
            lambda names, n: runner.extend_rounds(run, names, n, rec["samples"], rec["rounds"], rec["positions"], rec["counters"]), 8)
        self.assertEqual(first, {"Bench 1", "Bench 4"})
        self.assertEqual(final, set())
        self.assertEqual({n for c in calls for n in c[0]}, {"Bench 1", "Bench 4"})

    def test_real_regression_is_confirmed(self):
        run, _ = self.stub(self.now)
        rec = self.rec
        first, final = confirm.confirm_regressions(
            self.evaluate_all,
            lambda names, n: runner.extend_rounds(run, names, n, rec["samples"], rec["rounds"], rec["positions"], rec["counters"]), 4)
        self.assertEqual(final, {"Bench 1", "Bench 4"})

    def test_nothing_regressed_measures_nothing(self):
        self.rec = record(self.base)
        first, final = confirm.confirm_regressions(self.evaluate_all, lambda *a: self.fail("re-measured"), 4)
        self.assertEqual((first, final), (set(), set()))

    def test_pair_remeasures_its_reference(self):
        base = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, hand-written": 500.0, "H, vocab": 500.0}
        baseline = baseline_data(record(base))
        now = dict(base)
        now["G, vocab"] *= 1.30
        found = confirm.regressions_in([judge.evaluate(record(now), baseline, 0.10, 0.05, 0.05, False)])
        self.assertEqual(found[0], {"G, vocab"})
        self.assertEqual(found[1], {"G, vocab", "G, hand-written"})


class InconclusiveTest(unittest.TestCase):
    def setUp(self):
        self.base = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, hand-written": 500.0, "H, vocab": 500.0}
        self.baseline = baseline_data(record(self.base))

    def noisy_record(self):
        rec = record(self.base)
        rec["samples"]["H, vocab"] = [500.0, 800.0, 300.0, 650.0, 420.0]  # far above max_cv
        return rec

    def evaluations(self, rec):
        return [judge.evaluate(rec, self.baseline, 0.10, 0.05, 0.05, False)]

    def test_noisy_benchmark_and_its_group_are_found(self):
        names, remeasure = confirm.inconclusive_in(self.evaluations(self.noisy_record()))
        self.assertIn("H, vocab", names)
        self.assertEqual(remeasure, {"H, vocab", "H, hand-written"})

    def test_clean_run_is_conclusive(self):
        self.assertEqual(confirm.inconclusive_in(self.evaluations(record(self.base))), (set(), set()))

    def test_retry_remeasures_once_and_reports_what_stays(self):
        rec = self.noisy_record()
        calls = []
        first, final = confirm.retry_inconclusive(lambda: self.evaluations(rec), lambda names, n: calls.append((set(names), n)), 3)
        self.assertEqual(calls, [({"H, vocab", "H, hand-written"}, 3)])
        self.assertIn("H, vocab", first)
        self.assertEqual(final, first)  # the stub added nothing, so it is still inconclusive

    def test_retry_that_cures_the_noise_reports_none(self):
        rec = self.noisy_record()

        def fix(names, n):
            rec["samples"]["H, vocab"] = [500.0] * ROUNDS
        first, final = confirm.retry_inconclusive(lambda: self.evaluations(rec), fix, 3)
        self.assertTrue(first)
        self.assertEqual(final, set())

    def test_conclusive_run_is_not_remeasured(self):
        confirm.retry_inconclusive(lambda: self.evaluations(record(self.base)), lambda *a: self.fail("re-measured"), 3)

    def test_method_mismatch_is_reported_but_not_retried(self):
        rec = record(self.base)
        rec["cpu"] = 3
        baseline = baseline_data(record(self.base))
        baseline["cpu"] = 5
        rec["samples"]["G, vocab"] = [1300.0] * ROUNDS
        evaluations = [judge.evaluate(rec, baseline, 0.10, 0.05, 0.05, False)]
        names, remeasure = confirm.inconclusive_in(evaluations)
        self.assertEqual(names, {"G, vocab"})
        self.assertEqual(remeasure, set())

