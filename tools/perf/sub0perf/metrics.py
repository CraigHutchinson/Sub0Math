"""Deterministic metrics: exact and maximum values a benchmark executable reports (sizes, counts, hashes, iterations).

`metrics --spec S (--results F | --exe BENCH)`: with --exe the executable is run with `--metrics-only --json` first.
The executable (`BenchRunner::metric`, `--metrics-only`) writes `{"metrics":[{"name","value","unit","kind"}]}`. A spec
(`perf/metrics.json`, committed) holds what each must be:

  {"schema": "sub0perf-metrics-spec-1",
   "metrics": {"sizeof Vec3<double>": {"kind": "exact", "expected": 24, "unit": "B"},
               "iterations to converge": {"kind": "max", "limit": 40},
               "known gap": {"kind": "exact", "expected": 1, "xfail": "why it is expected to fail today"}},
   "accepted": []}

  exact  value must equal `expected` (optional `tolerance`, absolute, default 0)
  max    value must be <= `limit`
  xfail  a reason: the metric is expected to fail. XFAIL (still failing) does not block; XPASS (it passes) does, so the
         marker gets removed: a metric does not stay "expected to fail" after it was fixed.

Statuses: ok, FAIL, XFAIL, XPASS, MISSING (in the spec, not reported: blocks), NEW (reported, not in the spec: listed,
does not block). `metrics` exits 1 on FAIL, XPASS or MISSING, 2 on unreadable input. Nothing here depends on time or the
machine, so there is no noise, no calibration and no power policy.

`accept --metric NAME... --reason TEXT (--results <metrics.json> | --exe BENCH) --spec <spec>` moves the named metrics' expected value
(or limit) to the measured one, clears the xfail of a metric that is named, and logs date, commit, reason, before and after in the
spec's `accepted` list, like accept.py does for a baseline. `--all-changed` takes the failing and the new metrics, never an xfail
one (an xfail marker is cleared only by naming the metric). The spec is edited in place with the smallest change: its layout and
key order stay (jsonpatch.py). Commit the spec with the change.
"""

import json
import subprocess
import time
from pathlib import Path

from . import jsonpatch, output
from .common import atomic_write_text, read_json
from .gitstate import git_head

SPEC_SCHEMA = "sub0perf-metrics-spec-1"
KINDS = ("exact", "max")
BLOCKING = ("FAIL", "XPASS", "MISSING")


class MetricsError(ValueError):
    pass


def load_results(path):
    data = read_json(path)
    items = data.get("metrics") if isinstance(data, dict) else None
    if not isinstance(items, list):
        raise MetricsError(f"{path} has no 'metrics' list (write it with `<bench> --metrics-only --json FILE`)")
    out = {}
    for m in items:
        value = m.get("value") if isinstance(m, dict) else None
        if not isinstance(m, dict) or not isinstance(m.get("name"), str) or not isinstance(value, (int, float)) or isinstance(value, bool):
            raise MetricsError(f"{path}: every metric needs a string name and a numeric value, got {m!r}")
        kind = m.get("kind", "exact")
        if kind not in KINDS:
            raise MetricsError(f"{path}: metric {m['name']!r} has kind {kind!r}, expected exact or max")
        out[m["name"]] = {"value": value, "unit": m.get("unit", ""), "kind": kind}
    return out


def results_file(args):
    """The metrics results file: `--results`, else the one `--exe --metrics-only --json` writes (to --out, or beside the executable)."""
    if args.results:
        return args.results
    exe = Path(args.exe)
    out = Path(args.out) if args.out else exe.with_name(exe.stem + ".metrics.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    try:
        done = subprocess.run([str(exe), "--metrics-only", "--json", str(out)], capture_output=True, text=True, timeout=300)
    except (OSError, subprocess.SubprocessError) as error:
        raise MetricsError(f"could not run {exe} --metrics-only ({error})")
    if done.returncode != 0:
        raise MetricsError(f"{exe} --metrics-only exited {done.returncode}: {(done.stderr or done.stdout).strip()[:300]}")
    return str(out)


def load_spec(path):
    path = Path(path)
    if not path.is_file():
        return {"schema": SPEC_SCHEMA, "metrics": {}, "accepted": []}
    data = read_json(path)
    if not isinstance(data, dict) or not isinstance(data.get("metrics"), dict):
        raise MetricsError(f"{path} is not a metrics spec (an object with a 'metrics' object)")
    for name, entry in data["metrics"].items():
        kind = entry.get("kind", "exact") if isinstance(entry, dict) else None
        key = "expected" if kind == "exact" else "limit"
        if kind not in KINDS or not isinstance(entry.get(key), (int, float)):
            raise MetricsError(f"{path}: metric {name!r} needs kind exact with 'expected', or kind max with 'limit'")
        if "xfail" in entry and not (isinstance(entry["xfail"], str) and entry["xfail"].strip()):
            raise MetricsError(f"{path}: metric {name!r}: xfail must be a reason (text)")
    return data


def passes(kind, value, entry):
    if kind == "max":
        return value <= entry["limit"]
    return abs(value - entry["expected"]) <= entry.get("tolerance", 0)


def _number(x):
    return f"{x:g}" if isinstance(x, float) else str(x)


def judge(results, spec):
    """Rows (name, status, detail) for every metric of the spec, then those of the results the spec lacks."""
    rows = []
    for name, entry in spec["metrics"].items():
        kind = entry.get("kind", "exact")
        unit = entry.get("unit") or (results.get(name) or {}).get("unit", "")
        suffix = f" {unit}" if unit else ""
        if name not in results:
            rows.append((name, "MISSING", f"not reported by the executable (spec: {kind} {_number(entry.get('limit', entry.get('expected')))}{suffix})"))
            continue
        value = results[name]["value"]
        ok = passes(kind, value, entry)
        if kind == "max":
            detail = f"{_number(value)}{suffix} {'<=' if ok else '>'} max {_number(entry['limit'])}"
        else:
            detail = f"{_number(value)}{suffix} {'==' if ok else '!= expected'} {_number(entry['expected'])}"
        if "xfail" in entry:
            rows.append((name, "XPASS" if ok else "XFAIL", f"{detail} (xfail: {entry['xfail']})" + ("; it passes now, remove the xfail" if ok else "")))
        else:
            rows.append((name, "ok" if ok else "FAIL", detail))
    for name, m in results.items():
        if name not in spec["metrics"]:
            unit = f" {m['unit']}" if m["unit"] else ""
            rows.append((name, "NEW", f"{_number(m['value'])}{unit} ({m['kind']}), no entry in the spec"))
    return rows


def counts_of(rows):
    keys = {"FAIL": "failed", "XPASS": "xpass", "MISSING": "missing", "XFAIL": "xfail", "NEW": "new", "ok": "ok"}
    counts = {v: 0 for v in keys.values()}
    for _, status, _ in rows:
        counts[keys[status]] += 1
    return counts


def brief_summary(counts):
    parts = [f"{counts['failed'] + counts['xpass'] + counts['missing']} failed", f"{counts['xfail']} xfail", f"{counts['ok']} ok"]
    if counts["new"]:
        parts.append(f"{counts['new']} new")
    return ", ".join(parts)


def markdown(rows, spec_path, results_path):
    lines = [f"**Metrics** from `{results_path}` against `{spec_path}`\n", "| Metric | Status | Detail |", "|---|---|---|"]
    lines += [f"| {name} | {status} | {detail} |" for name, status, detail in rows]
    return "\n".join(lines)


def metrics_command(args):
    fmt = output.resolve_format(args)
    try:
        args.results = results_file(args)
        results, spec = load_results(args.results), load_spec(args.spec)
    except (OSError, ValueError) as error:  # unreadable or malformed input is not a verdict
        print(f"sub0perf: metrics: {error}")
        return 2
    rows = judge(results, spec)
    counts = counts_of(rows)
    blocked = [r for r in rows if r[1] in BLOCKING]
    code = 0 if args.report_only or not blocked else 1
    text = markdown(rows, args.spec, args.results)
    path = Path(args.report) if getattr(args, "report", None) else Path(args.results).with_name(Path(args.results).stem + ".report.md")
    status = "FAIL" if blocked else "PASS"
    shown = [r for r in rows if r[1] not in ("ok", "XFAIL")]
    action = ""
    if blocked or counts["new"]:
        action = (f"fix the code; if the new value is intended: python <tooling>/bench_check.py accept --results {args.results} "
                  f"--spec {args.spec} --metric <name> --reason \"<why>\" (commit the spec with the change)")
    changed = [{"name": n, "kind": "metric", "verdict": s, "detail": d} for n, s, d in shown]
    if fmt == "md":
        print(text)
        written = output.write_report(path, text) if getattr(args, "report", None) else None
        print(f"\nperf metrics: {status} {brief_summary(counts)}")
        output.record_outcome(output.outcome("metrics", status, code, counts, changed, written, action))
        return code
    written = output.write_report(path, text)
    data = output.outcome("metrics", status, code, counts, changed, written, action)
    output.record_outcome(data)
    if fmt == "json":
        print(json.dumps(data))
        return code
    print(f"perf metrics: {status} {brief_summary(counts)}" + (f"; report {written}" if written else ""))
    for name, state, detail in shown:
        print(f"{'FAILED' if state == 'FAIL' else state} {name}: {detail}")
    return code


def _named(args, results, spec):
    """(names to accept, skipped xfail names). `--metric` names are taken as given; `--all-changed` adds the failing and new
    metrics, never an xfail one: accepting it would move its value but keep the marker, and clearing the marker is a decision
    for the person who names the metric."""
    names = list(dict.fromkeys(args.metric or []))
    skipped = []
    if args.all_changed:
        for n, status, _ in judge(results, spec):
            if status in ("FAIL", "NEW") and n not in names:
                names.append(n)
            elif status in ("XFAIL", "XPASS") and n not in names:
                skipped.append(n)
    return names, skipped


def accept_metrics_command(args):
    """Moves metrics of the spec to their measured values with a logged reason (see the module docstring).

    The spec file is edited in place with the smallest change (jsonpatch.py): its layout and key order are kept.
    """
    try:
        args.results = results_file(args)
        results, spec = load_results(args.results), load_spec(args.spec)
    except (OSError, ValueError) as error:
        print(f"bench_check: {error}")
        return 2
    names, skipped = _named(args, results, spec)
    if not names:
        print("bench_check: nothing to accept" + (f" (xfail metrics are only accepted by name: {', '.join(skipped)})" if skipped else ""))
        return 2
    absent = [n for n in names if n not in results]
    if absent:
        print(f"bench_check: {args.results} has no value for: {', '.join(absent)}; measure them first (`<bench> --metrics-only --json FILE`)")
        return 2
    original = Path(args.spec).read_text(encoding="utf-8") if Path(args.spec).is_file() else None
    doc = jsonpatch.Doc(original) if original is not None else None
    changes = {}
    for name in names:
        measured = results[name]
        old = spec["metrics"].get(name)
        kind = (old or {}).get("kind", measured["kind"])
        key = "limit" if kind == "max" else "expected"
        entry = {k: v for k, v in (old or {}).items() if k not in ("xfail", "expected", "limit")}
        entry.update({"kind": kind, key: measured["value"]})
        unit_added = bool(measured["unit"]) and "unit" not in entry
        if unit_added:
            entry["unit"] = measured["unit"]
        changes[name] = {"from": None if old is None else old.get(key), "to": measured["value"], "kind": kind,
                         **({"xfail_cleared": old["xfail"]} if old and "xfail" in old else {})}
        spec["metrics"][name] = entry
        if doc is not None:
            _patch_metric(doc, name, old, entry, key, unit_added, measured)
    log = {"date": time.strftime("%Y-%m-%d"), "commit": git_head(Path(args.spec).resolve().parent) or "",
           "reason": args.reason, "metrics": changes}
    had_accepted = isinstance(spec.get("accepted"), list)
    spec.setdefault("accepted", []).append(log)
    text = _patched_text(doc, spec, log, had_accepted)
    if text is None:  # no spec yet, or a layout the patcher could not follow: write the whole file
        spec.setdefault("schema", SPEC_SCHEMA)
        text = json.dumps(spec, indent=2) + "\n"
        if doc is not None:
            print(f"bench_check: note: {args.spec} was rewritten in full (its layout could not be edited in place)")
    atomic_write_text(args.spec, text)
    for name, c in changes.items():
        print(f"accepted {name}: {c['from']} -> {c['to']}" + (" (xfail cleared)" if "xfail_cleared" in c else ""))
    if skipped:
        print(f"not accepted (xfail; name one to accept it and clear its marker): {', '.join(skipped)}")
    print(f"logged in {args.spec}; commit it with the change, and say why")
    return 0


def _patch_metric(doc, name, old, entry, key, unit_added, measured):
    """Edits metric `name` in the spec text: the value changes, an xfail goes, a missing unit is added; a new metric is appended."""
    path = ["metrics", name]
    if old is None:
        doc.add(["metrics"], name, entry)
        return
    if key in old:
        doc.replace(path + [key], measured["value"])
    else:
        doc.add(path, key, measured["value"])
    if "xfail" in old:
        doc.remove(path + ["xfail"])
    if unit_added:
        doc.add(path, "unit", measured["unit"])


def _patched_text(doc, spec, log, had_accepted):
    """The edited spec text, or None when there is none or it does not parse to the intended spec."""
    if doc is None:
        return None
    try:
        if had_accepted:
            doc.append(["accepted"], log)
        else:
            doc.add([], "accepted", [log])
        if json.loads(doc.text) == spec:
            return doc.text
    except (jsonpatch.PatchError, IndexError, ValueError, KeyError):
        pass
    return None
