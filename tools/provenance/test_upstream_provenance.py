"""Self-test for upstream_provenance.py: temporary git repositories, standard library only."""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import upstream_provenance as up  # noqa: E402


def run(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", *args],
                   check=True, capture_output=True)


class Fixture:
    """An upstream repository with two ported files and a watched directory, and a consumer directory."""

    def __init__(self, test):
        tmp = tempfile.TemporaryDirectory()
        test.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.upstream = self.root / "up"
        self.repo = self.root / "repo"
        (self.upstream / "dist").mkdir(parents=True)
        self.repo.mkdir()
        run(self.upstream, "init", "-q", "-b", "main")
        self.write_upstream("dist/a.js", "a1")
        self.write_upstream("dist/b.js", "b1")
        self.commit1 = self.commit("one")
        (self.repo / "a.hpp").write_text("x")
        (self.repo / "upstream").mkdir()
        self.manifest_path = self.repo / "upstream" / "m.json"
        self.manifest = {
            "name": "Up", "repository": "https://example.com/up", "commit": self.commit1, "watch": ["dist/"],
            "ignore": ["*.css"],
            "ported": [
                {"source": "dist/a.js", "blob": up.blob(self.upstream, self.commit1, "dist/a.js"), "targets": ["a.hpp"]},
                {"source": "dist/b.js", "blob": up.blob(self.upstream, self.commit1, "dist/b.js"), "targets": ["a.hpp"]},
            ]}
        self.save()

    def write_upstream(self, path, text):
        (self.upstream / path).write_text(text)

    def commit(self, message):
        run(self.upstream, "add", "-A")
        run(self.upstream, "commit", "-q", "-m", message)
        return up.git(self.upstream, "rev-parse", "HEAD")

    def save(self):
        self.manifest_path.write_text(json.dumps(self.manifest))

    def drift(self):
        report = self.root / "report.md"
        up.drift(self.manifest, self.upstream, "HEAD", report)
        return report.read_text(encoding="utf-8")


class CheckTests(unittest.TestCase):
    def test_consistent_manifest_passes(self):
        f = Fixture(self)
        self.assertEqual(up.check(f.manifest, f.upstream, f.repo), 0)

    def test_wrong_blob_fails(self):
        f = Fixture(self)
        f.manifest["ported"][0]["blob"] = "0" * 40
        self.assertEqual(up.check(f.manifest, f.upstream, f.repo), 1)

    def test_missing_target_fails(self):
        f = Fixture(self)
        f.manifest["ported"][0]["targets"] = ["gone.hpp"]
        self.assertEqual(up.check(f.manifest, f.upstream, f.repo), 1)

    def test_targets_resolve_against_repo_root_not_the_script(self):
        f = Fixture(self)
        self.assertEqual(up.check(f.manifest, f.upstream, f.root), 1)

    def test_short_commit_fails(self):
        f = Fixture(self)
        f.manifest["commit"] = f.commit1[:10]
        self.assertEqual(up.check(f.manifest, f.upstream, f.repo), 1)

    def test_non_owner_still_checked(self):
        f = Fixture(self)
        f.manifest["driftOwner"] = False
        f.manifest["ported"][0]["blob"] = "0" * 40
        self.assertEqual(up.check(f.manifest, f.upstream, f.repo), 1)


class DriftTests(unittest.TestCase):
    def test_no_drift(self):
        f = Fixture(self)
        self.assertIn("No drift", f.drift())

    def test_ported_change_and_new_file_reported_ignore_respected(self):
        f = Fixture(self)
        f.write_upstream("dist/a.js", "a2")
        f.write_upstream("dist/c.js", "new")
        f.write_upstream("dist/style.css", "ignored")
        f.commit("two")
        report = f.drift()
        self.assertIn("### `dist/a.js` (changed)", report)
        self.assertNotIn("dist/b.js", report)
        self.assertIn("`dist/c.js`", report)
        self.assertNotIn("style.css", report)

    def test_deleted_ported_file(self):
        f = Fixture(self)
        (f.upstream / "dist" / "b.js").unlink()
        f.commit("rm")
        self.assertIn("### `dist/b.js` (deleted)", f.drift())

    def test_entry_owner_overrides_manifest(self):
        f = Fixture(self)
        f.manifest["driftOwner"] = False
        f.manifest["ported"][0]["driftOwner"] = True
        f.write_upstream("dist/a.js", "a2")
        f.write_upstream("dist/b.js", "b2")
        f.write_upstream("dist/c.js", "new")
        f.commit("two")
        report = f.drift()
        self.assertIn("dist/a.js", report)
        self.assertNotIn("dist/b.js", report)   # inherits the manifest's false
        self.assertNotIn("dist/c.js", report)   # unported files follow the manifest value

    def test_non_owner_reports_no_drift(self):
        f = Fixture(self)
        f.manifest["driftOwner"] = False
        f.write_upstream("dist/a.js", "a2")
        f.commit("two")
        self.assertIn("No drift", f.drift())

    def test_owner_entry_false_under_owner_manifest(self):
        f = Fixture(self)
        f.manifest["ported"][0]["driftOwner"] = False
        f.write_upstream("dist/a.js", "a2")
        f.write_upstream("dist/c.js", "new")
        f.commit("two")
        report = f.drift()
        self.assertNotIn("### `dist/a.js`", report)
        self.assertIn("dist/c.js", report)

    def test_github_output_flag(self):
        f = Fixture(self)
        out = f.root / "gh_output"
        os.environ["GITHUB_OUTPUT"] = str(out)
        self.addCleanup(os.environ.pop, "GITHUB_OUTPUT", None)
        f.write_upstream("dist/a.js", "a2")
        f.commit("two")
        f.drift()
        self.assertEqual(out.read_text(), "drift=true\n")


class RecordTests(unittest.TestCase):
    def test_record_repins_blobs_and_commit(self):
        f = Fixture(self)
        f.write_upstream("dist/a.js", "a2")
        new = f.commit("two")
        up.record(f.manifest, f.manifest_path, f.upstream, new)
        saved = json.loads(f.manifest_path.read_text())
        self.assertEqual(saved["commit"], new)
        self.assertEqual(saved["ported"][0]["blob"], up.blob(f.upstream, new, "dist/a.js"))
        self.assertEqual(up.check(saved, f.upstream, f.repo), 0)

    def test_record_refuses_missing_source(self):
        f = Fixture(self)
        (f.upstream / "dist" / "b.js").unlink()
        new = f.commit("rm")
        with self.assertRaises(SystemExit):
            up.record(f.manifest, f.manifest_path, f.upstream, new)

    def test_record_keeps_ownership_fields(self):
        f = Fixture(self)
        f.manifest["ported"][0]["driftOwner"] = False
        f.manifest["ported"][0]["ownedBy"] = "other"
        up.record(f.manifest, f.manifest_path, f.upstream, f.commit1)
        saved = json.loads(f.manifest_path.read_text())["ported"][0]
        self.assertEqual((saved["driftOwner"], saved["ownedBy"]), (False, "other"))


class CommandLineTests(unittest.TestCase):
    def test_check_from_consumer_directory(self):
        f = Fixture(self)
        script = Path(up.__file__)
        result = subprocess.run([sys.executable, str(script), "check", "--manifest", "upstream/m.json",
                                 "--upstream-dir", str(f.upstream)], cwd=f.repo, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ok: 2 ported files", result.stdout)


if __name__ == "__main__":
    unittest.main()
