"""Self-test for bench_check.py: synthetic samples, no executables. Run: python -m unittest discover tools/perf"""

import random
import statistics
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_check as bc  # noqa: E402

ROUNDS = 5


def record(times, jitter=0.01, seed=1):
    """A samples record: `times` maps name to ns; constant calibration; small multiplicative jitter per round."""
    rng = random.Random(seed)
    meta = [{"round": r, "load": 0.0, "waited_s": 0.0, "quiet": True, "calibration_start_ns": 1000.0,
             "calibration_end_ns": 1000.0} for r in range(ROUNDS)]
    samples = {n: [t * (1.0 + rng.uniform(-jitter, jitter)) for _ in range(ROUNDS)] for n, t in times.items()}
    positions = {n: [0.5] * ROUNDS for n in times}
    return {"rounds": meta, "samples": samples, "positions": positions, "counters": {}, "epochs": 0}


def judge_against(base_times, now_times, seed=2):
    baseline = bc.baseline_data(record(base_times, seed=1))
    rows, pairs, env = bc.evaluate(record(now_times, seed=seed), baseline, 0.10, 0.05, 0.05, False)
    return {r[0]: r[5] for r in rows}, pairs, env


NAMES = [f"Bench {i}" for i in range(8)]


class JudgingTest(unittest.TestCase):
    def test_single_shift_with_controls_regresses(self):
        base = {f"Group {i}, vocabulary": 1000.0 + 100 * i for i in range(6)}
        base["Group 0, hand-written"] = 900.0
        now = dict(base)
        now["Group 3, vocabulary"] *= 1.30
        verdicts, _, env = judge_against(base, now)
        self.assertEqual(verdicts["Group 3, vocabulary"], "REGRESSED")
        self.assertEqual(sum(v == "REGRESSED" for v in verdicts.values()), 1)
        self.assertIsNone(env["suite_spread"])

    def test_random_shifts_without_controls_do_not_block(self):
        shifts = [-0.15, 0.15, 0.12, -0.14, 0.15, -0.10, 0.13, -0.12]  # as if random, +-15%
        base = {n: 1000.0 + 50 * i for i, n in enumerate(NAMES)}
        now = {n: t * (1.0 + d) for (n, t), d in zip(base.items(), shifts)}
        verdicts, _, env = judge_against(base, now)
        self.assertGreater(env["suite_spread"], 0.05)
        self.assertNotIn("REGRESSED", verdicts.values())
        # The same data with the floor disabled would have blocked something.
        self.assertGreater(max(now[n] / base[n] - 1.0 for n in base), 0.10)

    def test_genuine_shift_without_controls_regresses(self):
        base = {n: 1000.0 + 50 * i for i, n in enumerate(NAMES)}
        now = dict(base)
        now["Bench 2"] *= 1.40
        verdicts, _, env = judge_against(base, now)
        self.assertEqual(verdicts["Bench 2"], "REGRESSED")
        self.assertEqual(sum(v == "REGRESSED" for v in verdicts.values()), 1)
        self.assertIsNotNone(env["suite_spread"])
        self.assertIn("No controls", bc.render(*bc.evaluate(record(now), bc.baseline_data(record(base)), 0.10, 0.05, 0.05, False),
                                              "baseline", 0.10, 0.05))


class ConfirmationTest(unittest.TestCase):
    def setUp(self):
        self.base = {n: 1000.0 + 50 * i for i, n in enumerate(NAMES)}
        self.baseline = bc.baseline_data(record(self.base))
        self.now = dict(self.base)
        self.now["Bench 1"] *= 1.50  # looks regressed on the first pass
        self.now["Bench 4"] *= 1.50
        self.rec = record(self.now)

    def evaluate_all(self):
        return [bc.evaluate(self.rec, self.baseline, 0.10, 0.05, 0.05, False)]

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
        bc.extend_rounds(run, {"Bench 1"}, 2, rec["samples"], rec["rounds"], rec["positions"], rec["counters"])
        self.assertEqual(len(rec["rounds"]), ROUNDS + 2)
        self.assertEqual([c[1] for c in calls], [ROUNDS, ROUNDS + 1])
        self.assertEqual(calls[0][0], ["Bench 1"])
        for n, v in rec["samples"].items():
            self.assertEqual(len(v), ROUNDS + 2, n)
            self.assertEqual(len(rec["positions"][n]), ROUNDS + 2, n)

    def test_noise_is_not_confirmed(self):
        run, calls = self.stub(self.base)  # the re-measurement shows no regression
        rec = self.rec
        first, final = bc.confirm_regressions(
            self.evaluate_all,
            lambda names, n: bc.extend_rounds(run, names, n, rec["samples"], rec["rounds"], rec["positions"], rec["counters"]), 8)
        self.assertEqual(first, {"Bench 1", "Bench 4"})
        self.assertEqual(final, set())
        self.assertEqual({n for c in calls for n in c[0]}, {"Bench 1", "Bench 4"})

    def test_real_regression_is_confirmed(self):
        run, _ = self.stub(self.now)
        rec = self.rec
        first, final = bc.confirm_regressions(
            self.evaluate_all,
            lambda names, n: bc.extend_rounds(run, names, n, rec["samples"], rec["rounds"], rec["positions"], rec["counters"]), 4)
        self.assertEqual(final, {"Bench 1", "Bench 4"})

    def test_nothing_regressed_measures_nothing(self):
        self.rec = record(self.base)
        first, final = bc.confirm_regressions(self.evaluate_all, lambda *a: self.fail("re-measured"), 4)
        self.assertEqual((first, final), (set(), set()))

    def test_pair_remeasures_its_reference(self):
        base = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, hand-written": 500.0, "H, vocab": 500.0}
        baseline = bc.baseline_data(record(base))
        now = dict(base)
        now["G, vocab"] *= 1.30
        found = bc.regressions_in([bc.evaluate(record(now), baseline, 0.10, 0.05, 0.05, False)])
        self.assertEqual(found[0], {"G, vocab"})
        self.assertEqual(found[1], {"G, vocab", "G, hand-written"})


if __name__ == "__main__":
    unittest.main()
