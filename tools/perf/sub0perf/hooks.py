"""The git hooks: installing the pre-push gate and the post-checkout note, and chaining hooks that were already there."""

import json
import os
import re
import shlex
import sys
from pathlib import Path

from . import levels
from .common import atomic_write_text, read_json, tooling_script
from .gitstate import git, git_head, log_event, perf_dir
from .pins import parse_pin


HOOK_MARK = "# sub0perf hook"
PREVIOUS_SUFFIX = ".user"  # a foreign hook that was in the way is renamed to <hook>.user and run first
MODE_VAR = "SUB0MATH_PERF_HOOK_MODE"  # "chain": never write into git's hooks directory; keep ours in .git/sub0perf/hooks and print the line
CHAIN_DIR = "hooks"  # under .git/sub0perf
HOOK_COMMENTS = {"pre-push": "performance regression gate (tools/perf/bench_check.py gate)",
                 "post-checkout": "notes the checkout; the first clean build records its measurement"}


def hook_body(name, python, script, comment):
    """The sh text of a hook. Paths are shell-quoted (spaces); it does nothing, with a message, when python or the tooling is gone."""
    lines = ["#!/bin/sh", f"{HOOK_MARK}: {comment}",
             f"python={shlex.quote(python)}", f"script={shlex.quote(script)}",
             'previous="$(dirname "$0")/' + name + PREVIOUS_SUFFIX + '"']
    guard = ['if [ ! -f "$python" ] || [ ! -f "$script" ]; then',
             '    echo "sub0perf: python ($python) or the tooling ($script) is missing; ' + name + ' hook skipped"',
             '    exit 0',
             'fi']
    if name == "pre-push":  # stdin carries the refs being pushed; the previous hook and ours both need it
        lines += ['refs="$(cat)"',
                  'if [ -f "$previous" ]; then',
                  '    printf \'%s\\n\' "$refs" | "$previous" "$@" || exit $?',
                  'fi', *guard,
                  'printf \'%s\\n\' "$refs" | "$python" "$script" gate --repo "$(git rev-parse --show-toplevel)" --refs-from-stdin']
    else:
        lines += ['if [ -f "$previous" ]; then',
                  '    "$previous" "$@" || exit $?',
                  'fi', *guard,
                  'if [ "$3" = "1" ]; then',
                  '    "$python" "$script" note-checkout --repo "$(git rev-parse --show-toplevel)"',
                  'fi',
                  'exit 0']
    return "\n".join(lines) + "\n"


def hooks_dir(repo):
    """Where git looks for hooks in this repository (honours core.hooksPath), as an absolute path."""
    found = git(repo, "rev-parse", "--git-path", "hooks")
    path = Path(found or ".git/hooks")
    return path if path.is_absolute() else Path(repo) / path


def is_tracked_dir(repo):
    """True when core.hooksPath puts the hooks inside the worktree (for example `.githooks`): its files are committed,
    shared with everyone, and not ours to rename or overwrite."""
    if not git(repo, "config", "--get", "core.hooksPath"):
        return False
    top = Path(git(repo, "rev-parse", "--show-toplevel") or repo).resolve()
    where = hooks_dir(repo).resolve()
    git_dir = Path(git(repo, "rev-parse", "--absolute-git-dir") or top / ".git").resolve()
    inside = lambda p, root: p == root or root in p.parents
    return inside(where, top) and not inside(where, git_dir)


def chain_path(repo, name):
    """Where the chained copy of hook `name` lives: inside .git/sub0perf, never tracked."""
    d = perf_dir(Path(repo))
    return d / CHAIN_DIR / name


def chain_line(name):
    """The one line to add to a committed (or hand-written) hook so it runs ours. It must come before anything reads stdin."""
    target = f'"$(git rev-parse --path-format=absolute --git-common-dir)/sub0perf/{CHAIN_DIR}/{name}"'
    return f'[ ! -f {target} ] || sh {target} "$@" || exit $?'


GATE_CALL_RE = re.compile(r'bench_check\.py"?\s+gate\b')
TOOLING_RECORD = "sub0math_tooling_dir.txt"  # written by the configure step into the build directory: the tooling's location
TOOLING_ENV = "SUB0PERF_TOOLING_DIR"  # overrides the record


def direct_gate_lines(text):
    """The active (not commented out) lines of a hand-written hook that run `bench_check.py gate` themselves: the shared pre-push
    template's perf stanza. That hook reads the pushed refs from stdin itself, so it is wired without the chain line."""
    return [line.strip() for line in text.splitlines()
            if line.strip() and not line.lstrip().startswith("#") and GATE_CALL_RE.search(line)]


def tooling_dirs(repo, environ=None):
    """The tooling directories the template's stanza would use: SUB0PERF_TOOLING_DIR, then the newest record under `build*/` and
    `build/*/` (a preset's build directory is build/<preset>)."""
    environ = os.environ if environ is None else environ
    found = [Path(environ[TOOLING_ENV].strip())] if environ.get(TOOLING_ENV, "").strip() else []
    records = {p for pattern in (f"build*/{TOOLING_RECORD}", f"build/*/{TOOLING_RECORD}") for p in Path(repo).glob(pattern)}
    for record in sorted(records, key=lambda p: p.stat().st_mtime, reverse=True):
        try:
            found.append(Path(record.read_text(encoding="utf-8").strip()))
        except OSError:
            continue
    return found


def chained_in(text, name):
    """True when a hook's text already calls the chained copy."""
    return f"sub0perf/{CHAIN_DIR}/{name}" in text


def install_chained(path, body):
    """Writes the chained copy (unchanged is left alone: Windows refuses to replace a running hook)."""
    if path.is_file() and path.read_text(encoding="utf-8", errors="replace") == body:
        return
    try:
        atomic_write_text(path, body)
        path.chmod(0o755)
    except PermissionError as error:
        print(f"sub0perf: could not update {path} ({error}); it is probably running, and the next configure retries")
    except OSError:
        pass


def install_hooks_command(args):
    repo = Path(args.repo)
    d = perf_dir(repo)
    if d is None:
        print(f"sub0perf: {repo} is not a git repository; hooks not installed")
        return 0
    d.mkdir(parents=True, exist_ok=True)
    config_path = d / "config.json"
    config = read_json(config_path) if config_path.is_file() else {"targets": {}}
    config["targets"][args.target] = {"build_dir": str(Path(args.build_dir).resolve()), "exe": str(Path(args.exe).resolve()),
                                      "baseline": str(Path(args.baseline).resolve()), "config": args.config or ""}
    if getattr(args, "full_refs", None):  # tag-name patterns whose push runs the full level; "" restores the defaults
        patterns = [p for text in args.full_refs for p in levels.parse_patterns(text)]
        if patterns:
            config["full_refs"] = patterns
        else:
            config.pop("full_refs", None)
    if getattr(args, "quick_budget", None) not in (None, ""):
        config["quick_budget"] = float(args.quick_budget)
    if getattr(args, "pin", None):
        try:
            new_pins = [parse_pin(text) for text in args.pin]
        except ValueError as error:
            print(f"sub0perf: {error}")
            return 2
        keep = [p for p in config.get("pins", []) if (p["file"], p["var"]) not in {(n["file"], n["var"]) for n in new_pins}]
        config["pins"] = keep + new_pins
    atomic_write_text(config_path, json.dumps(config, indent=2) + "\n")
    hooks = hooks_dir(repo)
    python = Path(sys.executable).as_posix()
    script = tooling_script().as_posix()
    mode = os.environ.get(MODE_VAR, "").strip().lower()
    if mode not in ("", "install", "chain"):
        print(f"sub0perf: {MODE_VAR}={mode} is not install or chain; using install")
        mode = ""
    if mode == "chain" or is_tracked_dir(repo):
        # Never rename or overwrite what is in git's hooks directory: with a tracked directory (core.hooksPath inside the
        # worktree) those files are committed and shared. Ours go to .git/sub0perf/hooks, called by one line in the hook.
        why = f"{MODE_VAR}=chain" if mode == "chain" else f"{hooks} is tracked (core.hooksPath is inside the worktree)"
        for name, comment in HOOK_COMMENTS.items():
            install_chained(chain_path(repo, name), hook_body(name, python, script, comment))
            existing = hooks / name
            if existing.is_file() and chained_in(existing.read_text(encoding="utf-8", errors="replace"), name):
                continue  # the committed hook already runs ours
            if name == "pre-push" and existing.is_file() and direct_gate_lines(existing.read_text(encoding="utf-8", errors="replace")):
                continue  # the committed hook runs the gate itself (the shared template's stanza): that is a wiring too
            print(f"sub0perf: {why}; left {name} alone. Add this line to {existing} (before anything reads stdin; not for a hook that "
                  f"reads the refs on stdin itself, such as the shared pre-push template: that one runs `bench_check.py gate "
                  f"--refs-from-stdin` itself, see its perf stanza):\n  {chain_line(name)}")
        return 0
    hooks.mkdir(parents=True, exist_ok=True)
    for name, comment in HOOK_COMMENTS.items():
        path = hooks / name
        if path.is_file() and HOOK_MARK not in path.read_text(encoding="utf-8", errors="replace"):
            previous = hooks / (name + PREVIOUS_SUFFIX)
            if previous.exists():
                print(f"sub0perf: {path} exists and is not ours, and {previous.name} is taken; chain to the gate yourself:\n"
                      f'  "{python}" "{script}" gate --repo "$(git rev-parse --show-toplevel)" --refs-from-stdin')
                continue
            os.replace(path, previous)
            print(f"sub0perf: kept your {name} hook as {previous.name}; the installed hook runs it first")
        body = hook_body(name, python, script, comment)
        if path.is_file() and path.read_text(encoding="utf-8", errors="replace") == body:
            continue  # unchanged; Windows refuses to replace a hook that is running (a gate's own build)
        try:
            atomic_write_text(path, body)
        except PermissionError as error:
            print(f"sub0perf: could not update {path} ({error}); it is probably running, and the next configure retries")
            continue
        try:
            path.chmod(0o755)
        except OSError:
            pass
    return 0


def note_checkout_command(args):
    repo = Path(args.repo or ".")
    head = git_head(repo)
    log_event(repo, {"event": "checkout", "commit": head})
    d = perf_dir(repo)
    have = d and any((d / "records").glob(f"*/{head}.json")) if d else False
    if not have:
        print(f"sub0perf: checked out {head[:9]}; its benchmark record is made by the first clean build")
    return 0
