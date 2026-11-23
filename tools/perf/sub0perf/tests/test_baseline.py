"""Tests for baseline."""

import json
import os
import platform
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from .. import baseline, common, gitstate
from .helpers import git_run


class AtomicWriteTest(unittest.TestCase):
    def test_replaces_content_and_leaves_no_temp_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sub" / "f.json"
            common.atomic_write_text(path, "one\n")
            common.atomic_write_text(path, "two\n")
            self.assertEqual(path.read_text(encoding="utf-8"), "two\n")
            self.assertEqual([p.name for p in path.parent.iterdir()], ["f.json"])

    def test_failed_write_keeps_the_old_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "f.json"
            common.atomic_write_text(path, "old\n")
            with self.assertRaises(TypeError):
                common.atomic_write_text(path, 5)  # not text: the temp file is dropped, the target untouched
            self.assertEqual(path.read_text(encoding="utf-8"), "old\n")
            self.assertEqual([p.name for p in path.parent.iterdir()], ["f.json"])

    def test_baseline_and_samples_use_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = baseline.make_record("some/bench.exe", {"a": [1.0]}, [], {"a": [0.5]}, {}, 0, 42)
            baseline.write_samples(Path(tmp) / "s.json", rec)
            baseline.write_baseline(Path(tmp) / "b" / "base.json", {"format": 2})
            self.assertEqual(json.loads((Path(tmp) / "s.json").read_text(encoding="utf-8"))["seed"], 42)
            self.assertEqual(json.loads((Path(tmp) / "b" / "base.json").read_text(encoding="utf-8")), {"format": 2})


class HostNameTest(unittest.TestCase):
    """SUB0MATH_PERF_HOST is the name the baseline file carries; records and baselines record it, not the machine's own name."""

    def setUp(self):
        patch = mock.patch.dict(os.environ)
        patch.start()
        self.addCleanup(patch.stop)
        os.environ.pop(baseline.HOST_VAR, None)

    def record(self):
        return baseline.make_record("dir/Bench.exe", {"a": [1.0, 1.1, 0.9]}, [{"round": r, "calibration_start_ns": 1000.0,
                                    "calibration_end_ns": 1000.0} for r in range(3)], {"a": [0.5] * 3}, None, 3, 7)

    def test_without_the_variable_the_machine_name_is_recorded(self):
        self.assertEqual(self.record()["host"], platform.node())
        self.assertEqual(baseline.baseline_data(self.record())["host"], platform.node())

    def test_the_variable_is_recorded_in_the_record_and_the_baseline(self):
        os.environ[baseline.HOST_VAR] = "ci-box"
        self.assertEqual(self.record()["host"], "ci-box")
        self.assertEqual(baseline.baseline_data(self.record())["host"], "ci-box")

    def test_a_baseline_made_later_from_an_older_record_still_uses_the_variable(self):
        old = self.record()
        old["host"] = "REAL-HOSTNAME"
        self.assertEqual(baseline.baseline_data(old)["host"], "REAL-HOSTNAME")
        os.environ[baseline.HOST_VAR] = "  ci-box "
        self.assertEqual(baseline.baseline_data(old)["host"], "ci-box")

    def test_an_empty_variable_is_ignored(self):
        os.environ[baseline.HOST_VAR] = " "
        self.assertEqual(self.record()["host"], platform.node())


class RecordTest(unittest.TestCase):
    def test_make_record_has_format_exe_and_seed(self):
        rec = baseline.make_record("dir/Bench.exe", {"a": [1.0]}, [{"round": 0}], {"a": [0.5]}, None, 3, 7)
        self.assertEqual((rec["format"], rec["exe"], rec["epochs"], rec["seed"], rec["counters"]), (2, "Bench.exe", 3, 7, {}))
        for key in ("cpu", "host", "machine", "system", "rounds", "samples", "positions"):
            self.assertIn(key, rec)

    def test_saved_record_keeps_the_seed(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            git_run(repo, "init", "-q")
            git_run(repo, "commit", "-q", "--allow-empty", "-m", "c")
            rec = baseline.make_record("b.exe", {"a": [1.0]}, [], {}, {}, 0, 9)
            path = gitstate.save_record(repo, "tgt", rec)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["seed"], 9)

