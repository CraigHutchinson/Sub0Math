"""The token-efficient output contract of check, auto, gate, compare and metrics.

Three formats, chosen by `--format` (default: `SUB0PERF_OUTPUT`, else `md`):

- `brief`: ONE verdict line, then one line per row changed beyond noise (REGRESSED or IMPROVED; inconclusive and
  unchanged rows are only counted). Meant for hooks and agents: a few hundred bytes, however many benchmarks ran.
      perf: FAIL 1 regressed, 0 improved, 14 ok, 2 inconclusive; report build/perf/X.report.md
      REGRESSED index/cold 1e5: +14.2% (noise 2.1%, tol 10%)
- `json`: exactly one object on stdout, schema `sub0perf-outcome-1` (status, exit, counts, changed[], report,
  next_action), mirroring `sub0perf-workload-outcome-1`.
- `md`: the full markdown tables on stdout, as before.

The full markdown tables go to the `--report <path>` file (brief and json always write one; a default path is chosen
when none is given). Progress chatter of a long run (`auto`, `gate`) is held back in brief and json: it goes to
`<report>.log`, and only the lines that matter (refusals, warnings, failures) are echoed after the verdict.
"""

import contextlib
import io
import json
import os
import sys
from pathlib import Path

from .common import atomic_write_text, tooling_script
from .judge import verdict_hits

FORMATS = ("brief", "json", "md")
OUTCOME_SCHEMA = "sub0perf-outcome-1"
ENV_VAR = "SUB0PERF_OUTPUT"
# Lines of a quieted run that are still shown after the verdict.
IMPORTANT = ("refused", "blocked", "WARNING", "failed", "FAIL", "deferred", "error")


def resolve_format(args):
    """`--format`, else SUB0PERF_OUTPUT (brief|json|md), else md. An unknown environment value is ignored."""
    chosen = getattr(args, "format", None)
    if chosen:
        return chosen
    env = os.environ.get(ENV_VAR, "").strip().lower()
    return env if env in FORMATS else "md"


def default_report_path(args, stem=None):
    """Where a brief/json run keeps its markdown when --report is not given: beside its samples file, else beside the executable."""
    default = getattr(args, "report_default", None)
    if default:
        return Path(default)
    for field, suffix in (("out", ".samples.json"), ("results", ".samples.json")):
        value = getattr(args, field, None)
        if value:
            path = Path(value)
            name = path.name[:-len(suffix)] if path.name.endswith(suffix) else path.stem
            return path.with_name(name + ".report.md")
    exe = getattr(args, "exe", None)
    base = Path(exe).parent if exe else Path("build/perf")
    return base / f"{stem or (Path(exe).stem if exe else 'perf')}.report.md"


def quiet_log_path(args):
    """Where the held-back chatter of an auto or gate run goes: beside its report, or in .git/sub0perf for the gate."""
    if getattr(args, "report", None) or getattr(args, "out", None):
        return Path(report_path(args)).with_suffix(".log")
    repo = Path(getattr(args, "repo", None) or ".")
    git_dir = repo / ".git"
    return (git_dir / "sub0perf" / "last-run.log") if git_dir.is_dir() else (repo / "sub0perf-last-run.log")


def report_path(args, stem=None):
    given = getattr(args, "report", None)
    return Path(given) if given else default_report_path(args, stem)


def _noise_and_tolerance(row, args):
    kind, payload = row
    if kind == "benchmark":
        _, b, s, _, change, _ = payload
        return max((s or {}).get("cv") or 0.0, (b or {}).get("cv") or 0.0), args.tolerance, change
    if kind == "pair":
        _, p, _, change, _ = payload
        return p.get("cv") or 0.0, args.pair_tolerance, change
    raise ValueError(kind)


def changed_rows(rows, pair_rows, env, args, verdicts=("REGRESSED", "IMPROVED")):
    """[{name, kind, verdict, change, noise, tolerance}] for every benchmark, pair or counter judged in `verdicts`."""
    out = []
    for r in rows:
        if r[5] in verdicts:
            noise, tol, change = _noise_and_tolerance(("benchmark", r), args)
            out.append({"name": r[0], "kind": "benchmark", "verdict": r[5], "change": change, "noise": noise, "tolerance": tol})
    for p in pair_rows:
        if p[4] in verdicts:
            noise, tol, change = _noise_and_tolerance(("pair", p), args)
            out.append({"name": p[0], "kind": "pair", "verdict": p[4], "change": change, "noise": noise, "tolerance": tol})
    for row in env["counter_rows"]:
        for text in row["verdicts"]:
            key, verdict = text.split()[0], text.split()[-1]
            if verdict in verdicts:
                change = row.get(key + "_change")
                tol = args.instr_tolerance if key == "instructions" else args.tolerance
                out.append({"name": f"{row['name']} [{key}]", "kind": "counter", "verdict": verdict, "change": change,
                            "noise": 0.0, "tolerance": tol})
    return out


def counts_of(rows, pair_rows, env):
    return {"regressed": len(verdict_hits(rows, pair_rows, env, ["REGRESSED"])),
            "improved": len(verdict_hits(rows, pair_rows, env, ["IMPROVED"])),
            "ok": sum(1 for r in rows if r[5] == "ok"),
            "inconclusive": len(verdict_hits(rows, pair_rows, env, ["NOISY", "ENVIRONMENT"]))}


def brief_line(entry):
    change = "n/a" if entry["change"] is None else f"{entry['change']:+.1%}"
    return f"{entry['verdict']} {entry['name']}: {change} (noise {entry['noise']:.1%}, tol {entry['tolerance']:.0%})"


def verdict_text(counts):
    return (f"{counts['regressed']} regressed, {counts['improved']} improved, {counts['ok']} ok, "
            f"{counts['inconclusive']} inconclusive")


def _script():
    return f"python {tooling_script().as_posix()}"


def next_action(command, counts, args, results=None):
    """The one command to run next, as text (empty when nothing is needed)."""
    baseline = getattr(args, "baseline", None)
    results = results or getattr(args, "results", None) or getattr(args, "out", None) or "<samples.json>"
    if command == "compare":
        if counts["regressed"]:
            return "iterate on the change, rebuild and run compare again (three passes, then decide); or reject it"
        return "keep the change; accept it from a full steady run (`_runs`, then accept --reason / `_baseline --reason`)"
    if counts["regressed"]:
        return (f"fix, rebuild, `_runs` then `_check --format brief`; if intended: {_script()} accept --results {results} "
                f"--baseline {baseline or '<baseline.json>'} --all-changed --reason \"<why>\"")
    if counts["improved"]:
        return (f"record it: {_script()} accept --results {results} --baseline {baseline or '<baseline.json>'} "
                f"--all-changed --reason \"<what changed>\"; commit the baseline with the change")
    return ""


def outcome(command, status, exit_code, counts, changed, report, action, **extra):
    return {"schema": OUTCOME_SCHEMA, "command": command, "status": status, "exit": exit_code, "counts": counts,
            "changed": changed, "report": str(report) if report else None, "next_action": action, **extra}


def status_of(counts):
    return "FAIL" if counts["regressed"] else "PASS"


def write_report(path, text):
    try:
        atomic_write_text(path, text if text.endswith("\n") else text + "\n")
    except OSError as error:
        print(f"sub0perf: could not write the report {path} ({error})", file=sys.stderr)
        return None
    return path


def emit_text(fmt, command, counts, changed, text, path, args, exit_code, results=None, label=None):
    """Prints the outcome in `fmt` and writes the markdown `text` to `path` (brief, json) or only prints it (md).

    Returns the printed outcome dict (None in md). `label` replaces the command name on the verdict line.
    """
    with speaking():
        return _emit(fmt, command, counts, changed, text, path, args, exit_code, results, label)


def _emit(fmt, command, counts, changed, text, path, args, exit_code, results, label):
    state = status_of(counts)
    if fmt == "md":
        print(text)
        if getattr(args, "report", None):
            write_report(path, text)
        print(f"\nperf: {counts['regressed']} regressed, {counts['improved']} improved, {counts['inconclusive']} inconclusive")
        return None
    written = write_report(path, text)
    action = next_action(command, counts, args, results)
    if fmt == "json":
        data = outcome(command, state, exit_code, counts, changed, written, action)
        print(json.dumps(data))
        return data
    print(f"{label or 'perf'}: {state} {verdict_text(counts)}" + (f"; report {written}" if written else ""))
    for entry in changed:
        print(brief_line(entry))
    return outcome(command, state, exit_code, counts, changed, written, action)


ACTIVE = None  # the Quiet block currently holding output back, if any


@contextlib.contextmanager
def speaking():
    """Within the block stdout is the real one even inside a Quiet block; the block counts as having spoken."""
    quiet = ACTIVE
    if quiet is None or not quiet.active:
        yield
        return
    with contextlib.redirect_stdout(quiet._real):
        yield
    quiet.spoke = True


class Quiet:
    """Holds the chatter of a run back (brief, json): `with Quiet(fmt, log_path) as q`; `q.spoke` says an outcome was printed.

    Inside the block `print` goes to a buffer; `emit_text` (the outcome) reaches the real stdout.
    On exit the buffer is written to the log, and the important lines are echoed (all of it, capped, when no outcome was printed).
    """

    def __init__(self, fmt, log_path):
        self.active = fmt in ("brief", "json")
        self.fmt = fmt
        self.log_path = log_path
        self.buffer = io.StringIO()
        self.spoke = False
        self.exit_code = None
        self._stack = contextlib.ExitStack()
        self._real = sys.stdout

    def __enter__(self):
        global ACTIVE
        if self.active:
            self._real = sys.stdout
            ACTIVE = self
            self._stack.enter_context(contextlib.redirect_stdout(self.buffer))
        return self

    def __exit__(self, *exc):
        global ACTIVE
        if not self.active:
            return False
        ACTIVE = None
        self._stack.close()
        text = self.buffer.getvalue()
        lines = [line for line in text.splitlines() if line.strip()]
        if lines and self.log_path:
            try:
                atomic_write_text(self.log_path, text)
            except OSError:
                pass
        if self.fmt == "json":
            if not self.spoke:
                print(json.dumps({"schema": OUTCOME_SCHEMA, "command": None, "status": "NO_VERDICT", "exit": self.exit_code, "counts": None,
                                  "changed": [], "report": None, "next_action": "", "log": lines[-15:]}))
            return False
        shown = [line for line in lines if any(k in line for k in IMPORTANT)] if self.spoke else lines[-15:]
        for line in shown[:15]:
            print(line)
        return False
