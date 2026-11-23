# The improve loop

How to make something faster and prove it, as commands. Written for an agent or a person who wants few tokens read per
run: every step prints one verdict line, and the full tables sit in a report file you open only when a line needs it.
The measurement itself is described in [PERFORMANCE.md](PERFORMANCE.md); an agent skill with the same loop is
[`tools/perf/skill/SKILL.md`](../tools/perf/skill/SKILL.md).

Set once per shell (hooks and agents: always):

```bash
export SUB0PERF_OUTPUT=brief          # check/auto/gate/compare/metrics print ONE verdict line + one line per change beyond noise
```

`T` below is the benchmark target (for example `Sub0Math_Bench`), `B` the build directory.

## The loop

| # | Step | Command | Read |
|---|---|---|---|
| 0 | Keep the build you start from | Build on a clean tree: the after-build check keeps it in `.git/sub0perf/bins/T/`. Or `cp <exe> B/perf/prev.exe` | nothing |
| 1 | Measure where you are | `cmake --build B --target T_check` (it runs `T_runs` first) | one line: `perf: PASS 0 regressed, ...` and a line per row that moved |
| 2 | Change one thing | edit, `cmake --build B --target T` | compiler output only |
| 3 | Compare with the previous build | `cmake --build B --target T_compare` (kept build) or `-DSUB0MATH_COMPARE_AGAINST=B/perf/prev.exe` first. Narrow it: `-DSUB0MATH_COMPARE_FILTER="<name>"` | `perf compare: ...` plus a line per row that moved |
| 4 | Keep or reject | see below | |
| 5 | Record it | from a full steady run: `bench_check.py accept --results B/perf/T.samples.json --baseline perf/baselines/<host>.json --all-changed --reason "<what and why>"`, or `SUB0MATH_PERF_REASON="<why>" cmake --build B --target T_baseline` for the whole baseline | `accepted ...` lines |
| 6 | Commit the baseline with the change | `git add perf/baselines/<host>.json <the sources>` and commit, saying why | |

`T_compare` is an interleaved A/B (at least 10 rounds, which build goes first flips every round) in this session, so a
difference is the code and not the machine. `bench_check.py compare --exe <new> --against <old> --filter <text>` does the
same by hand. REGRESSED there means the new build is slower than the old one.

### Step 4: keep or reject

| `compare` says | Do |
|---|---|
| IMPROVED on the target, nothing REGRESSED | Keep. Go to step 5 |
| Nothing moved (neutral) | Keep only with a reason that is not speed (simpler, smaller, clearer, needed by the next step). Write the reason in the commit and in `--reason`; otherwise reject |
| REGRESSED | Iterate: change again and go back to step 3. The three-pass rule applies ([PERFORMANCE.md](PERFORMANCE.md), "Choosing between alternatives"): an alternative gets three optimisation passes before it is judged or parked. After three passes with no improvement, park the variant (do not revert it silently) and say so |
| a line says `inconclusive` | The machine was noisy or shifted. Run step 3 again, on a quiet machine on mains. Never keep or reject on an inconclusive row |

Before step 5 make sure the measurement is a full steady run on mains: `bench_check.py gate --level full`, or
`T_runs` with the default rounds. A quick record cannot become a baseline, and a run measured on battery or with an
unsteady calibration is refused (`accept` says why and exits 2).

### Deterministic facts first

If the change should alter a size, a count or an iteration number, check those exactly before timing anything:

```bash
ctest --test-dir B -L smoke                                   # T_smoke and T_metrics: milliseconds, never timing
SUB0MATH_PERF_REASON="<why>" cmake --build B --target T_metrics_accept   # a deliberate change of a metric, logged in perf/metrics.json
```

## Reading the output

```text
perf: FAIL 1 regressed, 0 improved, 14 ok, 2 inconclusive; report build/perf/X.report.md
REGRESSED index/cold 1e5: +14.2% (noise 2.1%, tol 10%)
```

The first line is the whole verdict. Open the report only to see a row the lines do not explain. For a machine reader,
`--format json` prints one object (`sub0perf-outcome-1`: `status`, `exit`, `counts`, `changed[]`, `report`, `next_action`), and
`--out-json PATH` (env `SUB0PERF_OUTCOME_JSON`) writes it to a file whatever the format (check, auto, gate, compare, metrics);
`next_action` is the command to run next. Exit status: 0 PASS, 1 FAIL, 2 unusable input (nothing judged).

## Rules that stay

- One change per iteration, so a verdict has one cause.
- Never accept a regression to get a pass; accept only with a real reason in `--reason`.
- Baselines are per host. Re-recording or accepting is logged in the baseline's `accepted` list, and the file is committed with the change.
- `git push --no-verify` and the tooling's bypass variables are for the owner, not for the loop.
