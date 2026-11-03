"""`selftest`: proves a repository's perf gate is wired the way it should be, in seconds, without building or measuring.

Each check is a row (status, check, detail); PASS, WARN (worth knowing, not wrong) or FAIL (the gate would misbehave).

- hook: the pre-push hook is installed where git looks for it, calls an existing bench_check.py, and is what
  `install-hooks` writes for this tooling (drift is reported); the post-checkout hook likewise.
- config: `.git/sub0perf/config.json` is valid; each target's build dir, executable and baseline exist (a build that is
  not there yet is a warning), pins and settings parse.
- decisions: the real `gate_command` is driven with synthetic pushes in its plan-only mode, which stops before the pins'
  network check, the build and any measuring: empty stdin, a deletion and a documentation-only range must skip; a code
  range must plan a quick run whose predicted seconds plus the probe allowance fit the budget, from the repository's real
  coverage state. The unknown and slow benchmarks and the stalest group are reported.
- lock: no stale `.git/sub0perf/lock`.
"""

import contextlib
import io
import json
import os
import re
import sys
import types
from pathlib import Path

from . import gate, hooks, levels, plan as planning
from .common import read_json, tooling_script
from .gitstate import git, git_head, perf_dir
from .lock import PerfLock
from .pins import PIN_KEYS, pinned_sha

PASS, WARN, FAIL = "PASS", "WARN", "FAIL"
ZERO = "0" * 40


def hooks_dir(repo):
    """Where git looks for hooks in this repository (honours core.hooksPath), as an absolute path."""
    found = git(repo, "rev-parse", "--git-path", "hooks")
    path = Path(found or ".git/hooks")
    return path if path.is_absolute() else Path(repo) / path


def _path_lines(text):
    """The text of a hook with its python= and script= lines blanked: what remains is the hook's logic."""
    return re.sub(r"^(python|script)=.*$", r"\1=<path>", text, flags=re.MULTILINE)


def _assigned(text, name):
    m = re.search(rf"^{name}=(.*)$", text, flags=re.MULTILINE)
    if not m:
        return None
    parts = m.group(1).strip()
    if len(parts) >= 2 and parts[0] == parts[-1] and parts[0] in "'\"":
        parts = parts[1:-1]
    return parts


def check_hook(repo, name, required):
    """Rows for one installed hook."""
    d = hooks_dir(repo)
    path = d / name
    label = f"hook {name}"
    if not path.is_file():
        return [(FAIL if required else WARN, label, f"not installed in {d} (git looks there; configure the build or run install-hooks)")]
    text = path.read_text(encoding="utf-8", errors="replace")
    if hooks.HOOK_MARK not in text:
        return [(FAIL, label, f"{path} is not a sub0perf hook, so the gate does not run")]
    rows = []
    if os.name == "posix" and not os.access(path, os.X_OK):
        rows.append((FAIL, label, f"{path} is not executable, so git ignores it"))
    script, python = _assigned(text, "script"), _assigned(text, "python")
    if not script or Path(script).name != "bench_check.py" or not Path(script).is_file():
        rows.append((FAIL, label, f"calls {script or '(no script= line)'}, which is not an existing bench_check.py"))
    if not python or not Path(python).is_file():
        rows.append((FAIL, label, f"calls python {python or '(no python= line)'}, which does not exist"))
    expected = hooks.hook_body(name, Path(sys.executable).as_posix(), tooling_script().as_posix(), hooks.HOOK_COMMENTS[name])
    if text == expected:
        status, detail = PASS, f"{path}: installed, calls {script}, identical to what install-hooks writes"
    elif _path_lines(text) == _path_lines(expected):
        status = WARN
        detail = f"{path}: same logic as install-hooks, other paths (hook: python {python}, script {script}; here: {sys.executable}, {tooling_script().as_posix()})"
    else:
        status, detail = FAIL, f"{path} has drifted from what install-hooks writes (an older or edited hook); re-run install-hooks"
    if not rows:
        rows.append((status, label, detail))
    elif status != PASS:
        rows.append((status, label + " drift", detail))
    return rows


def check_config(repo, only_target):
    """(rows, config or None, d)."""
    d = perf_dir(repo)
    if d is None:
        return [(FAIL, "config", f"{repo} is not a git repository")], None, None
    path = d / "config.json"
    if not path.is_file():
        return [(WARN, "config", f"{path} does not exist: install-hooks has not run here, so the gate has nothing to gate")], None, d
    try:
        config = read_json(path)
        if not isinstance(config, dict) or not isinstance(config.get("targets"), dict):
            raise ValueError("no 'targets' object")
    except (OSError, ValueError) as error:
        return [(FAIL, "config", f"{path} is not valid: {error}")], None, d
    rows = [(PASS, "config", f"{path} parses; targets: {', '.join(config['targets']) or 'none'}")]
    if not config["targets"]:
        rows.append((WARN, "config targets", "no target is configured, so the gate measures nothing"))
    for target, t in config["targets"].items():
        if only_target and target != only_target:
            continue
        label = f"target {target}"
        if not isinstance(t, dict) or not all(isinstance(t.get(k), str) and t.get(k) for k in ("build_dir", "exe", "baseline")):
            rows.append((FAIL, label, "needs build_dir, exe and baseline"))
            continue
        build, exe, baseline = Path(t["build_dir"]), Path(t["exe"]), Path(t["baseline"])
        if not build.is_dir():
            rows.append((WARN, label, f"not built yet: build dir {build} is missing"))
        elif not exe.is_file():
            rows.append((WARN, label, f"not built yet: {exe} is missing"))
        else:
            rows.append((PASS, label, f"build dir and {exe.name} exist"))
        if not baseline.is_file():
            rows.append((FAIL, label + " baseline", f"{baseline} does not exist; the gate would judge against nothing"))
        else:
            try:
                benchmarks = read_json(baseline).get("benchmarks")
                if not isinstance(benchmarks, dict) or not benchmarks:
                    raise ValueError("no benchmarks")
                rows.append((PASS, label + " baseline", f"{baseline.name}: {len(benchmarks)} benchmarks"))
            except (OSError, ValueError, AttributeError) as error:
                rows.append((FAIL, label + " baseline", f"{baseline} is not a baseline: {error}"))
    if only_target and only_target not in config["targets"]:
        rows.append((FAIL, f"target {only_target}", f"not configured (have: {', '.join(config['targets']) or 'none'})"))
    rows += check_pin_entries(repo, config)
    rows += check_settings(config)
    return rows, config, d


def check_pin_entries(repo, config):
    rows = []
    for pin in config.get("pins", []):
        label = f"pin {pin.get('name') or pin.get('var')}"
        problems = []
        if not isinstance(pin, dict) or not pin.get("file") or not pin.get("var"):
            rows.append((FAIL, "pin", f"needs file and var: {pin}"))
            continue
        if not (pin.get("repo") or pin.get("checkout")):
            problems.append("needs repo= or checkout= to verify against")
        problems += [f"unknown key {k}" for k in pin if k not in PIN_KEYS]
        file = Path(repo) / pin["file"]
        if not file.is_file():
            problems.append(f"{pin['file']} does not exist")
        elif pinned_sha(file.read_text(encoding="utf-8", errors="replace"), pin["var"]) is None:
            rows.append((WARN, label, f"set({pin['var']} <sha>) is not found in {pin['file']}; the pin check cannot read it"))
            continue
        rows.append((FAIL, label, "; ".join(problems)) if problems else (PASS, label, f"{pin['var']} in {pin['file']} parses"))
    return rows


def check_settings(config):
    rows = []
    refs = config.get("full_refs")
    if refs is not None and not (isinstance(refs, list) and all(isinstance(p, str) and p for p in refs)):
        rows.append((FAIL, "config full_refs", f"must be a list of tag patterns, not {refs!r}"))
    budget = config.get("quick_budget")
    if budget is not None:
        try:
            float(budget)
        except (TypeError, ValueError):
            rows.append((FAIL, "config quick_budget", f"must be a number of seconds, not {budget!r}"))
    return rows


def drive(repo, refs_text, changed_paths, budget):
    """Runs the real gate_command in plan-only mode on synthetic refs. Returns (exit code, outcome, previews, output)."""
    args = types.SimpleNamespace(repo=str(repo), refs_from_stdin=True, plan_only=True, changed_paths=changed_paths, lock_timeout=1.0,
                                 level="quick", budget=budget, rounds=3)
    real, out = sys.stdin, io.StringIO()
    sys.stdin = io.StringIO(refs_text)
    try:
        with contextlib.redirect_stdout(out):
            code = gate.gate_command(args)
    finally:
        sys.stdin = real
    return code, args.outcome, getattr(args, "previews", []), out.getvalue()


def check_decisions(repo, config, d, budget):
    if config is None or not config["targets"]:
        return [(WARN, "gate decisions", "no configured target; the gate stops before any decision")]
    head = git_head(repo)
    if head is None:
        return [(WARN, "gate decisions", "the repository has no commit to push")]
    parent = git(repo, "rev-parse", "HEAD~1") or "1" * 40
    update = f"refs/heads/selftest {head} refs/heads/selftest {parent}\n"
    delete = f"refs/heads/selftest {ZERO} refs/heads/selftest {parent}\n"
    never = lambda remote, local: (_ for _ in ()).throw(AssertionError("a push with nothing to compare must not diff"))
    rows = []
    for label, text, paths, want in (
            ("empty stdin (up to date)", "", never, "skip-empty"),
            ("deletion only", delete, never, "skip-deletions"),
            ("docs-only range", update, lambda r, l: ["docs/Guide.md", "README.md", ".ci/local.json"], "skip-docs")):
        try:
            code, outcome, _, _ = drive(repo, text, paths, budget)
        except Exception as error:  # a decision that crashes is a failed decision
            rows.append((FAIL, f"gate: {label}", f"crashed: {type(error).__name__}: {error}"))
            continue
        rows.append((PASS if outcome == want and code == 0 else FAIL, f"gate: {label}",
                     f"skips without building or measuring ({outcome})" if outcome == want and code == 0
                     else f"expected {want} and exit 0, got {outcome or 'no decision'} and exit {code}"))
    try:
        code, outcome, previews, _ = drive(repo, update, lambda r, l: ["include/Example.hpp", "docs/Guide.md"], budget)
    except Exception as error:
        return rows + [(FAIL, "gate: code range", f"crashed: {type(error).__name__}: {error}")]
    if outcome != "measure" or code != 0:
        return rows + [(FAIL, "gate: code range", f"expected a measuring run, got {outcome or 'no decision'} and exit {code}")]
    for p in previews:
        rows += describe_preview(p)
    return rows


def describe_preview(p):
    target, state, names = p["target"], p["state"], p["names"]
    label = f"gate: code range {target}"
    plan = p["plan"]
    if plan is None:
        why = "the quick level has no time budget" if p["level"] == levels.QUICK and not p["budget"] else "no benchmark is known"
        return [(WARN, label, f"measuring is planned but no time-budgeted plan can be made: {why}")]
    unplanned = planning.unplanned(names, state)
    unknown = [n for n in names if n not in state["costs"] and n not in state.get("slow", {})]
    slow = [n for n in names if n in state.get("slow", {})]
    total = plan.estimate + p["allowance"]
    fits = total <= p["budget"]
    rows = [(PASS if fits else FAIL, label,
             f"quick plan {len(plan.selected)}/{len(names)} benchmarks x {plan.rounds} rounds: {plan.estimate:.0f} s predicted"
             f" + {p['allowance']:.0f} s probe allowance = {total:.0f} s {'within' if fits else 'OVER'} the {p['budget']:g} s budget")]
    units = plan.units
    benchmarks = state.get("benchmarks", {})
    stalest = min(units, key=lambda k: (planning.unit_staleness(units[k], benchmarks), k)) if units else None
    last = planning.unit_staleness(units[stalest], benchmarks) if stalest else -1
    when = "never measured" if last < 0 else f"last measured {state.get('pushes', 0) - last} push(es) ago"
    rows.append((WARN if unknown or slow else PASS, f"coverage {target}",
                 f"{len(unknown)} unknown (no recorded cost), {len(slow)} slow (full level only), {len(plan.deferred)} group(s) deferred; "
                 f"every group covered within {plan.pushes_to_cover} push(es); stalest group: {stalest} ({when})"))
    if unplanned and not unknown and not slow:
        rows.append((WARN, f"coverage {target}", "benchmarks are unplanned for a reason the state does not explain"))
    return rows


def check_lock(repo):
    d = perf_dir(repo)
    lock = PerfLock(repo, "selftest")
    if d is None or lock.path is None or not lock.path.exists():
        return [(PASS, "lock", "no lock file")]
    holder = lock._holder()
    if lock._stale():
        return [(FAIL, "lock", f"stale lock {lock.path} ({'unreadable' if holder is None else f'pid {holder[0]} is gone or it is old'}); "
                               f"the next gate would remove it, delete it now if no run is active")]
    return [(WARN, "lock", f"{lock.path} is held by a live run ({holder[2]}, pid {holder[0]}); a gate would wait for it")]


def run_selftest(repo, target=None, budget=None):
    """All rows for `repo`."""
    rows = []
    environment = [name for name in ("SUB0MATH_PERF_SKIP", "SUB0MATH_PINS_SKIP") if os.environ.get(name)]
    if environment:
        rows.append((WARN, "environment", f"{', '.join(environment)} is set: the real gate would be bypassed (not applied in this test)"))
    rows += check_hook(repo, "pre-push", required=True)
    rows += check_hook(repo, "post-checkout", required=False)
    config_rows, config, d = check_config(repo, target)
    rows += config_rows
    rows += check_decisions(repo, config, d, budget)
    rows += check_lock(repo)
    return rows


def render(rows, title):
    width = max(len(r[1]) for r in rows)
    lines = [f"sub0perf selftest: {title}", ""]
    lines += [f"{status:5} {check:{width}}  {detail}" for status, check, detail in rows]
    counts = {s: sum(1 for r in rows if r[0] == s) for s in (PASS, WARN, FAIL)}
    lines += ["", f"{counts[PASS]} passed, {counts[WARN]} warning(s), {counts[FAIL]} failed"]
    return "\n".join(lines)


def selftest_command(args):
    repo = Path(args.repo or git(".", "rev-parse", "--show-toplevel") or ".").resolve()
    rows = run_selftest(repo, args.target, args.budget)
    print(render(rows, str(repo)))
    return 1 if any(r[0] == FAIL for r in rows) else 0
