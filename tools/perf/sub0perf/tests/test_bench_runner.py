"""Tests for bench_runner.hpp: --metrics-only and --smoke, with a tiny benchmark executable built by the host C++ compiler."""

import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
PERF = ROOT / "tools" / "perf"
VENDOR = ROOT / "tests" / "vendor"
COMPILER = shutil.which("g++") or shutil.which("clang++") or shutil.which("c++")

SOURCE = r'''
#define ANKERL_NANOBENCH_IMPLEMENT
#include <bench_runner.hpp>
#include <cstdio>

int main(int argc, char** argv)
{
    sub0bench::BenchRunner runner(argc, argv, "tiny");
    runner.metric("sizeof int", sizeof(int), "B");
    runner.metric("odd \"name\"\\", 1.5, "x", sub0bench::MetricKind::max);
    runner.metric("sizeof int", 4, "B");  // the last value of a name wins
    int ran = 0;
    runner.run("add, double", [&] { ++ran; ankerl::nanobench::doNotOptimizeAway(ran); });
    runner.run("mul, double", [&] { ankerl::nanobench::doNotOptimizeAway(ran *= 2); });
    runner.run("inside", [&] { runner.metric("ran", 7, "n"); });
    return runner.finish();
}
'''


@unittest.skipUnless(COMPILER and (VENDOR / "nanobench.h").is_file(), "needs a C++ compiler and tests/vendor/nanobench.h")
class BenchRunnerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.dir = Path(cls.tmp.name)
        source = cls.dir / "tiny.cpp"
        source.write_text(SOURCE, encoding="utf-8")
        cls.exe = cls.dir / "tiny.exe"
        built = subprocess.run([COMPILER, "-std=c++20", "-O0", f"-I{PERF}", f"-I{VENDOR}", str(source), "-o", str(cls.exe)],
                               capture_output=True, text=True)
        if built.returncode != 0:
            raise RuntimeError(built.stderr[-2000:])

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_exe(self, *args):
        return subprocess.run([str(self.exe), *args], capture_output=True, text=True, timeout=120)

    def test_metrics_only_writes_the_metrics_to_stdout_without_timing(self):
        done = self.run_exe("--metrics-only")
        self.assertEqual(done.returncode, 0, done.stderr)
        data = json.loads(done.stdout)
        by_name = {m["name"]: m for m in data["metrics"]}
        self.assertEqual(by_name["sizeof int"], {"name": "sizeof int", "value": 4, "unit": "B", "kind": "exact"})
        self.assertEqual(by_name['odd "name"\\']["kind"], "max")
        self.assertEqual(by_name['odd "name"\\']["value"], 1.5)
        self.assertEqual(by_name["ran"]["value"], 7)  # registered inside a workload, which ran once
        self.assertEqual(len(data["metrics"]), 3)
        self.assertNotIn("results", data)

    def test_metrics_only_writes_the_json_file_when_asked(self):
        out = self.dir / "m.json"
        done = self.run_exe("--metrics-only", "--json", str(out))
        self.assertEqual(done.returncode, 0)
        self.assertEqual(done.stdout, "")
        self.assertIn("metrics", json.loads(out.read_text(encoding="utf-8")))

    def test_smoke_is_exit_status_only(self):
        done = self.run_exe("--smoke")
        self.assertEqual((done.returncode, done.stdout, done.stderr), (0, "", ""))

    def test_smoke_with_no_matching_benchmark_fails(self):
        self.assertEqual(self.run_exe("--smoke", "--filter", "nothing here").returncode, 1)

    def test_metrics_feed_the_metrics_command_end_to_end(self):
        from .. import metrics
        spec = self.dir / "spec.json"
        spec.write_text(json.dumps({"metrics": {"sizeof int": {"kind": "exact", "expected": 4},
                                                 'odd "name"\\': {"kind": "max", "limit": 2}, "ran": {"kind": "exact", "expected": 7}}}),
                        encoding="utf-8")
        args = type("A", (), {"results": None, "exe": str(self.exe), "out": str(self.dir / "o.json"), "spec": str(spec)})
        self.assertEqual(metrics.results_file(args), str(self.dir / "o.json"))
        rows = metrics.judge(metrics.load_results(args.out), metrics.load_spec(spec))
        self.assertEqual({s for _, s, _ in rows}, {"ok"})

    def test_a_normal_run_still_times_and_writes_nanobench_json(self):
        out = self.dir / "t.json"
        done = self.run_exe("--exact", "add, double", "--epochs", "2", "--json", str(out))
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual([r["name"] for r in json.loads(out.read_text(encoding="utf-8"))["results"]], ["add, double"])


if __name__ == "__main__":
    unittest.main()
