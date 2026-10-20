"""The pre-push gate: refuse a push that is not what was built, measure HEAD, block on regressions that persist."""

import os
import subprocess
import sys
from pathlib import Path

from .baseline import baseline_data, make_record
from .common import read_json, tooling_script
from .confirm import confirm_regressions, inconclusive_in, retry_inconclusive
from .gitstate import (ZERO_SHA_CHARS, accepted_since, changed_paths_between, documentation_only, git, git_head, log_event,
                       parse_push_refs, perf_dir, push_problems, reference_record, save_record, tree_clean)
from .judge import evaluate_with, names_with
from .lock import LockTimeout, PerfLock
from .report import render, report
from .runner import measure_all, rerunner


def gate_command(args):
    repo = Path(args.repo or git(".", "rev-parse", "--show-toplevel") or ".")
    skip = os.environ.get("SUB0MATH_PERF_SKIP")
    if skip:
        print(f"sub0perf: pre-push gate skipped (SUB0MATH_PERF_SKIP={skip})")
        log_event(repo, {"event": "skip", "commit": git_head(repo), "reason": skip})
        return 0
    d = perf_dir(repo)
    config_path = d / "config.json" if d else None
    if not config_path or not config_path.is_file():
        print("sub0perf: no benchmarks configured for this repository; nothing to gate")
        return 0
    head = git_head(repo)
    clean = tree_clean(repo)
    if args.refs_from_stdin:  # a push: what is measured must be what is pushed
        refs = parse_push_refs(sys.stdin.read())
        if refs and all(set(r[1]) == ZERO_SHA_CHARS for r in refs):
            print("sub0perf: only deletions are pushed; nothing to measure")
            return 0
        problems = push_problems(refs, head, clean, lambda sha: git(repo, "rev-parse", f"{sha}^{{commit}}") or sha)
        if problems:
            print("sub0perf: push refused by the performance gate:")
            for problem in problems:
                print(f"  - {problem}")
            print("  (SUB0MATH_PERF_SKIP=<reason> bypasses the gate and logs the reason; git push --no-verify skips all hooks)")
            log_event(repo, {"event": "gate-refused", "commit": head, "problems": problems})
            return 1

        if documentation_only(refs, lambda remote, local: changed_paths_between(repo, remote, local)):
            print("sub0perf: the push changes documentation only; nothing to measure")
            log_event(repo, {"event": "skip", "commit": head, "reason": "documentation only"})
            return 0
    try:
        with PerfLock(repo, "gate", args.lock_timeout):
            return run_gate(args, repo, d, config_path, head, clean)
    except LockTimeout as e:
        print(f"sub0perf: {e}; push blocked")
        return 1


def run_gate(args, repo, d, config_path, head, clean):
    config = read_json(config_path)
    upstream = git(repo, "rev-parse", "@{upstream}")
    blocked = []
    for target, t in config.get("targets", {}).items():
        print(f"sub0perf: pre-push gate for {target} at {head[:9]}{'' if clean else ' (working tree has uncommitted changes)'}")
        command = ["cmake", "--build", t["build_dir"], "--target", target] + (["--config", t["config"]] if t.get("config") else [])
        build = subprocess.run(command, env={**os.environ, "SUB0MATH_PERF_SKIP": "1"})
        if build.returncode != 0:
            print(f"sub0perf: building {target} failed; push blocked")
            blocked.append(target)
            continue
        exe = t["exe"]
        args.filter = ""
        samples, meta, positions, counters = measure_all(exe, args)
        record = make_record(exe, samples, meta, positions, counters, args.epochs, args.seed)
        args.baseline = t["baseline"]
        ref_sha, ref = reference_record(repo, target, upstream)

        def both(record=record, ref=ref, baseline=t["baseline"]):
            found = [evaluate_with(record, baseline, args)]
            if ref:
                found.append(evaluate_with(record, baseline_data(ref), args))
            return found

        rerun = rerunner(exe, args, samples, meta, positions, counters)

        # Inconclusive (noisy, environment-shifted) results are measured once more with twice the rounds, and a push
        # is never blocked by what stays inconclusive. One noisy measurement never blocks either: whatever regressed
        # is re-measured, and only what persists counts.
        retried, _ = retry_inconclusive(both, rerun, args.rounds)
        confirm_regressions(both, rerun, args.confirm_rounds)
        inconclusive = sorted(inconclusive_in(both())[0])
        record["inconclusive"] = inconclusive
        record["inconclusive_retried"] = sorted(retried)
        save_record(repo, target, record)  # the record before push (with any extra rounds)
        print("\n### Against the committed baseline\n")
        rows, pair_rows, regressed_base = report(record, args)
        regressed_ref = []
        if ref:
            print(f"\n### Against the local record of {ref_sha[:9]} (like for like on this machine)\n")
            ref_rows, ref_pairs, env = evaluate_with(record, baseline_data(ref), args)
            print(render(ref_rows, ref_pairs, env, f"record {ref_sha[:9]}", args.tolerance, args.pair_tolerance))
            regressed_ref = names_with(ref_rows, ref_pairs, env, "REGRESSED")
        else:
            print("\nsub0perf: no local record of an earlier commit; it is made by the first clean build after a checkout")
        baseline_rel = os.path.relpath(t["baseline"], repo).replace("\\", "/")
        accepted = accepted_since(repo, baseline_rel, ref_sha)
        unaccepted = [n for n in regressed_ref if n not in accepted and "*" not in accepted]
        if regressed_base or unaccepted:
            blocked.append(target)
            names_text = ", ".join(sorted(set(unaccepted) | {r[0] for r in rows if r[5] == "REGRESSED"}
                                         | {p[0] for p in pair_rows if p[4] == "REGRESSED"}))
            print(f"\nsub0perf: push blocked by regressions in {target}: {names_text}")
            print("  Fix them, or accept them explicitly and commit the baseline:")
            print(f'    python "{tooling_script()}" accept --results "{d / "records" / target / (head + ".json")}"'
                  f' --baseline "{t["baseline"]}" --all-changed --reason "why this is acceptable"')
        if inconclusive:
            print(f"\nsub0perf: WARNING: {len(inconclusive)} benchmark(s) of {target} stayed inconclusive after re-measuring with "
                  f"{args.rounds} more round(s); nothing was proven about them{'' if target in blocked else ', and the push is allowed'}:")
            for name in inconclusive:
                print(f"  - {name}")
            print("  (recorded in the local record and log; re-run the check on a quieter machine to judge them)")
        log_event(repo, {"event": "gate", "target": target, "commit": head, "reference": ref_sha,
                         "blocked": target in blocked, "regressed_vs_baseline": regressed_base, "unaccepted_vs_record": unaccepted,
                         "inconclusive": inconclusive, "seed": args.seed})
    return 1 if blocked else 0
