"""Tests for deterministic metrics: exact, max and xfail judging, the output contract, and logged acceptance."""

import contextlib
import io
import json
import tempfile
import types
import unittest
from pathlib import Path

from .. import metrics


def spec_of(**entries):
    return {"schema": metrics.SPEC_SCHEMA, "metrics": entries, "accepted": []}


class MetricsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.spec = self.dir / "metrics.json"
        self.results = self.dir / "results.json"
        self.write_spec(**{"size": {"kind": "exact", "expected": 24, "unit": "B"}, "steps": {"kind": "max", "limit": 40}})

    def write_spec(self, **entries):
        self.spec.write_text(json.dumps(spec_of(**entries)), encoding="utf-8")

    def write_results(self, **values):
        items = [{"name": n, "value": v, "unit": "", "kind": "max" if n == "steps" else "exact"} for n, v in values.items()]
        self.results.write_text(json.dumps({"metrics": items}), encoding="utf-8")

    def run_metrics(self, fmt="brief", **kw):
        args = types.SimpleNamespace(results=str(self.results), spec=str(self.spec), exe=None, out=None, format=fmt, report=None,
                                     report_only=False, **kw)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = metrics.metrics_command(args)
        return code, out.getvalue()

    def statuses(self):
        return {n: s for n, s, _ in metrics.judge(metrics.load_results(self.results), metrics.load_spec(self.spec))}

    def test_exact_and_max_pass(self):
        self.write_results(size=24, steps=40)
        code, text = self.run_metrics()
        self.assertEqual(code, 0)
        self.assertEqual(text.strip().splitlines()[0].split(";")[0], "perf metrics: PASS 0 failed, 0 xfail, 2 ok")
        self.assertEqual(len(text.strip().splitlines()), 1)

    def test_exact_mismatch_and_max_overrun_fail_with_a_line_each(self):
        self.write_results(size=32, steps=41)
        code, text = self.run_metrics()
        lines = text.strip().splitlines()
        self.assertEqual(code, 1)
        self.assertIn("2 failed", lines[0])
        self.assertIn("FAILED size: 32 B != expected 24", lines[1])
        self.assertIn("FAILED steps: 41 > max 40", lines[2])

    def test_a_smaller_max_value_and_a_tolerance_pass(self):
        self.write_spec(size={"kind": "exact", "expected": 1.0, "tolerance": 0.001}, steps={"kind": "max", "limit": 40})
        self.write_results(size=1.0005, steps=3)
        self.assertEqual(self.statuses(), {"size": "ok", "steps": "ok"})

    def test_xfail_does_not_block_while_failing_and_blocks_when_it_passes(self):
        self.write_spec(size={"kind": "exact", "expected": 24, "xfail": "layout is being reworked"})
        self.write_results(size=32)
        code, text = self.run_metrics()
        self.assertEqual((code, self.statuses()["size"]), (0, "XFAIL"))
        self.assertIn("1 xfail", text)
        self.write_results(size=24)
        code, text = self.run_metrics()
        self.assertEqual((code, self.statuses()["size"]), (1, "XPASS"))
        self.assertIn("remove the xfail", text)

    def test_missing_blocks_and_new_is_listed_without_blocking(self):
        self.write_results(size=24, extra=7)
        code, text = self.run_metrics()
        self.assertEqual(code, 1)
        self.assertIn("MISSING steps", text)
        self.assertIn("NEW extra: 7", text)
        self.write_results(size=24, steps=1, extra=7)
        self.assertEqual(self.run_metrics()[0], 0)

    def test_json_outcome(self):
        self.write_results(size=32, steps=1)
        code, text = self.run_metrics("json")
        data = json.loads(text)
        self.assertEqual((data["schema"], data["command"], data["status"], data["exit"]), ("sub0perf-outcome-1", "metrics", "FAIL", 1))
        self.assertEqual(data["counts"]["failed"], 1)
        self.assertEqual(data["changed"][0]["name"], "size")
        self.assertIn("accept", data["next_action"])

    def test_md_prints_the_table(self):
        self.write_results(size=24, steps=1)
        _, text = self.run_metrics("md")
        self.assertIn("| Metric | Status | Detail |", text)

    def test_malformed_input_is_exit_2_not_a_verdict(self):
        self.results.write_text("{}", encoding="utf-8")
        code, text = self.run_metrics()
        self.assertEqual(code, 2)
        self.assertIn("no 'metrics' list", text)
        self.write_results(size=24, steps=1)
        self.spec.write_text(json.dumps({"metrics": {"size": {"kind": "exact"}}}), encoding="utf-8")
        self.assertEqual(self.run_metrics()[0], 2)

    def accept(self, names=None, reason="because", all_changed=False):
        args = types.SimpleNamespace(results=str(self.results), spec=str(self.spec), exe=None, out=None, metric=names or [], reason=reason,
                                     all_changed=all_changed)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = metrics.accept_metrics_command(args)
        return code, out.getvalue()

    def test_accept_moves_the_value_and_logs_it(self):
        self.write_results(size=32, steps=41)
        code, _ = self.accept(["size"])
        data = json.loads(self.spec.read_text(encoding="utf-8"))
        self.assertEqual(code, 0)
        self.assertEqual(data["metrics"]["size"]["expected"], 32)
        self.assertEqual(data["metrics"]["steps"]["limit"], 40)  # not named, not moved
        entry = data["accepted"][-1]
        self.assertEqual((entry["reason"], entry["metrics"]["size"]["from"], entry["metrics"]["size"]["to"]), ("because", 24, 32))
        self.assertIn("date", entry)

    def test_accept_max_moves_the_limit_and_clears_an_xfail(self):
        self.write_spec(steps={"kind": "max", "limit": 10, "xfail": "known"})
        self.write_results(steps=50)
        self.accept(["steps"])
        data = json.loads(self.spec.read_text(encoding="utf-8"))
        self.assertEqual(data["metrics"]["steps"], {"kind": "max", "limit": 50})
        self.assertEqual(data["accepted"][-1]["metrics"]["steps"]["xfail_cleared"], "known")

    def test_accept_all_changed_takes_failing_and_new_metrics(self):
        self.write_results(size=32, steps=1, extra=3)
        self.accept(all_changed=True)
        data = json.loads(self.spec.read_text(encoding="utf-8"))
        self.assertEqual(sorted(data["accepted"][-1]["metrics"]), ["extra", "size"])
        self.assertEqual(data["metrics"]["extra"], {"kind": "exact", "expected": 3})

    def test_accept_a_metric_with_no_value_is_an_error_and_changes_nothing(self):
        self.write_results(size=32)
        before = self.spec.read_text(encoding="utf-8")
        code, text = self.accept(["steps"])
        self.assertEqual(code, 2)
        self.assertIn("steps", text)
        self.assertEqual(self.spec.read_text(encoding="utf-8"), before)

    HAND_WRITTEN = """{
  "schema": "sub0perf-metrics-spec-1",
  "metrics": {
    "size": {"expected": 24, "kind": "exact", "unit": "B"},
    "steps": {"kind": "max", "limit": 40, "xfail": "known"},
    "alloc": {
        "kind": "exact",
        "expected": 2,
        "xfail": "two today"
    },
    "ok": {"kind": "exact", "expected": 5}
  },
  "accepted": []
}
"""

    def write_hand_written(self):
        self.spec.write_text(self.HAND_WRITTEN, encoding="utf-8", newline="\n")

    def write_all_results(self, **values):
        items = [{"name": n, "value": v, "unit": "", "kind": "max" if n == "steps" else "exact"} for n, v in values.items()]
        self.results.write_text(json.dumps({"metrics": items}), encoding="utf-8")

    def test_all_changed_never_clears_an_xfail_and_says_so(self):
        self.write_hand_written()
        self.write_all_results(size=32, steps=99, alloc=3, ok=5)  # size FAILs; steps and alloc are XFAIL
        code, text = self.accept(all_changed=True)
        data = json.loads(self.spec.read_text(encoding="utf-8"))
        self.assertEqual(code, 0)
        self.assertEqual(data["metrics"]["size"]["expected"], 32)
        self.assertEqual(data["metrics"]["steps"], {"kind": "max", "limit": 40, "xfail": "known"})
        self.assertEqual(data["metrics"]["alloc"]["expected"], 2)
        self.assertEqual(sorted(data["accepted"][-1]["metrics"]), ["size"])
        self.assertIn("steps", text)
        self.assertIn("alloc", text)

    def test_all_changed_with_only_xfail_failing_is_nothing_to_accept(self):
        self.write_hand_written()
        self.write_all_results(size=24, steps=99, alloc=3, ok=5)
        before = self.spec.read_text(encoding="utf-8")
        code, text = self.accept(all_changed=True)
        self.assertEqual(code, 2)
        self.assertIn("by name", text)
        self.assertEqual(self.spec.read_text(encoding="utf-8"), before)

    def test_naming_an_xfail_metric_clears_just_that_marker(self):
        self.write_hand_written()
        self.write_all_results(size=24, steps=99, alloc=3, ok=5)
        self.accept(["alloc"])
        data = json.loads(self.spec.read_text(encoding="utf-8"))
        self.assertEqual(data["metrics"]["alloc"], {"kind": "exact", "expected": 3})
        self.assertEqual(data["metrics"]["steps"]["xfail"], "known")
        self.assertEqual(data["accepted"][-1]["metrics"]["alloc"]["xfail_cleared"], "two today")

    def test_accept_keeps_the_layout_and_key_order_of_the_spec(self):
        self.write_hand_written()
        self.write_all_results(size=32, steps=99, alloc=3, ok=5, extra=7)
        self.accept(["size", "alloc"], all_changed=True)
        text = self.spec.read_text(encoding="utf-8")
        expected_start = self.HAND_WRITTEN[:self.HAND_WRITTEN.index('    "alloc"')].replace('"expected": 24', '"expected": 32')
        self.assertTrue(text.startswith(expected_start), text)  # key order and layout of the untouched part
        self.assertIn('    "steps": {"kind": "max", "limit": 40, "xfail": "known"},\n', text)
        self.assertIn('        "kind": "exact",\n        "expected": 3\n    },\n    "ok": {"kind": "exact", "expected": 5},\n'
                      '    "extra": {"kind": "exact", "expected": 7}\n  },\n', text)
        data = json.loads(text)
        self.assertEqual(list(data["metrics"]["size"]), ["expected", "kind", "unit"])
        self.assertEqual(sorted(data["accepted"][-1]["metrics"]), ["alloc", "extra", "size"])

    def test_a_missing_spec_is_created_whole(self):
        self.spec.unlink()
        self.write_results(size=32)
        self.assertEqual(self.accept(["size"])[0], 0)
        data = json.loads(self.spec.read_text(encoding="utf-8"))
        self.assertEqual(data["metrics"]["size"], {"kind": "exact", "expected": 32})
        self.assertEqual(data["schema"], metrics.SPEC_SCHEMA)

    def test_a_spec_without_an_accepted_list_gets_one_added_in_place(self):
        self.spec.write_text('{\n  "metrics": {\n    "size": {"kind": "exact", "expected": 24}\n  }\n}\n', encoding="utf-8", newline="\n")
        self.write_results(size=32)
        self.accept(["size"])
        text = self.spec.read_text(encoding="utf-8")
        self.assertTrue(text.startswith('{\n  "metrics": {\n    "size": {"kind": "exact", "expected": 32}\n  },\n  "accepted": ['), text)
        self.assertEqual(json.loads(text)["accepted"][0]["metrics"]["size"]["to"], 32)

    def test_accept_with_nothing_named_is_an_error(self):
        self.write_results(size=32)
        self.assertEqual(self.accept([])[0], 2)


if __name__ == "__main__":
    unittest.main()
