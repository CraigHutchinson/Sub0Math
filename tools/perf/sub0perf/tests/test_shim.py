"""The command line that consumers and installed hooks call by path: tools/perf/bench_check.py."""

import subprocess
import sys
import unittest
from pathlib import Path

SHIM = Path(__file__).resolve().parents[2] / "bench_check.py"


class ShimTest(unittest.TestCase):
    def test_help_lists_every_command(self):
        result = subprocess.run([sys.executable, str(SHIM), "--help"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for command in ("run", "check", "baseline", "auto", "accept", "gate", "compare", "metrics", "install-hooks", "note-checkout", "workload"):
            self.assertIn(command, result.stdout)

    def test_missing_arguments_are_a_usage_error(self):
        result = subprocess.run([sys.executable, str(SHIM), "accept"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("accept needs", result.stderr)

    def test_metrics_and_logged_accept_through_the_command_line(self):
        import json
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            spec, results = Path(d) / "spec.json", Path(d) / "m.json"
            spec.write_text(json.dumps({"metrics": {"size": {"kind": "exact", "expected": 24}}}), encoding="utf-8")
            results.write_text(json.dumps({"metrics": [{"name": "size", "value": 32, "unit": "B", "kind": "exact"}]}), encoding="utf-8")
            run = lambda *a: subprocess.run([sys.executable, str(SHIM), *a], capture_output=True, text=True)
            failed = run("metrics", "--results", str(results), "--spec", str(spec), "--format", "brief")
            self.assertEqual(failed.returncode, 1, failed.stdout + failed.stderr)
            self.assertIn("FAILED size: 32 B != expected 24", failed.stdout)
            self.assertEqual(run("accept", "--results", str(results), "--spec", str(spec), "--metric", "size").returncode, 2)  # needs a reason
            done = run("accept", "--results", str(results), "--spec", str(spec), "--metric", "size", "--reason", "layout grew on purpose")
            self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
            self.assertEqual(run("metrics", "--results", str(results), "--spec", str(spec), "--format", "brief").returncode, 0)
            logged = json.loads(spec.read_text(encoding="utf-8"))["accepted"][-1]
            self.assertEqual(logged["reason"], "layout grew on purpose")

    def test_installed_hooks_point_at_the_shim(self):
        from ..common import tooling_script
        self.assertEqual(tooling_script(), SHIM)


if __name__ == "__main__":
    unittest.main()
