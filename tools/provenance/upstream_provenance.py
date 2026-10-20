#!/usr/bin/env python3
"""Records and checks which upstream commit ported code came from.

The manifest (upstream/<name>.json) names the upstream repository, the exact commit that was ported,
and for every ported source file its git blob hash at that commit and the files here that carry it.

  check   the manifest is self-consistent: blobs match the commit, targets exist here.
  drift   what changed upstream since the commit: ported files changed, and other new or changed
          files under the watched paths. Writes a Markdown report; never fails on drift.
  record  re-pin the manifest to a new commit after porting its changes (rewrites blob hashes).

Ownership. One upstream file can be ported into several repositories that each record it. `check`
always covers every ported file, but `drift` should raise one issue, not one per repository. A manifest
and each `ported` entry carry an optional `driftOwner` (default true). An entry's own value wins; an
entry without one inherits the manifest's. `drift` reports only owned entries; the "other new or changed
files" section is owned by the manifest-level value. `drift=` is true only if something owned drifted.
An entry may name the repository that owns it in `ownedBy` (documentation only).

All paths in the manifest (targets) are relative to --repo-root (default: the current directory), which
is the repository being checked, not the one holding this script.

Standard library only. Needs `git` and a full-history clone of the upstream repository.
"""

import argparse
import fnmatch
import json
import os
import subprocess
import sys
from pathlib import Path



def git(upstream, *args):
    result = subprocess.run(["git", "-C", str(upstream), *args], capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def blob(upstream, commit, path):
    result = subprocess.run(["git", "-C", str(upstream), "rev-parse", f"{commit}:{path}"],
                            capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def load(manifest_path):
    with open(manifest_path, encoding="utf-8") as f:
        return json.load(f)


def owns(manifest, entry):
    """Whether this repository raises drift for the entry (entry value, else manifest value, else true)."""
    return bool(entry.get("driftOwner", manifest.get("driftOwner", True)))


def check(manifest, upstream, repo_root):
    errors = []
    commit = manifest["commit"]
    if len(commit) != 40:
        errors.append("commit must be a full 40-character SHA")
    if git(upstream, "cat-file", "-t", commit) != "commit":
        errors.append(f"{commit} is not a commit in {upstream}")
    for entry in manifest["ported"]:
        actual = blob(upstream, commit, entry["source"])
        if actual != entry["blob"]:
            errors.append(f"{entry['source']}: manifest blob {entry['blob']} but {commit[:7]} has {actual}")
        for target in entry["targets"]:
            if not (Path(repo_root) / target).is_file():
                errors.append(f"{entry['source']}: target {target} does not exist")
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    if not errors:
        print(f"ok: {len(manifest['ported'])} ported files match {manifest['name']} {commit[:7]}")
    return 1 if errors else 0


def drift(manifest, upstream, ref, report_path):
    commit = manifest["commit"]
    head = git(upstream, "rev-parse", ref)
    ported = {entry["source"]: entry for entry in manifest["ported"]}
    owned = {source: entry for source, entry in ported.items() if owns(manifest, entry)}
    lines = [f"# {manifest['name']} drift since the ported commit", "",
             f"Ported: [`{commit[:7]}`]({manifest['repository']}/commit/{commit}) · "
             f"compared with `{ref}` at [`{head[:7]}`]({manifest['repository']}/commit/{head}) · "
             f"[full diff]({manifest['repository']}/compare/{commit}...{head})", ""]

    changed_ported = []
    for source, entry in owned.items():
        now = blob(upstream, head, source)
        if now != entry["blob"]:
            log = git(upstream, "log", "--format=%h %s", f"{commit}..{head}", "--", source)
            changed_ported.append((source, entry, now, log))

    watched = git(upstream, "diff", "--name-status", f"{commit}..{head}", "--", *manifest.get("watch", []))
    ignore = manifest.get("ignore", [])
    unported = [line for line in watched.splitlines()
                if manifest.get("driftOwner", True)
                and line.split("\t")[-1] not in ported
                and not any(fnmatch.fnmatch(line.split("\t")[-1], pattern) for pattern in ignore)]

    if changed_ported:
        lines += ["## Ported files changed upstream", "",
                  "Re-port these, then run `upstream_provenance.py record`.", ""]
        for source, entry, now, log in changed_ported:
            state = "deleted" if now is None else "changed"
            lines.append(f"### `{source}` ({state})")
            lines.append("")
            lines.append("Carried here by: " + ", ".join(f"`{t}`" for t in entry["targets"]))
            lines.append("")
            lines += [f"- {l}" for l in log.splitlines()] or ["- (no commits found)"]
            lines.append("")
    if unported:
        lines += ["## Other new or changed upstream files (not ported)", "",
                  "Decide whether each belongs here.", ""]
        lines += [f"- `{l.split(chr(9))[0]}` `{l.split(chr(9))[-1]}`" for l in unported]
        lines.append("")
    has_drift = bool(changed_ported or unported)
    if not has_drift:
        lines.append("No drift: every ported file and watched path is unchanged.")

    Path(report_path).write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"drift={'true' if has_drift else 'false'}")
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as f:
            f.write(f"drift={'true' if has_drift else 'false'}\n")
    return 0


def record(manifest, manifest_path, upstream, commit):
    full = git(upstream, "rev-parse", f"{commit}^{{commit}}")
    for entry in manifest["ported"]:
        new_blob = blob(upstream, full, entry["source"])
        if new_blob is None:
            raise SystemExit(f"{entry['source']} does not exist at {full}; remove or update that entry first")
        entry["blob"] = new_blob
    manifest["commit"] = full
    with open(manifest_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"recorded {manifest['name']} {full}")
    return 0


def main():
    # Defaults are resolved against the repository being checked (the current directory).
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["check", "drift", "record"])
    parser.add_argument("--manifest", default="upstream/bikelab.json", help="path relative to --repo-root")
    parser.add_argument("--repo-root", default=".", help="the repository being checked (default: current directory)")
    parser.add_argument("--upstream-dir", required=True, help="full-history clone of the upstream repository")
    parser.add_argument("--ref", default="origin/main", help="drift: upstream ref to compare with")
    parser.add_argument("--report", default="upstream-drift.md", help="drift: Markdown report path")
    parser.add_argument("--commit", help="record: upstream commit to pin")
    args = parser.parse_args()

    manifest_path = Path(args.repo_root) / args.manifest
    manifest = load(manifest_path)
    if args.command == "check":
        return check(manifest, args.upstream_dir, args.repo_root)
    if args.command == "drift":
        return drift(manifest, args.upstream_dir, args.ref, args.report)
    if not args.commit:
        parser.error("record needs --commit")
    return record(manifest, manifest_path, args.upstream_dir, args.commit)


if __name__ == "__main__":
    sys.exit(main())
