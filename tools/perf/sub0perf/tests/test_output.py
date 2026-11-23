"""Tests for the output contract: brief, json and md of check/auto/gate/compare, and the held-back chatter."""

import contextlib
import io
import json
import os
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock

from .. import cli, output
from ..baseline import baseline_data, write_samples
from .helpers import record

BASE = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, vocab": 500.0, "I, vocab": 800.0, "J, vocab": 300.0}


def args_for(tmp, **kw):
    base = {"results": str(tmp / "X.samples.json"), "baseline": str(tmp / "base.json"), "tolerance": 0.10, "pair_tolerance": 0.05,
            "max_cv": 0.05, "absolute": False, "instr_tolerance": 0.02, "report_only": False, "format": None, "report": None}
    base.update(kw)
    return types.SimpleNamespace(**base)


class OutputContractTest(unittest.TestCase):
    def setUp(self):
        patch = mock.patch.dict(os.environ, {}, clear=False)
        patch.start()
        self.addCleanup(patch.stop)
        os.environ.pop(output.ENV_VAR, None)
        os.environ.pop("GITHUB_STEP_SUMMARY", None)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        (self.dir / "base.json").write_text(json.dumps(baseline_data(record(BASE))), encoding="utf-8")

    def check(self, now, **kw):
        write_samples(self.dir / "X.samples.json", record(now, seed=5))
        args = args_for(self.dir, **kw)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = cli.check_command(args)
        return code, out.getvalue(), args

    def test_format_comes_from_the_flag_then_the_environment_then_md(self):
        self.assertEqual(output.resolve_format(types.SimpleNamespace(format=None)), "md")
        with mock.patch.dict(os.environ, {output.ENV_VAR: "brief"}):
            self.assertEqual(output.resolve_format(types.SimpleNamespace(format=None)), "brief")
            self.assertEqual(output.resolve_format(types.SimpleNamespace(format="json")), "json")
        with mock.patch.dict(os.environ, {output.ENV_VAR: "nonsense"}):
            self.assertEqual(output.resolve_format(types.SimpleNamespace(format=None)), "md")

    def test_brief_is_one_verdict_line_plus_one_line_per_change(self):
        code, text, _ = self.check({**BASE, "H, vocab": 650.0}, format="brief")
        lines = text.strip().splitlines()
        self.assertEqual(code, 1)
        self.assertEqual(len(lines), 2, text)
        self.assertRegex(lines[0], r"^perf: FAIL 1 regressed, 0 improved, \d+ ok, 0 inconclusive; report .*X\.report\.md$")
        self.assertRegex(lines[1], r"^REGRESSED H, vocab: \+\d+\.\d% \(noise \d+\.\d%, tol 10%\)$")

    def test_brief_with_nothing_changed_is_a_single_line_and_the_tables_are_in_the_report_only(self):
        code, text, _ = self.check(BASE, format="brief")
        self.assertEqual(code, 0)
        self.assertEqual(len(text.strip().splitlines()), 1)
        self.assertIn("perf: PASS 0 regressed", text)
        report = (self.dir / "X.report.md").read_text(encoding="utf-8")
        self.assertIn("| Benchmark |", report)
        self.assertNotIn("| Benchmark |", text)

    def test_an_improvement_is_a_line_and_does_not_fail(self):
        code, text, _ = self.check({**BASE, "I, vocab": 500.0}, format="brief")
        self.assertEqual(code, 0)
        self.assertIn("1 improved", text)
        self.assertRegex(text, r"IMPROVED I, vocab: -")

    def test_json_is_one_object_with_the_outcome_schema(self):
        code, text, _ = self.check({**BASE, "H, vocab": 650.0}, format="json")
        data = json.loads(text)
        self.assertEqual(data["schema"], "sub0perf-outcome-1")
        self.assertEqual((data["status"], data["exit"], data["command"]), ("FAIL", 1, "check"))
        self.assertEqual(data["counts"]["regressed"], 1)
        self.assertEqual([c["name"] for c in data["changed"]], ["H, vocab"])
        self.assertEqual(data["changed"][0]["verdict"], "REGRESSED")
        self.assertTrue(data["report"].endswith("X.report.md"))
        self.assertIn("accept", data["next_action"])
        self.assertTrue(Path(data["report"]).is_file())

    def test_md_keeps_the_full_tables_and_report_writes_the_file(self):
        target = self.dir / "sub" / "r.md"
        code, text, _ = self.check({**BASE, "H, vocab": 650.0}, format="md", report=str(target))
        self.assertIn("| Benchmark |", text)
        self.assertIn("perf: 1 regressed", text)
        self.assertIn("| Benchmark |", target.read_text(encoding="utf-8"))

    def test_the_environment_makes_brief_the_default(self):
        with mock.patch.dict(os.environ, {output.ENV_VAR: "brief"}):
            _, text, _ = self.check(BASE)
        self.assertTrue(text.startswith("perf: PASS"))

    def test_report_only_never_fails_but_still_says_fail(self):
        code, text, _ = self.check({**BASE, "H, vocab": 650.0}, format="brief", report_only=True)
        self.assertEqual(code, 0)
        self.assertIn("perf: FAIL 1 regressed", text)

    def test_explicit_report_path_is_used_for_brief(self):
        target = self.dir / "elsewhere" / "mine.md"
        _, text, _ = self.check(BASE, format="brief", report=str(target))
        self.assertTrue(target.is_file())
        self.assertIn(str(target), text)


class OutcomeFileTest(unittest.TestCase):
    """`--out-json PATH` / SUB0PERF_OUTCOME_JSON: the sub0perf-outcome-1 object in a file, whatever the format on stdout."""

    def setUp(self):
        patch = mock.patch.dict(os.environ, {}, clear=False)
        patch.start()
        self.addCleanup(patch.stop)
        for var in (output.ENV_VAR, output.OUT_JSON_VAR, "GITHUB_STEP_SUMMARY"):
            os.environ.pop(var, None)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        (self.dir / "base.json").write_text(json.dumps(baseline_data(record(BASE))), encoding="utf-8")
        write_samples(self.dir / "X.samples.json", record({**BASE, "H, vocab": 650.0}, seed=5))
        self.out = self.dir / "sub" / "outcome.json"

    def main(self, *extra, command="check"):
        argv = ["bench_check.py", command, "--results", str(self.dir / "X.samples.json"), "--baseline", str(self.dir / "base.json"), *extra]
        out = io.StringIO()
        with mock.patch("sys.argv", argv), contextlib.redirect_stdout(out):
            code = cli.main()
        return code, out.getvalue()

    def read(self):
        return json.loads(self.out.read_text(encoding="utf-8"))

    def test_every_format_writes_the_same_outcome_file(self):
        outcomes = []
        for fmt in ("md", "brief", "json"):
            code, text = self.main("--format", fmt, "--out-json", str(self.out))
            data = self.read()
            self.assertEqual((code, data["schema"], data["status"], data["exit"]), (1, output.OUTCOME_SCHEMA, "FAIL", 1), fmt)
            self.assertEqual(data["counts"]["regressed"], 1)
            self.assertIn("accept", data["next_action"])
            outcomes.append(data)
            if fmt == "json":
                self.assertEqual(json.loads(text), data)  # stdout and file agree
        self.assertEqual(outcomes[1]["changed"], outcomes[2]["changed"])
        self.assertEqual(outcomes[0]["changed"], outcomes[2]["changed"])

    def test_the_environment_variable_is_honoured_and_the_flag_wins(self):
        os.environ[output.OUT_JSON_VAR] = str(self.out)
        self.main("--format", "brief")
        self.assertEqual(self.read()["status"], "FAIL")
        other = self.dir / "flag.json"
        self.main("--format", "brief", "--out-json", str(other))
        self.assertTrue(other.is_file())

    def test_a_passing_run_writes_pass_and_report_only_keeps_the_exit_zero(self):
        write_samples(self.dir / "X.samples.json", record(BASE, seed=5))
        self.main("--out-json", str(self.out))
        self.assertEqual((self.read()["status"], self.read()["exit"]), ("PASS", 0))
        write_samples(self.dir / "X.samples.json", record({**BASE, "H, vocab": 650.0}, seed=5))
        code, _ = self.main("--out-json", str(self.out), "--report-only")
        self.assertEqual((code, self.read()["exit"], self.read()["status"]), (0, 0, "FAIL"))

    def test_without_the_flag_no_file_is_written(self):
        self.main("--format", "json")
        self.assertFalse(self.out.exists())

    def test_a_run_with_no_verdict_writes_no_verdict(self):
        spec = self.dir / "bad.json"
        spec.write_text("{}", encoding="utf-8")
        argv = ["bench_check.py", "metrics", "--spec", str(spec), "--results", str(self.dir / "missing.json"), "--out-json", str(self.out)]
        with mock.patch("sys.argv", argv), contextlib.redirect_stdout(io.StringIO()):
            code = cli.main()
        data = self.read()
        self.assertEqual((code, data["status"], data["exit"], data["command"]), (2, "NO_VERDICT", 2, "metrics"))

    def test_metrics_writes_the_outcome_in_md_too(self):
        spec = self.dir / "spec.json"
        spec.write_text(json.dumps({"schema": "sub0perf-metrics-spec-1", "metrics": {"size": {"kind": "exact", "expected": 24}}}), encoding="utf-8")
        results = self.dir / "m.json"
        results.write_text(json.dumps({"metrics": [{"name": "size", "value": 32, "unit": "", "kind": "exact"}]}), encoding="utf-8")
        argv = ["bench_check.py", "metrics", "--spec", str(spec), "--results", str(results), "--format", "md", "--out-json", str(self.out)]
        with mock.patch("sys.argv", argv), contextlib.redirect_stdout(io.StringIO()):
            code = cli.main()
        data = self.read()
        self.assertEqual((code, data["command"], data["status"], data["counts"]["failed"]), (1, "metrics", "FAIL", 1))

    def test_the_file_is_not_written_by_commands_that_have_no_outcome(self):
        with mock.patch("sys.argv", ["bench_check.py", "power", "--out-json", str(self.out)]), contextlib.redirect_stdout(io.StringIO()):
            cli.main()
        self.assertFalse(self.out.exists())


class QuietTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.log = Path(self.tmp.name) / "run.log"

    def run_block(self, fmt, body):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            with output.Quiet(fmt, self.log) as quiet:
                body(quiet)
        return out.getvalue()

    def speak(self, quiet):
        print("sub0perf: measuring a round")
        print("sub0perf: round 2 of 3")
        counts = {"regressed": 0, "improved": 0, "ok": 3, "inconclusive": 0}
        output.emit_text("brief", "auto", counts, [], "table", Path(self.tmp.name) / "r.md", types.SimpleNamespace(), 0)
        print("sub0perf: WARNING: noisy machine")

    def test_chatter_is_held_back_and_logged_but_warnings_are_echoed(self):
        text = self.run_block("brief", self.speak)
        self.assertIn("perf: PASS", text)
        self.assertNotIn("measuring a round", text)
        self.assertIn("WARNING: noisy machine", text)
        self.assertIn("measuring a round", self.log.read_text(encoding="utf-8"))

    def test_without_an_outcome_the_tail_is_shown(self):
        text = self.run_block("brief", lambda q: print("sub0perf: push refused: dirty tree"))
        self.assertIn("push refused", text)

    def test_json_without_an_outcome_is_still_one_json_object(self):
        def body(quiet):
            print("sub0perf: nothing to measure")
            quiet.exit_code = 0
        data = json.loads(self.run_block("json", body))
        self.assertEqual((data["status"], data["exit"]), ("NO_VERDICT", 0))
        self.assertIn("nothing to measure", data["log"][0])

    def test_md_is_untouched(self):
        self.assertEqual(self.run_block("md", lambda q: print("hello")), "hello\n")


if __name__ == "__main__":
    unittest.main()
