import tempfile
import unittest
from pathlib import Path

import wasm_gate


class FindEmsdkTest(unittest.TestCase):
    def make_sdk(self, root: Path) -> Path:
        sdk = root / "emsdk"
        sdk.mkdir()
        (sdk / "emsdk_env.sh").write_text("")
        return sdk

    def test_sibling_checkout_is_found(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            sdk = self.make_sdk(root)
            repo = root / "Repo"
            repo.mkdir()
            self.assertEqual(wasm_gate.find_emsdk(None, repo, {}), sdk)

    def test_environment_variable_wins_over_sibling(self):
        with tempfile.TemporaryDirectory() as tmp, tempfile.TemporaryDirectory() as other:
            root = Path(tmp)
            self.make_sdk(root)
            elsewhere = self.make_sdk(Path(other))
            repo = root / "Repo"
            repo.mkdir()
            self.assertEqual(wasm_gate.find_emsdk(None, repo, {"EMSDK_DIR": str(elsewhere)}), elsewhere)

    def test_absent_sdk_is_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "Repo"
            repo.mkdir()
            self.assertIsNone(wasm_gate.find_emsdk(None, repo, {}))

    def test_absent_sdk_fails_by_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "Repo"
            repo.mkdir()
            self.assertEqual(wasm_gate.main(["--repo", str(repo), "--prefix", "X", "--emsdk", str(Path(tmp) / "none")]),
                             wasm_gate.MISSING_SDK_EXIT)

    def test_absent_sdk_skips_with_exit_zero_when_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp) / "Repo"
            repo.mkdir()
            self.assertEqual(wasm_gate.main(["--repo", str(repo), "--prefix", "X", "--emsdk", str(Path(tmp) / "none"),
                                             "--allow-missing-sdk"]), 0)


class EnvironmentTest(unittest.TestCase):
    def test_parse_windows_set_output(self):
        env = wasm_gate.parse_environment("A=1\nPath=C:\\x;D:\\y\n=C:=C:\\\n", "\n")
        self.assertEqual(env, {"A": "1", "Path": "C:\\x;D:\\y"})
        self.assertEqual(wasm_gate.env_get(env, "PATH"), "C:\\x;D:\\y")

    def test_parse_posix_env_output(self):
        self.assertEqual(wasm_gate.parse_environment("A=1\0B=x=y\0", "\0"), {"A": "1", "B": "x=y"})


class PerfConfigGuardTest(unittest.TestCase):
    def test_restores_a_rewritten_config_and_leaves_an_untouched_one(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            config = repo / ".git" / "sub0perf" / "config.json"
            config.parent.mkdir(parents=True)
            config.write_text("original")
            guard = wasm_gate.PerfConfigGuard(repo)
            self.assertFalse(guard.restore())
            config.write_text("rewritten")
            self.assertTrue(guard.restore())
            self.assertEqual(config.read_text(), "original")


class ConfigureTest(unittest.TestCase):
    def test_turns_the_perf_tooling_off_and_keeps_contraction_alone(self):
        arguments = wasm_gate.configure_arguments(Path("r"), "build-wasm", "SUB0PHYSICS")
        self.assertIn("-DSUB0MATH_PERF_GIT_HOOKS=OFF", arguments)
        self.assertIn("-DSUB0MATH_PERF_ON_BUILD=OFF", arguments)
        self.assertIn("-DSUB0PHYSICS_BUILD_BENCHMARKS=OFF", arguments)
        self.assertFalse([a for a in arguments if "contract" in a or "O3" in a or "Ofast" in a])


if __name__ == "__main__":
    unittest.main()
