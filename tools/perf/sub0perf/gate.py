"""The pre-push gate: refuse a push that is not what was built, measure HEAD, block on regressions that persist."""

import os
import subprocess
import sys
from pathlib import Path

from .baseline import baseline_data, make_record
from . import levels, plan as planning
from .common import group_of, read_json, tooling_script
from .confirm import confirm_regressions, inconclusive_in, retry_inconclusive
from .gitstate import (ZERO_SHA_CHARS, accepted_since, changed_paths_between, documentation_only, git, git_head, log_event,
                       parse_push_refs, perf_dir, pushed_refs, push_problems, reference_binary, reference_record, save_record, tree_clean)
from .judge import evaluate_with, names_with
from .lock import LockTimeout, PerfLock
from .pins import pin_problems, show_file_at
from .report import render, report
from .reproduce import reproduced_against
from .runner import list_names, measure_all, probe_cost, rerunner
from .stats import rounds_needed


def check_pins(repo, config, refs):
    """Refuses a push whose pinned dependency commits are not on the dependencies' remotes. True when the push may go on.

    Runs before everything else, also when the performance gate is bypassed or the push is documentation only (a pin
    file is not documentation): `SUB0MATH_PINS_SKIP=<reason>` bypasses it, logged.
    """
    pins = config.get("pins", [])
    if not pins or not pushed_refs(refs):
        return True
    reason = os.environ.get("SUB0MATH_PINS_SKIP")
    if reason:
        print(f"sub0perf: dependency pin check skipped (SUB0MATH_PINS_SKIP={reason})")
        log_event(repo, {"event": "pins-skip", "reason": reason})
        return True
    problems, warnings = pin_problems(repo, pins, refs, lambda remote, local: changed_paths_between(repo, remote, local),
                                      show_file_at(repo))
    for warning in warnings:
        print(f"sub0perf: WARNING: {warning}")
    if warnings:
        log_event(repo, {"event": "pins-unverified", "warnings": warnings})
    if problems:
        print("sub0perf: push refused: a pinned dependency is not on its remote:")
        for problem in problems:
            print(f"  - {problem}")
        print("  (SUB0MATH_PINS_SKIP=<reason> bypasses this check and logs the reason; git push --no-verify skips all hooks)")
        log_event(repo, {"event": "pins-refused", "problems": problems})
        return False
    return True


def gate_command(args):
    repo = Path(args.repo or git(".", "rev-parse", "--show-toplevel") or ".")
    d = perf_dir(repo)
    config_path = d / "config.json" if d else None
    config = read_json(config_path) if config_path and config_path.is_file() else None
    refs = parse_push_refs(sys.stdin.read()) if getattr(args, "refs_from_stdin", False) else []
    if config and not check_pins(repo, config, refs):
        return 1
    skip = os.environ.get("SUB0MATH_PERF_SKIP")
    if skip:
        print(f"sub0perf: pre-push gate skipped (SUB0MATH_PERF_SKIP={skip})")
        log_event(repo, {"event": "skip", "commit": git_head(repo), "reason": skip})
        return 0
    if config is None:
        print("sub0perf: no benchmarks configured for this repository; nothing to gate")
        return 0
    try:
        levels.resolve(args, refs, config)
    except ValueError as error:
        print(f"sub0perf: {error}; push blocked")
        return 1
    print(f"sub0perf: level {args.level} ({args.level_why}); "
          f"{'budget ' + format(args.budget_seconds, 'g') + ' s (' + args.budget_why + ')' if args.budget_seconds else 'no time budget'}")
    head = git_head(repo)
    clean = tree_clean(repo)
    if args.refs_from_stdin:  # a push: what is measured must be what is pushed
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


def choose_benchmarks(args, exe, target, state, head, all_names, baseline=None):
    """(benchmarks to measure, rounds) for this level: everything at full, a plan that fits the time budget at quick."""
    rounds = args.rounds
    if args.level != levels.QUICK:
        return all_names, rounds
    if not args.budget_seconds:
        print(f"sub0perf: quick level without a time budget: all {len(all_names)} benchmarks, {rounds} rounds")
        return all_names, rounds
    per_sample = None
    if not state["costs"] and all_names:  # nothing timed yet: a cheap probe of one sample and one calibration
        per_sample, overhead = probe_cost(exe, (planning.choose_controls(all_names, {}, (), 1) or all_names)[0], args)
        state["round_overhead"] = overhead
        print(f"sub0perf: no timings of {target} yet; probe: {per_sample:.1f} s per sample, {overhead:.1f} s of calibration per round")
    rounds = max(rounds, planning.QUICK_ROUNDS)
    known = read_json(baseline).get("benchmarks", {}) if baseline and Path(baseline).is_file() else {}
    bimodal = [n for n, b in known.items() if isinstance(b, dict) and b.get("modes")]
    plan = planning.plan_quick(all_names, state, head, args.budget_seconds, rounds, default_cost=per_sample, avoid=bimodal)
    for line in planning.describe_plan(plan, len(all_names), state, head):
        print(line)
    return plan.selected, rounds


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
        quick = args.level == levels.QUICK
        state = planning.load_state(d, target)
        timings = planning.new_timings()
        all_names = list_names(exe, "")
        chosen, rounds = choose_benchmarks(args, exe, target, state, head, all_names, t["baseline"])
        args.subset = len(chosen) < len(all_names)
        budget = planning.Budget(args.budget_seconds)  # measuring time; the build above does not count
        samples, meta, positions, counters = measure_all(exe, args, names=chosen, rounds=rounds, timings=timings)
        record = make_record(exe, samples, meta, positions, counters, args.epochs, args.seed)
        record["level"] = args.level
        record["unmeasured"] = sorted(set(all_names) - set(chosen))
        args.baseline = t["baseline"]
        ref_sha, ref = reference_record(repo, target, upstream)

        def both(record=record, ref=ref, baseline=t["baseline"]):
            found = [evaluate_with(record, baseline, args)]
            if ref:
                found.append(evaluate_with(record, baseline_data(ref), args))
            return found

        rerun = rerunner(exe, args, samples, meta, positions, counters, timings=timings)
        more_for = lambda names, record=record: rounds_needed(record, names)

        def affordable(names, extra, record=record, state=state, budget=budget):
            return budget.affords(planning.pass_estimate(names, max(extra, rounds_needed(record, names)), state))

        # A benchmark the baseline knows to sit at two levels is measured for enough rounds from the start for the
        # two-level test to work, instead of finding out only when it looks regressed.
        known = read_json(t["baseline"]).get("benchmarks", {}) if Path(t["baseline"]).is_file() else {}
        groups = {group_of(n) for n, b in known.items() if isinstance(b, dict) and b.get("modes") and n in samples}
        bimodal = {n for n in samples if group_of(n) in groups}
        # Full measures them that long from the start; quick only when a verdict turns out to depend on it (the
        # confirmation and retry passes below always gather that many samples for what they re-measure).
        if args.level == levels.FULL and bimodal and more_for(bimodal):
            print(f"sub0perf: {len(bimodal)} benchmark(s) are known to be bimodal; measuring them for {more_for(bimodal)} more round(s)")
            rerun(bimodal, more_for(bimodal))

        # Inconclusive (noisy, environment-shifted) results are measured once more with twice the rounds, and a push
        # is never blocked by what stays inconclusive. One noisy measurement never blocks either: whatever regressed
        # is re-measured, and only what persists counts.
        retried, _ = retry_inconclusive(both, rerun, args.rounds, more_for, affordable if quick and args.budget_seconds else None)
        # A regression is always confirmed before it blocks, even if that overruns a quick budget.
        first_regressed, _ = confirm_regressions(both, rerun, args.confirm_rounds, more_for)
        inconclusive = sorted(inconclusive_in(both())[0])
        measured_s = budget.elapsed()
        planning.absorb_timings(state, timings)
        planning.update_recheck(state, planning.units_of(all_names), chosen, set(first_regressed) | set(inconclusive))
        planning.mark_measured(state, chosen, head)
        planning.save_state(d, target, state)
        print(f"\nsub0perf: measured {len(chosen)}/{len(all_names)} benchmarks at the {args.level} level in {measured_s:.0f} s"
              + (f" (budget {args.budget_seconds:g} s)" if args.budget_seconds else ""))
        if budget.overrun():
            print(f"sub0perf: the time budget was exceeded by {budget.overrun():.0f} s (confirming a regression, or the always-measured "
                  f"pairs and controls, take precedence over the budget)")
        record["inconclusive"] = inconclusive
        record["inconclusive_retried"] = sorted(retried)
        save_record(repo, target, record, exe=exe)  # the record before push (with any extra rounds), and the build that made it
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
        unreproduced = []
        if unaccepted:
            unaccepted, unreproduced = reproduce_vs_record(repo, target, exe, ref_sha, unaccepted, args)
        if regressed_base or unaccepted:
            blocked.append(target)
            names_text = ", ".join(sorted(set(unaccepted) | {r[0] for r in rows if r[5] == "REGRESSED"}
                                         | {p[0] for p in pair_rows if p[4] == "REGRESSED"}))
            print(f"\nsub0perf: push blocked by regressions in {target}: {names_text}")
            print("  Fix them, or accept them explicitly and commit the baseline:")
            print(f'    python "{tooling_script()}" accept --results "{d / "records" / target / (head + ".json")}"'
                  f' --baseline "{t["baseline"]}" --all-changed --reason "why this is acceptable"')
        if unreproduced:
            print(f"\nsub0perf: WARNING: {len(unreproduced)} difference(s) from the record of {ref_sha[:9]} were not reproduced in "
                  f"this session, so they do not block the push: {', '.join(sorted(unreproduced))}")
        if inconclusive:
            print(f"\nsub0perf: WARNING: {len(inconclusive)} benchmark(s) of {target} stayed inconclusive after re-measuring with "
                  f"{args.rounds} more round(s); nothing was proven about them{'' if target in blocked else ', and the push is allowed'}:")
            for name in inconclusive:
                print(f"  - {name}")
            print("  (recorded in the local record and log; re-run the check on a quieter machine to judge them)")
        log_event(repo, {"event": "gate", "target": target, "commit": head, "reference": ref_sha,
                         "blocked": target in blocked, "regressed_vs_baseline": regressed_base, "unaccepted_vs_record": unaccepted, "unreproduced_vs_record": unreproduced,
                         "inconclusive": inconclusive, "seed": args.seed, "level": args.level,
                         "measured": len(chosen), "benchmarks": len(all_names), "seconds": round(measured_s, 1)})
    return 1 if blocked else 0


def reproduce_vs_record(repo, target, exe, ref_sha, names, args):
    """Splits regressions against the local record into (those that reproduce, those that do not).

    The record was measured in an earlier session, so a difference from it can be the session and not the code. It
    blocks only when the record's own build, kept beside the record, is measured again in this session alternately with
    the new build and the difference is still there. Without that build nothing can be shown, so none of them block.
    """
    old = reference_binary(repo, target, ref_sha)
    if old is None:
        print(f"\nsub0perf: the build of {ref_sha[:9]} was not kept, so its differences cannot be re-measured side by side")
        return [], list(names)
    print(f"\nsub0perf: re-measuring the kept build of {ref_sha[:9]} beside the new one for {len(names)} difference(s) from its record")
    again, gone, why = reproduced_against(exe, str(old), names, args)
    if why:
        print(f"sub0perf: {why}")
    print(f"sub0perf: reproduced side by side: {', '.join(sorted(again)) or 'none'}")
    print(f"sub0perf: not reproduced (same times in this session): {', '.join(sorted(gone)) or 'none'}")
    return sorted(again), sorted(gone)
