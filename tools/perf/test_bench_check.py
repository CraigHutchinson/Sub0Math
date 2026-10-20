"""Self-test for bench_check.py: synthetic samples, no executables. Run: python -m unittest discover tools/perf"""

import json
import os
import random
import shutil
import statistics
import subprocess
import sys
import tempfile
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

    def test_large_environment_shift_makes_absolute_verdicts_inconclusive(self):
        base = {f"Group {i}, vocabulary": 1000.0 + 100 * i for i in range(4)}
        base.update({f"Group {i}, hand-written": 900.0 + 100 * i for i in range(4)})
        now = {n: t * 1.40 for n, t in base.items()}  # everything, controls included, 40% slower
        now["Group 2, vocabulary"] *= 1.15  # plus a residual that would otherwise count
        verdicts, pairs, env = judge_against(base, now)
        self.assertGreater(env["environment_shift"], 0.10)
        self.assertNotIn("REGRESSED", verdicts.values())
        self.assertEqual(verdicts["Group 2, vocabulary"], "ENVIRONMENT")
        # The pair ratio for group 2 is relative to the same environment and still judges.
        self.assertEqual({p[0]: p[4] for p in pairs}["Group 2, vocabulary"], "REGRESSED")

    def test_pair_ratio_ignores_filled_rounds(self):
        rec = record({"G, vocabulary": 1000.0, "G, hand-written": 1000.0}, jitter=0.0)
        # Two extra rounds in a 30% slower environment: only the vocabulary member was re-measured,
        # the reference got its median copied in (as alignment filling does).
        for _ in range(2):
            rec["rounds"].append(dict(rec["rounds"][0]))
            rec["samples"]["G, vocabulary"].append(1300.0)
            rec["positions"]["G, vocabulary"].append(0.5)
            rec["samples"]["G, hand-written"].append(1000.0)
            rec["positions"]["G, hand-written"].append(None)
        stats, _, _, _ = bc.summarise(rec)
        pairs = bc.pair_ratios(stats)
        self.assertAlmostEqual(pairs["G, vocabulary"]["ratio"], 1.0, places=6)

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


class InconclusiveTest(unittest.TestCase):
    def setUp(self):
        self.base = {"G, hand-written": 1000.0, "G, vocab": 1000.0, "H, hand-written": 500.0, "H, vocab": 500.0}
        self.baseline = bc.baseline_data(record(self.base))

    def noisy_record(self):
        rec = record(self.base)
        rec["samples"]["H, vocab"] = [500.0, 800.0, 300.0, 650.0, 420.0]  # far above max_cv
        return rec

    def evaluations(self, rec):
        return [bc.evaluate(rec, self.baseline, 0.10, 0.05, 0.05, False)]

    def test_noisy_benchmark_and_its_group_are_found(self):
        names, remeasure = bc.inconclusive_in(self.evaluations(self.noisy_record()))
        self.assertIn("H, vocab", names)
        self.assertEqual(remeasure, {"H, vocab", "H, hand-written"})

    def test_clean_run_is_conclusive(self):
        self.assertEqual(bc.inconclusive_in(self.evaluations(record(self.base))), (set(), set()))

    def test_retry_remeasures_once_and_reports_what_stays(self):
        rec = self.noisy_record()
        calls = []
        first, final = bc.retry_inconclusive(lambda: self.evaluations(rec), lambda names, n: calls.append((set(names), n)), 3)
        self.assertEqual(calls, [({"H, vocab", "H, hand-written"}, 3)])
        self.assertIn("H, vocab", first)
        self.assertEqual(final, first)  # the stub added nothing, so it is still inconclusive

    def test_retry_that_cures_the_noise_reports_none(self):
        rec = self.noisy_record()

        def fix(names, n):
            rec["samples"]["H, vocab"] = [500.0] * ROUNDS
        first, final = bc.retry_inconclusive(lambda: self.evaluations(rec), fix, 3)
        self.assertTrue(first)
        self.assertEqual(final, set())

    def test_conclusive_run_is_not_remeasured(self):
        bc.retry_inconclusive(lambda: self.evaluations(record(self.base)), lambda *a: self.fail("re-measured"), 3)

    def test_method_mismatch_is_reported_but_not_retried(self):
        rec = record(self.base)
        rec["cpu"] = 3
        baseline = bc.baseline_data(record(self.base))
        baseline["cpu"] = 5
        rec["samples"]["G, vocab"] = [1300.0] * ROUNDS
        evaluations = [bc.evaluate(rec, baseline, 0.10, 0.05, 0.05, False)]
        names, remeasure = bc.inconclusive_in(evaluations)
        self.assertEqual(names, {"G, vocab"})
        self.assertEqual(remeasure, set())


class MethodMismatchTest(unittest.TestCase):
    def method(self, record_cpu, baseline_cpu):
        times = {"G, hand-written": 1000.0, "G, vocab": 1000.0}
        rec = record(times)
        base = bc.baseline_data(record(times))
        if record_cpu != "absent":
            rec["cpu"] = record_cpu
        if baseline_cpu == "absent":
            del base["cpu"]
        else:
            base["cpu"] = baseline_cpu
        return bc.evaluate(rec, base, 0.10, 0.05, 0.05, False)[2]["method"]

    def test_unpinned_baseline_against_pinned_run_is_flagged(self):
        note = self.method(7, None)
        self.assertIn("pinned CPU differs", note)
        self.assertIn("baseline unpinned", note)

    def test_pinned_baseline_against_unpinned_run_is_flagged(self):
        self.assertIn("now unpinned", self.method(None, 7))

    def test_different_cpus_are_flagged_and_equal_ones_are_not(self):
        self.assertIn("pinned CPU differs", self.method(1, 2))
        self.assertIsNone(self.method(2, 2))
        self.assertIsNone(self.method(None, None))

    def test_unknown_cpu_is_not_a_mismatch(self):
        self.assertIsNone(self.method("absent", 3))
        self.assertIsNone(self.method(3, "absent"))


class PushedRefsTest(unittest.TestCase):
    HEAD = "a" * 40
    OTHER = "b" * 40
    ZERO = "0" * 40

    def test_parse(self):
        text = f"refs/heads/main {self.HEAD} refs/heads/main {self.OTHER}\n\nrefs/heads/x {self.ZERO} refs/heads/x {self.OTHER}\n"
        self.assertEqual(bc.parse_push_refs(text), [("refs/heads/main", self.HEAD, "refs/heads/main", self.OTHER),
                                                    ("refs/heads/x", self.ZERO, "refs/heads/x", self.OTHER)])
        self.assertEqual(bc.parse_push_refs(""), [])

    def test_documentation_only_pushes_are_not_measured(self):
        update = [("refs/heads/main", self.HEAD, "refs/heads/main", self.OTHER)]
        docs = lambda *_: ["README.md", "docs/DESIGN.md", "tools/perf/README.md", "LICENSE"]
        code = lambda *_: ["README.md", "include/x.hpp"]
        self.assertTrue(bc.documentation_only(update, docs))
        self.assertFalse(bc.documentation_only(update, code))
        self.assertFalse(bc.documentation_only(update, lambda *_: None))  # remote commit unknown: measure
        new_branch = [("refs/heads/x", self.HEAD, "refs/heads/x", self.ZERO)]
        self.assertFalse(bc.documentation_only(new_branch, docs))  # nothing to diff against: measure
        self.assertFalse(bc.documentation_only([("refs/heads/x", self.ZERO, "refs/heads/x", self.OTHER)], docs))
        self.assertFalse(bc.is_documentation("docsy/a.cpp"))

    def refs(self, *shas):
        return [(f"refs/heads/b{i}", sha, f"refs/heads/b{i}", self.ZERO) for i, sha in enumerate(shas)]

    def test_head_pushed_from_a_clean_tree_is_fine(self):
        self.assertEqual(bc.push_problems(self.refs(self.HEAD), self.HEAD, True), [])

    def test_dirty_tree_is_refused(self):
        problems = bc.push_problems(self.refs(self.HEAD), self.HEAD, False)
        self.assertEqual(len(problems), 1)
        self.assertIn("uncommitted", problems[0])

    def test_pushing_something_that_is_not_head_is_refused(self):
        problems = bc.push_problems(self.refs(self.OTHER), self.HEAD, True)
        self.assertEqual(len(problems), 1)
        self.assertIn("refs/heads/b0", problems[0])

    def test_one_foreign_ref_among_several_is_refused(self):
        self.assertEqual(len(bc.push_problems(self.refs(self.HEAD, self.OTHER), self.HEAD, True)), 1)

    def test_deletes_are_ignored(self):
        self.assertEqual(bc.push_problems(self.refs(self.ZERO), self.HEAD, False), [])
        self.assertEqual(bc.push_problems(self.refs(self.ZERO, self.HEAD), self.HEAD, True), [])

    def test_tag_objects_resolve_to_their_commit(self):
        tag = "c" * 40
        self.assertEqual(bc.push_problems(self.refs(tag), self.HEAD, True, lambda sha: self.HEAD if sha == tag else sha), [])


def git_run(repo, *args):
    subprocess.run(["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.com", *args], check=True,
                   capture_output=True)


class RepoTestCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name) / "my repo"  # with a space, as users have
        self.repo.mkdir()
        git_run(self.repo, "init", "-q")


class AtomicWriteTest(unittest.TestCase):
    def test_replaces_content_and_leaves_no_temp_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sub" / "f.json"
            bc.atomic_write_text(path, "one\n")
            bc.atomic_write_text(path, "two\n")
            self.assertEqual(path.read_text(encoding="utf-8"), "two\n")
            self.assertEqual([p.name for p in path.parent.iterdir()], ["f.json"])

    def test_failed_write_keeps_the_old_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "f.json"
            bc.atomic_write_text(path, "old\n")
            with self.assertRaises(TypeError):
                bc.atomic_write_text(path, 5)  # not text: the temp file is dropped, the target untouched
            self.assertEqual(path.read_text(encoding="utf-8"), "old\n")
            self.assertEqual([p.name for p in path.parent.iterdir()], ["f.json"])

    def test_baseline_and_samples_use_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            rec = bc.make_record("some/bench.exe", {"a": [1.0]}, [], {"a": [0.5]}, {}, 0, 42)
            bc.write_samples(Path(tmp) / "s.json", rec)
            bc.write_baseline(Path(tmp) / "b" / "base.json", {"format": 2})
            self.assertEqual(json.loads((Path(tmp) / "s.json").read_text(encoding="utf-8"))["seed"], 42)
            self.assertEqual(json.loads((Path(tmp) / "b" / "base.json").read_text(encoding="utf-8")), {"format": 2})


class RecordTest(unittest.TestCase):
    def test_make_record_has_format_exe_and_seed(self):
        rec = bc.make_record("dir/Bench.exe", {"a": [1.0]}, [{"round": 0}], {"a": [0.5]}, None, 3, 7)
        self.assertEqual((rec["format"], rec["exe"], rec["epochs"], rec["seed"], rec["counters"]), (2, "Bench.exe", 3, 7, {}))
        for key in ("cpu", "host", "machine", "system", "rounds", "samples", "positions"):
            self.assertIn(key, rec)

    def test_saved_record_keeps_the_seed(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            git_run(repo, "init", "-q")
            git_run(repo, "commit", "-q", "--allow-empty", "-m", "c")
            rec = bc.make_record("b.exe", {"a": [1.0]}, [], {}, {}, 0, 9)
            path = bc.save_record(repo, "tgt", rec)
            self.assertEqual(json.loads(path.read_text(encoding="utf-8"))["seed"], 9)


class AcceptedSinceTest(RepoTestCase):
    def baseline(self, *accepted):
        return json.dumps({"accepted": [{"benchmarks": {n: {}}} for n in accepted]})

    def test_reads_the_committed_baseline_not_the_working_tree(self):
        rel = "perf/baselines/h.json"
        (self.repo / "perf" / "baselines").mkdir(parents=True)
        (self.repo / rel).write_text(self.baseline(), encoding="utf-8")
        git_run(self.repo, "add", ".")
        git_run(self.repo, "commit", "-q", "-m", "one")
        ref = bc.git_head(self.repo)
        (self.repo / rel).write_text(self.baseline("A"), encoding="utf-8")
        git_run(self.repo, "commit", "-q", "-am", "two")  # A accepted in HEAD
        self.assertEqual(bc.accepted_since(self.repo, rel, ref), {"A"})
        (self.repo / rel).write_text(self.baseline("A", "B"), encoding="utf-8")  # uncommitted acceptance does not count
        self.assertEqual(bc.accepted_since(self.repo, rel, ref), {"A"})

    def test_baseline_missing_at_head_accepts_nothing(self):
        git_run(self.repo, "commit", "-q", "--allow-empty", "-m", "one")
        (self.repo / "b.json").write_text(self.baseline("A"), encoding="utf-8")
        self.assertEqual(bc.accepted_since(self.repo, "b.json", None), set())


class LockTest(RepoTestCase):
    def setUp(self):
        super().setUp()
        git_run(self.repo, "commit", "-q", "--allow-empty", "-m", "one")

    def lock(self, **kw):
        kw.setdefault("sleep", lambda s: None)
        return bc.PerfLock(self.repo, "test", **kw)

    def test_acquire_and_release(self):
        lock = self.lock()
        with lock:
            self.assertTrue(lock.path.is_file())
            self.assertEqual(json.loads(lock.path.read_text(encoding="utf-8"))["pid"], os.getpid())
        self.assertFalse(lock.path.exists())

    def test_released_after_an_exception(self):
        lock = self.lock()
        with self.assertRaises(RuntimeError):
            with lock:
                raise RuntimeError
        self.assertFalse(lock.path.exists())

    def test_live_holder_blocks_until_timeout(self):
        with self.lock():
            ticks = iter(range(0, 10000, 5))
            second = self.lock(timeout=20, clock=lambda: next(ticks))
            with self.assertRaises(bc.LockTimeout):
                second.__enter__()
            self.assertTrue(second.path.exists())  # the holder's lock survived

    def test_waiter_gets_the_lock_when_the_holder_releases(self):
        first = self.lock()
        first.__enter__()
        waits = []

        def sleep(s):
            waits.append(s)
            first.__exit__(None, None, None)
        second = self.lock(sleep=sleep)
        with second:
            self.assertEqual(len(waits), 1)

    def test_dead_holder_is_taken_over(self):
        lock = self.lock(alive=lambda pid: False)
        lock.path.parent.mkdir(parents=True, exist_ok=True)
        lock.path.write_text(json.dumps({"pid": 99999999, "time": 0, "what": "old"}), encoding="utf-8")
        with lock:
            self.assertEqual(json.loads(lock.path.read_text(encoding="utf-8"))["pid"], os.getpid())

    def test_old_lock_of_a_live_pid_is_stale(self):
        lock = self.lock(alive=lambda pid: True, stale_s=100, clock=lambda: 1000.0)
        lock.path.parent.mkdir(parents=True, exist_ok=True)
        lock.path.write_text(json.dumps({"pid": 1, "time": 1.0, "what": "old"}), encoding="utf-8")
        with lock:
            pass

    def test_pid_alive(self):
        self.assertTrue(bc.pid_alive(os.getpid()))
        self.assertFalse(bc.pid_alive(0))
        child = subprocess.Popen([sys.executable, "-c", "pass"])
        child.wait()
        self.assertFalse(bc.pid_alive(child.pid))


@unittest.skipUnless(shutil.which("sh"), "needs a POSIX sh")
class HooksTest(RepoTestCase):
    def install(self):
        args = type("A", (), {"repo": str(self.repo), "build_dir": str(self.repo / "build dir"), "target": "tgt",
                              "exe": str(self.repo / "build dir" / "tgt"), "baseline": str(self.repo / "b.json"), "config": ""})
        bc.install_hooks_command(args)
        return Path(bc.git(self.repo, "rev-parse", "--git-path", "hooks"))

    def hooks_dir(self):
        hooks = Path(bc.git(self.repo, "rev-parse", "--git-path", "hooks"))
        return hooks if hooks.is_absolute() else self.repo / hooks

    def run_hook(self, name, *args, stdin=""):
        return subprocess.run(["sh", str(self.hooks_dir() / name), *args], input=stdin, capture_output=True, text=True, cwd=self.repo)

    def test_hook_text_quotes_paths_with_spaces(self):
        body = bc.hook_body("pre-push", "C:/Program Files/Python/python.exe", "C:/my tools/bench_check.py", "x")
        self.assertIn("python='C:/Program Files/Python/python.exe'", body)
        self.assertIn("script='C:/my tools/bench_check.py'", body)

    def test_missing_tooling_makes_the_hook_a_no_op_with_a_message(self):
        hooks = self.hooks_dir()
        hooks.mkdir(parents=True, exist_ok=True)
        (hooks / "pre-push").write_text(bc.hook_body("pre-push", "/no/such/python", "/no/such/script.py", "x"), encoding="utf-8", newline="\n")
        result = self.run_hook("pre-push", "origin", "url", stdin="refs/heads/a " + "1" * 40 + " refs/heads/a " + "0" * 40 + "\n")
        self.assertEqual(result.returncode, 0)
        self.assertIn("missing", result.stdout)

    def test_install_creates_hooks_and_is_idempotent(self):
        self.install()
        first = (self.hooks_dir() / "pre-push").read_text(encoding="utf-8")
        self.install()
        self.assertEqual((self.hooks_dir() / "pre-push").read_text(encoding="utf-8"), first)
        self.assertIn(bc.HOOK_MARK, first)
        self.assertIn("--refs-from-stdin", first)
        self.assertTrue((self.hooks_dir() / "post-checkout").is_file())

    def test_reinstall_leaves_an_unchanged_running_hook_alone(self):
        self.install()
        original = bc.atomic_write_text

        def refuse_hooks(path, text):
            if Path(path).parent == self.hooks_dir():
                raise PermissionError("in use")  # what Windows does to a running hook
            original(path, text)

        bc.atomic_write_text = refuse_hooks
        try:
            self.install()  # unchanged hooks are skipped, so nothing is replaced
            (self.hooks_dir() / "pre-push").write_text("# stale ours " + bc.HOOK_MARK + "\n", encoding="utf-8")
            self.install()  # a changed one that cannot be replaced is reported, not raised
        finally:
            bc.atomic_write_text = original

    def test_foreign_hook_is_kept_and_chained_with_stdin_and_arguments(self):
        hooks = self.hooks_dir()
        hooks.mkdir(parents=True, exist_ok=True)
        out = self.repo / "seen.txt"
        (hooks / "pre-push").write_text(f'#!/bin/sh\necho "args=$@" > "{out.as_posix()}"\ncat >> "{out.as_posix()}"\n',
                                         encoding="utf-8", newline="\n")
        (hooks / "pre-push").chmod(0o755)  # a real hook is executable; git and the chain run it directly
        self.install()
        self.assertTrue((hooks / "pre-push.user").is_file())
        self.assertIn(bc.HOOK_MARK, (hooks / "pre-push").read_text(encoding="utf-8"))
        # The user's hook runs first with the arguments and stdin; ours then refuses (the pushed sha is not HEAD).
        result = self.run_hook("pre-push", "origin", "url", stdin="refs/heads/a " + "1" * 40 + " refs/heads/a " + "0" * 40 + "\n")
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("push refused", result.stdout)
        seen = out.read_text(encoding="utf-8")
        self.assertIn("args=origin url", seen)
        self.assertIn("refs/heads/a", seen)

    def test_failing_user_hook_stops_the_push(self):
        hooks = self.hooks_dir()
        hooks.mkdir(parents=True, exist_ok=True)
        (hooks / "pre-push").write_text("#!/bin/sh\nexit 3\n", encoding="utf-8", newline="\n")
        (hooks / "pre-push").chmod(0o755)
        self.install()
        self.assertEqual(self.run_hook("pre-push", "origin", "url").returncode, 3)


class GateSkipTest(RepoTestCase):
    def test_skip_bypasses_and_is_logged(self):
        git_run(self.repo, "commit", "-q", "--allow-empty", "-m", "one")
        args = type("A", (), {"repo": str(self.repo)})
        os.environ["SUB0MATH_PERF_SKIP"] = "emergency fix"
        try:
            self.assertEqual(bc.gate_command(args), 0)
        finally:
            del os.environ["SUB0MATH_PERF_SKIP"]
        log = (bc.perf_dir(self.repo) / "log.jsonl").read_text(encoding="utf-8")
        self.assertIn("emergency fix", log)
        self.assertIn('"skip"', log)


class GateRefusalTest(RepoTestCase):
    """The gate's push checks run before any build: a configured repository with no targets shows them."""

    def setUp(self):
        super().setUp()
        (self.repo / "f.txt").write_text("one", encoding="utf-8")
        git_run(self.repo, "add", ".")
        git_run(self.repo, "commit", "-q", "-m", "one")
        self.head = bc.git_head(self.repo)
        bc.atomic_write_text(bc.perf_dir(self.repo) / "config.json", json.dumps({"targets": {}}))

    def gate(self, stdin_text):
        import io
        args = type("A", (), {"repo": str(self.repo), "refs_from_stdin": True, "lock_timeout": 1.0})
        real = sys.stdin
        sys.stdin = io.StringIO(stdin_text)
        try:
            return bc.gate_command(args)
        finally:
            sys.stdin = real

    def push(self, sha):
        return f"refs/heads/main {sha} refs/heads/main {'0' * 40}\n"

    def test_clean_pushed_head_proceeds(self):
        self.assertEqual(self.gate(self.push(self.head)), 0)

    def test_dirty_tree_is_refused_and_logged(self):
        (self.repo / "f.txt").write_text("two", encoding="utf-8")
        self.assertEqual(self.gate(self.push(self.head)), 1)
        self.assertIn("gate-refused", (bc.perf_dir(self.repo) / "log.jsonl").read_text(encoding="utf-8"))

    def test_untracked_files_do_not_refuse(self):
        (self.repo / "new.txt").write_text("x", encoding="utf-8")
        self.assertEqual(self.gate(self.push(self.head)), 0)

    def test_pushing_another_commit_is_refused(self):
        git_run(self.repo, "commit", "-q", "--allow-empty", "-m", "two")
        self.assertEqual(self.gate(self.push(self.head)), 1)  # HEAD moved on; the pushed sha is the older commit

    def test_deleting_a_branch_proceeds_even_when_dirty(self):
        (self.repo / "f.txt").write_text("two", encoding="utf-8")
        self.assertEqual(self.gate(self.push("0" * 40)), 0)


if __name__ == "__main__":
    unittest.main()
