"""Tests for compare: interleaved A/B of two builds, kept builds, the output contract."""

import contextlib
import io
import json
import os
import tempfile
import types
import unittest
from pathlib import Path

from .. import compare, gitstate
from .helpers import RepoTestCase, git_run, record

TIMES = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, vocab": 700.0}


def args_for(**kw):
    base = dict(exe="new.exe", against=None, kept=None, repo=None, target="tgt", filter="", rounds=10, tolerance=0.10, pair_tolerance=0.05,
                max_cv=0.05, absolute=False, instr_tolerance=0.02, epochs=0, max_load=0.2, settle=0.0, seed=1, report_only=False,
                format="brief", report=None)
    base.update(kw)
    return types.SimpleNamespace(**base)


def measuring(now_times, ref_times, seen=None):
    def measure(exe_now, exe_ref, names, rounds, args):
        if seen is not None:
            seen.append((list(names), rounds))
        return record({n: now_times[n] for n in names}, seed=3), record({n: ref_times[n] for n in names}, seed=4)
    return measure


def listing(names):
    return lambda exe, text: [n for n in names if text.lower() in n.lower()]


class CompareTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.new = self.dir / "new.exe"
        self.old = self.dir / "old.exe"
        self.new.write_bytes(b"new")
        self.old.write_bytes(b"old")

    def run_compare(self, now, ref, seen=None, names=TIMES, **kw):
        args = args_for(exe=str(self.new), against=str(self.old), **kw)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = compare.compare_command(args, measure=measuring(now, ref, seen), listing=listing(names))
        return code, out.getvalue()

    def test_a_slowdown_is_one_regressed_line(self):
        code, text = self.run_compare({**TIMES, "H, vocab": 900.0}, TIMES)
        lines = text.strip().splitlines()
        self.assertEqual(code, 1)
        self.assertTrue(lines[0].startswith("perf compare: FAIL 1 regressed"), text)
        self.assertTrue(lines[1].startswith("REGRESSED H, vocab: +"))
        self.assertTrue((self.dir / "new.compare.md").is_file())

    def test_identical_builds_are_neutral(self):
        code, text = self.run_compare(TIMES, TIMES)
        self.assertEqual(code, 0)
        self.assertEqual(len(text.strip().splitlines()), 1)
        self.assertIn("PASS 0 regressed, 0 improved", text)

    def test_a_speedup_is_an_improved_line_and_passes(self):
        code, text = self.run_compare({**TIMES, "H, vocab": 400.0}, TIMES)
        self.assertEqual(code, 0)
        self.assertIn("IMPROVED H, vocab", text)

    def test_at_least_ten_rounds_whatever_is_asked(self):
        seen = []
        self.run_compare(TIMES, TIMES, seen=seen, rounds=3)
        self.assertEqual(seen[0][1], 10)
        seen.clear()
        self.run_compare(TIMES, TIMES, seen=seen, rounds=14)
        self.assertEqual(seen[0][1], 14)

    def test_filter_keeps_whole_groups_so_a_pair_has_its_reference(self):
        seen = []
        self.run_compare(TIMES, TIMES, seen=seen, filter="G, vocab")
        self.assertEqual(seen[0][0], ["G, hand-written", "G, vocab"])

    def test_no_shared_benchmark_is_exit_2(self):
        code, text = self.run_compare(TIMES, TIMES, filter="nothing like this")
        self.assertEqual(code, 2)
        self.assertIn("share no benchmark", text)

    def test_a_build_that_cannot_run_is_exit_2(self):
        def broken(*a):
            raise OSError("missing dll")
        args = args_for(exe=str(self.new), against=str(self.old))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = compare.compare_command(args, measure=broken, listing=listing(TIMES))
        self.assertEqual(code, 2)
        self.assertIn("missing dll", out.getvalue())

    def test_json_format(self):
        code, text = self.run_compare({**TIMES, "H, vocab": 900.0}, TIMES, format="json")
        data = json.loads(text)
        self.assertEqual((data["schema"], data["command"], data["status"]), ("sub0perf-outcome-1", "compare", "FAIL"))
        self.assertEqual(data["changed"][0]["name"], "H, vocab")

    def test_missing_against_file_is_exit_2(self):
        args = args_for(exe=str(self.new), against=str(self.dir / "nope.exe"))
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(compare.compare_command(args, measure=measuring(TIMES, TIMES), listing=listing(TIMES)), 2)
        self.assertIn("not a file", out.getvalue())


class KeptBuildTest(RepoTestCase):
    def setUp(self):
        super().setUp()
        (self.repo / "f.txt").write_text("one", encoding="utf-8")
        git_run(self.repo, "add", ".")
        git_run(self.repo, "commit", "-q", "-m", "one")
        self.perf = gitstate.perf_dir(self.repo)
        self.exe = self.repo / "tgt.exe"
        self.exe.write_bytes(b"current")

    def keep(self, sha, content, age):
        d = self.perf / "bins" / "tgt" / sha
        d.mkdir(parents=True)
        (d / "tgt.exe").write_bytes(content)
        os.utime(d, (age, age))

    def resolve(self, **kw):
        return compare.resolve_against(args_for(exe=str(self.exe), repo=str(self.repo), **kw))

    def test_the_newest_kept_build_that_differs_is_chosen(self):
        self.keep("a" * 40, b"old", 1000)
        self.keep("b" * 40, b"current", 2000)  # identical to the executable being measured
        path, text = self.resolve(kept="latest")
        self.assertEqual(path.parent.name, "a" * 40)
        self.assertIn("aaaaaaaaa", text)

    def test_a_sha_prefix_picks_that_build(self):
        self.keep("a" * 40, b"old", 1000)
        self.keep("b" * 40, b"older", 2000)
        self.assertEqual(self.resolve(kept="aaaa")[0].parent.name, "a" * 40)

    def test_nothing_kept_or_only_identical_builds_say_why(self):
        path, why = self.resolve(kept="latest")
        self.assertIsNone(path)
        self.assertIn("no kept build", why)
        self.keep("b" * 40, b"current", 2000)
        path, why = self.resolve(kept="latest")
        self.assertIsNone(path)
        self.assertIn("identical", why)
        self.assertIsNone(self.resolve(kept="cccc")[0])


if __name__ == "__main__":
    unittest.main()
