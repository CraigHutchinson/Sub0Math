"""`compare`: an interleaved A/B of two builds, to decide whether a change is faster, slower or neutral.

  bench_check.py compare --exe NEW --against OLD            two executables
  bench_check.py compare --exe NEW --kept [SHA] --repo DIR  NEW against a build kept in .git/sub0perf/bins/<target>/

Both builds are measured alternately (which one goes first flips every round), at least MIN_ROUNDS rounds, in this
session, so a difference is the code and not the machine's state: the logic of reproduce.py, exposed as a command.
`--filter TEXT` limits it to the benchmarks (and their groups) whose name contains TEXT. The verdict is NEW relative to
OLD: REGRESSED means NEW is slower. Output follows the contract of output.py (brief by default under SUB0PERF_OUTPUT=brief).
"""

import filecmp
from pathlib import Path

from . import output
from .baseline import baseline_data
from .common import group_of
from .gitstate import git, perf_dir
from .judge import evaluate_with
from .report import render
from .reproduce import groups_of, side_by_side
from .runner import list_names

MIN_ROUNDS = 10


def kept_builds(repo, target):
    """The executables kept for `target`, newest first, as [(commit, path)]."""
    d = perf_dir(Path(repo))
    where = d / "bins" / target if d else None
    if not where or not where.is_dir():
        return []
    found = []
    for directory in sorted((p for p in where.iterdir() if p.is_dir()), key=lambda p: p.stat().st_mtime, reverse=True):
        files = [p for p in directory.iterdir() if p.is_file()]
        if files:
            found.append((directory.name, files[0]))
    return found


def resolve_against(args):
    """(path of the build to compare against, description) or (None, why not)."""
    if args.against:
        path = Path(args.against)
        return (path.resolve(), str(path)) if path.is_file() else (None, f"--against {args.against} is not a file")
    repo = Path(args.repo or git(".", "rev-parse", "--show-toplevel") or ".")
    target = args.target or Path(args.exe).stem
    builds = kept_builds(repo, target)
    if not builds:
        return None, f"no kept build of {target} in {repo} (a clean-tree build or gate run keeps one; or pass --against)"
    wanted = None if args.kept in (None, "latest", "") else args.kept
    for commit, path in builds:
        if wanted:
            if commit.startswith(wanted):
                return path, f"kept build of {commit[:9]}"
        elif not filecmp.cmp(path, args.exe, shallow=False):  # the newest kept build that is not the one being measured
            return path, f"kept build of {commit[:9]}"
    return None, (f"no kept build starts with {wanted}" if wanted else
                  f"every kept build of {target} is identical to {Path(args.exe).name}; nothing to compare (pass --against or --kept SHA)")


def shared_names(listing, exe, ref, text_filter):
    """The benchmarks both builds have, whole groups of those matching the filter (so a pair keeps its reference)."""
    both = set(listing(exe, "")) & set(listing(ref, ""))
    if not text_filter:
        return sorted(both)
    matching = set(listing(exe, text_filter)) & both
    return groups_of(sorted(matching), both) if matching else []


def compare_command(args, measure=side_by_side, listing=list_names):
    fmt = output.resolve_format(args)
    ref, described = resolve_against(args)
    if ref is None:
        print(f"sub0perf: compare: {described}")
        return 2
    rounds = max(args.rounds, MIN_ROUNDS)
    exe = Path(args.exe)
    args.report_default = str(exe.parent / f"{exe.stem}.compare.md")
    with output.Quiet(fmt, Path(output.report_path(args)).with_suffix(".log")) as quiet:
        try:
            names = shared_names(listing, args.exe, str(ref), args.filter)
            if not names:
                print("sub0perf: compare: the two builds share no benchmark matching the filter")
                quiet.exit_code = 2
                return 2
            now, old = measure(args.exe, str(ref), names, rounds, args)
        except Exception as error:  # a build that does not start is no verdict
            print(f"sub0perf: compare failed ({type(error).__name__}: {error})")
            quiet.exit_code = 2
            return 2
        rows, pair_rows, env = evaluate_with(now, baseline_data(old), args, True)
        text = (f"**Compare:** {exe.name} against {described}; {rounds} alternating rounds, {len(names)} benchmark(s). "
                f"REGRESSED means the new build is slower.\n\n" + render(rows, pair_rows, env, described, args.tolerance, args.pair_tolerance))
        counts = output.counts_of(rows, pair_rows, env)
        code = 0 if args.report_only or not counts["regressed"] else 1
        output.emit_text(fmt, "compare", counts, output.changed_rows(rows, pair_rows, env, args), text, output.report_path(args), args, code,
                         label="perf compare")
        return code
