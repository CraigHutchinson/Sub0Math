---
name: sub0perf-improve-loop
description: Make a benchmarked C++ function faster and prove it with the Sub0 family's sub0perf tooling. Use when asked to optimise, speed up, or check for a performance regression in a project that has `sub0math_add_benchmark` targets (`<T>_runs`, `<T>_check`, `<T>_compare`, `<T>_baseline`). Prints one line per run, so very little is read.
---

# sub0perf improve loop

Full text: `docs/PERF_LOOP.md` in Sub0Math. Do the steps in order; read only the verdict lines.

Setup (once per shell): `export SUB0PERF_OUTPUT=brief`. `T` = the benchmark target, `B` = the build dir. Measure on mains,
on a quiet machine; never run two measuring commands at once (they take a lock).

1. Baseline of where you are: `cmake --build B --target T_check`. Reads one line: `perf: PASS|FAIL <n> regressed, <n> improved, <n> ok, <n> inconclusive; report <path>`,
   then one line per row that moved (`REGRESSED <name>: +14.2% (noise 2.1%, tol 10%)`). Start from a clean tree built once, so the
   previous build is kept (`.git/sub0perf/bins/T/`); otherwise `cp <exe> B/perf/prev.exe` now.
2. Change ONE thing. Rebuild: `cmake --build B --target T`.
3. Compare with the previous build: `cmake --build B --target T_compare` (kept build), or configure with
   `-DSUB0MATH_COMPARE_AGAINST=B/perf/prev.exe`; narrow with `-DSUB0MATH_COMPARE_FILTER="<benchmark name>"`. By hand:
   `python <tooling>/bench_check.py compare --exe <new> --against <old> --filter "<name>"` (the tooling dir is in `B/sub0math_tooling_dir.txt`).
   Output: `perf compare: ...` plus a line per row that moved. REGRESSED = the new build is slower. At least 10 interleaved rounds.
4. Decide:
   - IMPROVED, nothing REGRESSED: keep.
   - Neutral: keep only with a non-speed reason (simpler, smaller, needed next); say it in the commit and in `--reason`. Otherwise reject.
   - REGRESSED: iterate (back to 2), at most three optimisation passes per alternative (the repository's three-pass rule), then park the variant and say so.
   - `inconclusive` rows: measure again on a quiet machine; never decide on them.
5. Record from a full steady run (not a quick one): `python <tooling>/bench_check.py accept --results B/perf/T.samples.json --baseline perf/baselines/<host>.json --all-changed --reason "<what changed and why>"`,
   or for the whole baseline `SUB0MATH_PERF_REASON="<why>" cmake --build B --target T_baseline`. A reason is required and is logged in the baseline.
6. Commit the baseline file with the change (`git add perf/baselines/<host>.json <sources>`); the message says why. Never push unless asked.

Exact facts (sizes, counts, iterations) are checked without timing: `ctest --test-dir B -L smoke`. A deliberate change:
`SUB0MATH_PERF_REASON="<why>" cmake --build B --target T_metrics_accept`, then commit `perf/metrics.json`.

Machine output: add `--format json` (schema `sub0perf-outcome-1`: `status`, `exit`, `counts`, `changed[]`, `report`, `next_action`), or `--out-json PATH` (env `SUB0PERF_OUTCOME_JSON`) to write the same object to a file whatever the format. Open the
report file only when a line does not explain itself. Exit: 0 PASS, 1 FAIL, 2 unusable input.

Do not: accept a regression to get a pass; change several things at once; use `--no-verify` or the bypass variables; edit the baseline by hand.
