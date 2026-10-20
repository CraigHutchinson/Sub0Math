"""Tests for accept: moving a baseline to a measurement, and refusing a measurement that is not there."""

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from .. import accept
from ..baseline import baseline_data, write_samples
from .helpers import record


class AcceptTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.base = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, vocab": 500.0}
        self.baseline = self.dir / "base.json"
        self.baseline.write_text(json.dumps(baseline_data(record(self.base))), encoding="utf-8")

    def accept(self, rec, names):
        results = self.dir / "samples.json"
        write_samples(results, rec)
        args = type("A", (), {"results": str(results), "baseline": str(self.baseline), "reason": "because", "all_changed": False,
                              "benchmark": names})
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = accept.accept_command(args)
        return code, out.getvalue()

    def logged(self):
        return json.loads(self.baseline.read_text(encoding="utf-8"))

    def test_measured_benchmark_moves_and_is_logged(self):
        slower = dict(self.base, **{"G, vocab": 1300.0})
        code, _ = self.accept(record(slower), ["G, vocab"])
        data = self.logged()
        self.assertEqual(code, 0)
        self.assertAlmostEqual(data["benchmarks"]["G, vocab"]["median_ns"], 1300.0, delta=40)
        change = data["accepted"][-1]["benchmarks"]["G, vocab"]
        self.assertNotEqual(change["from_ns"], change["to_ns"])

    def test_benchmark_without_samples_is_an_error_and_changes_nothing(self):
        only_g = {k: v for k, v in self.base.items() if k.startswith("G")}
        before = self.baseline.read_text(encoding="utf-8")
        code, out = self.accept(record(only_g), ["H, vocab"])  # absent from the results
        self.assertEqual(code, 2)
        self.assertIn("H, vocab", out)
        self.assertIn("--filter", out)
        self.assertEqual(self.baseline.read_text(encoding="utf-8"), before)

    def test_empty_and_filled_only_samples_count_as_no_data(self):
        rec = record(self.base)
        rec["samples"]["H, vocab"] = []
        rec["positions"]["H, vocab"] = []
        code, out = self.accept(rec, ["H, vocab"])
        self.assertEqual(code, 2)
        self.assertIn("H, vocab", out)
        rec = record(self.base)
        rec["positions"]["H, vocab"] = [None] * len(rec["positions"]["H, vocab"])  # only filled-in copies of a median
        code, out = self.accept(rec, ["H, vocab"])
        self.assertEqual(code, 2)

    def test_every_missing_benchmark_is_named_and_good_ones_are_not_half_accepted(self):
        only_g = {k: v for k, v in self.base.items() if k.startswith("G")}
        before = self.baseline.read_text(encoding="utf-8")
        code, out = self.accept(record(only_g), ["G, vocab", "H, vocab", "Nope"])
        self.assertEqual(code, 2)
        self.assertIn("H, vocab", out)
        self.assertIn("Nope", out)
        self.assertEqual(self.baseline.read_text(encoding="utf-8"), before)


if __name__ == "__main__":
    unittest.main()
