# Plan: extract sub0perf into its own repository (Sub0Perf)

Status: in progress, paused 2026-11-24 after phase 4's PR; see "Hand-over" at the end.
Owner approval is needed where a step says **(owner)**: creating the GitHub repository, every push, issue transfers,
and installing `git-filter-repo`.

## Why

Sub0Math's scope is "value types and their algebra" (AGENTS.md), but it also carries the family's performance tooling:
about 5,700 lines of Python and CMake in `tools/perf/` and `cmake/Sub0MathTooling.cmake`. Three of the four consumers
(Sub0Canvas, Sub0Fabric, Sub0Squad) download Sub0Math only for that tooling, never for the maths. Only Sub0Physics uses
both. Every tooling fix moves Sub0Math's history and every consumer's Sub0Math pin. ADR 0001 decision 5 put the tooling
"at the bottom of the stack". The stack now has Sub0Platform below Sub0Math, and the tooling has outgrown that decision.

## Decisions (made; change only with the owner)

| # | Decision |
|---|---|
| D1 | New repository **CraigHutchinson/Sub0Perf**, public, MIT. It sits above Sub0Platform and below every library that benchmarks. **Not in Sub0Platform:** the family review (Sub0LlmEngine `docs/reviews/2026-11-24-family-responsibilities.md`, D2 and D10) keeps Platform to mechanism only (processes, locks, directories, host facts). sub0perf is policy: statistics, verdicts, baselines, gates and hooks. |
| D2 | **Move first, rename later.** The extraction keeps every public name byte for byte: `sub0math_*` CMake functions, `SUB0MATH_*` cache and environment variables, the `sub0math_tooling_dir.txt` record, the `.git/sub0perf/` state directory, the `bench_check.py` command line and the `sub0perf-*` JSON schemas. Renaming to `sub0perf_*` / `SUB0PERF_*` with aliases is phase 7, a separate change. |
| D3 | **Layout keeps the relative paths** consumers and the `.github` tools already look for: `tools/perf/**` stays `tools/perf/**`, and `bench_runner.hpp` stays at `tools/perf/bench_runner.hpp`, so `#include "bench_runner.hpp"` is unchanged. Only two files move: `cmake/Sub0MathTooling.cmake` becomes `cmake/Sub0Perf.cmake`, and `tests/vendor/nanobench.h` becomes `vendor/nanobench.h` (fix the two `CMAKE_CURRENT_FUNCTION_LIST_DIR` paths and `test_bench_runner.py`'s `VENDOR`). |
| D4 | `sub0math_target_deterministic_fp` **stays in Sub0Math**: it is Sub0Math's determinism contract (det_math, digests), not perf tooling. Move it to `cmake/Sub0MathFp.cmake` in Sub0Math. Sub0Perf's file keeps a copy only behind `if(NOT COMMAND sub0math_target_deterministic_fp)` until phase 7. `sub0math_add_test` and `sub0math_label_full_tests` (test tiers) move to Sub0Perf. |
| D5 | **Transition shim.** Sub0Math keeps a `cmake/Sub0MathTooling.cmake` for one release. It prints a `DEPRECATION` message, does `CPMAddPackage(NAME Sub0Perf ...)` at a pinned SHA if `Sub0Perf_SOURCE_DIR` is not already set, and includes `${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake`. Consumers pinned to older Sub0Math SHAs are unaffected until they bump. Remove the shim once every consumer has moved. |
| D6 | Sub0Math's own benchmarks and gate consume Sub0Perf through CPM at an exact SHA, **only when Sub0Math is the top-level project** (`NOT IS_SUBPROJECT`). A consumer of the maths never fetches Sub0Perf because of Sub0Math. |
| D7 | Out of scope (they stay in Sub0Math for now): `tools/wasm/wasm_gate.py` and `tools/provenance/`. Sub0Physics uses both from Sub0Math. They are family CI tooling and could later move to `.github` `tools/ci`. Record this as an open item in the ADR, nothing more. |
| D8 | Open issues follow the code: Sub0Math issues 4, 5, 6, 7 and 8 transfer to Sub0Perf (`gh issue transfer`) **(owner)**. Issue 3 (macOS runner) stays in Sub0Math. |
| D9 | Issue 8 (parallel benchmark rows) is **designed and built in Sub0Perf after the move**, never during it (D2). Its contract is set now (phase 7 below) so that the extraction does not close it off: rows stay serial unless a row is both **declared** parallel-eligible by its author and **measured** safe on this host. |

## Inventory: what moves

| From Sub0Math | To Sub0Perf | Notes |
|---|---|---|
| `tools/perf/**` (bench_check.py, profile.py, vectorization_report.py, bench_runner.hpp, skill/, sub0perf/ with tests/) | same path | Exclude `__pycache__`. Change the docstring in `sub0perf/__init__.py` ("Sub0Math performance tooling") and `speed.py:105` `REBUILD_ADVICE` ("against a Sub0Math with") to name Sub0Perf |
| `cmake/Sub0MathTooling.cmake` | `cmake/Sub0Perf.cmake` | Header comment: "Consumers include this file from the Sub0Perf source tree (CPM sets Sub0Perf_SOURCE_DIR)" |
| `tests/vendor/nanobench.h` | `vendor/nanobench.h` | Sub0Math keeps its own copy too: `tests/bench_math.cpp` uses it |
| `docs/PERF_LOOP.md` | `docs/PERF_LOOP.md` | |
| `docs/PERFORMANCE.md`, tooling sections | `docs/PERFORMANCE.md` | Moves: "Timing budgets (per host)" with all its subsections (levels, self-test, power state, pre-push gate, tooling layout), "Deterministic metrics", "Output contract", "Improve loop", "Profiling", "Whole-process workload evidence" |
| `docs/PERFORMANCE.md`, the rest | stays in Sub0Math | "Choosing between alternatives", "Codified gates", "Floating-point determinism", "Vectorization and aliasing", "Subnormal numbers", "Findings", "Measured", "Open gaps": these are about Sub0Math's code. Add a link to Sub0Perf's PERFORMANCE.md for the method |
| `.github/workflows/tools-tests.yml` (the tools/perf step) | `.github/workflows/tools-tests.yml` | Sub0Math keeps the provenance and wasm steps |

New in Sub0Perf: `README.md`, `LICENSE.md` (MIT, copy Sub0Math's), `AGENTS.md` and `CLAUDE.md` (scope, "changes here affect every consumer", the commit rule naming affected consumers, generic names only), `CMakeLists.txt` (see phase 2), `CMakePresets.json`, `.ci/local.json` (python-tests, a sample build, private-names, hook-check, selftest), `.gitignore`, and CI callers of the shared reusable workflows, pinned the way Sub0Math's are.

## Phases

Each phase ends green before the next starts. Run commands in Git Bash unless they're shown as PowerShell.

### Phase 0: ADR **(owner)** [done: ADR 0004 Accepted, in .github PR 2]

Write `docs/decisions/0004-sub0perf.md` in the `.github` repository (local clone `D:\Craig\GitHub\CraigHutchinson.github`),
in the format of 0002: Status *Proposed*, Owner, *Supersedes ADR 0001 decision 5*, Context (the Why above), Decision (D1-D9),
Follow-ups (phase 7, issues 4 and 7 against Sub0Platform, issue 8, D7). Add the row to `docs/decisions/README.md`. The owner
accepts it before phase 3 pushes anything.

### Phase 1: create Sub0Perf with history [done]

1. **(owner)** Install `git-filter-repo` (`python -m pip install --user git-filter-repo`).
2. Make a throwaway clone (never filter the working checkout):
   `git clone --no-local D:/Craig/GitHub/Sub0Math D:/Craig/GitHub/Sub0Perf && cd D:/Craig/GitHub/Sub0Perf && git remote remove origin`
3. `git filter-repo --path tools/perf --path cmake/Sub0MathTooling.cmake --path tests/vendor/nanobench.h --path docs/PERF_LOOP.md --path docs/PERFORMANCE.md --path .github/workflows/tools-tests.yml --path-rename cmake/Sub0MathTooling.cmake:cmake/Sub0Perf.cmake --path-rename tests/vendor/nanobench.h:vendor/nanobench.h`
4. Check that `git log --oneline | wc -l` is non-trivial and that `git log --follow tools/perf/sub0perf/gate.py` shows its history.
5. One commit, "Sub0Perf: standalone layout", that fixes the paths in D3, splits PERFORMANCE.md (keep only the moving sections; replace
   Sub0Math-specific examples with generic wording where they no longer make sense), trims tools-tests.yml to tools/perf, and adds the new files.

### Phase 2: Sub0Perf is green on its own [done: Sub0Perf main 3c74bce]

- `CMakeLists.txt`: `project(Sub0Perf VERSION 0.1.0 LANGUAGES CXX)`. It includes `cmake/Sub0Perf.cmake`. When top level, it builds
  `examples/bench_sample.cpp`: two trivial workloads, one a hand-written reference pair, using `BenchRunner` and `metric()`.
  It does that through `sub0math_add_benchmark(Sub0Perf_SampleBench examples/bench_sample.cpp METRICS examples/metrics.json SMOKE_TEST)`,
  so the module is exercised end to end. Hooks off by default in this repository (`SUB0MATH_PERF_GIT_HOOKS OFF`); it has no timing gate.
- Must pass: `python -m unittest discover -s tools/perf` (the bench_runner test compiles against `vendor/nanobench.h`);
  `cmake --preset default && cmake --build --preset default && ctest --preset default` (runs `_smoke` and `_metrics`);
  `python tools/perf/bench_check.py selftest --repo .` (expect WARNs for no config, no FAIL); the `.github` `private_names.py --tree --repo .`.
- **(owner)** Create the GitHub repository (`gh repo create CraigHutchinson/Sub0Perf --public --source . --push`) and confirm CI is green.
  Note the SHA: it is `<SUB0PERF_SHA>` below.

### Phase 3: Sub0Math consumes Sub0Perf (one PR on a topic branch) [done: merged as Sub0Math PR 9, 67ede5a]

1. Delete what moved (`tools/perf/`, the tooling sections of `docs/PERFORMANCE.md`, `docs/PERF_LOOP.md`, the tools/perf step of tools-tests.yml).
2. Add `cmake/Sub0MathFp.cmake` with `sub0math_target_deterministic_fp` (D4), included from `CMakeLists.txt`.
3. Replace `cmake/Sub0MathTooling.cmake` with the shim (D5), pinned to `<SUB0PERF_SHA>`.
4. In `CMakeLists.txt`, include the tooling only when top level (D6), through the same shim path or CPM directly. Sub0Math needs CPM
   for this: use the family's shared CPM bootstrap (0.43.2, ADR 0001 decision 6) the way the consumers do. Check whether
   `tests/CMakeLists.txt` uses `sub0math_add_test` or the label helpers; if so, they come from Sub0Perf.
5. `.ci/local.json`: the selftest becomes `{python} {sub0math_tooling}/bench_check.py selftest --repo .` (check that `local_ci.py` supports that
   placeholder, as `docs/ci-strategy.md` line 237 suggests; otherwise read `build/sub0math_tooling_dir.txt`). Drop `tools/perf` from python-tests.
6. Docs: README ("Sub0Math also hosts the family's performance tooling" becomes a pointer to Sub0Perf), AGENTS.md (the tooling bullet now
   says Sub0Math *uses* Sub0Perf), CLAUDE.md (the pre-push and commit-rule lines name Sub0Perf), docs/CI.md.
7. Must pass: the build and `ctest --preset default`; `Sub0Math_Bench` builds; configuring installs the pre-push hook, and it runs the gate
   from the Sub0Perf checkout; `python <.github>/tools/ci/hook_check.py --repo .` and `local_ci.py --level quick` pass.
   Commit message: no public API change in `include/sub0math/`, and say which consumers the tooling move affects (all four, through the shim).
8. **(owner)** Push and open the PR.

### Phase 4: the `.github` repository (one PR)

- `docs/ci-strategy.md`: "the performance gate (`Sub0Math` tooling)" and its other mentions become Sub0Perf; the reference implementations line gains Sub0Perf.
- `templates/pre-push`: the comments name Sub0Perf (the mechanism, `sub0math_tooling_dir.txt`, is unchanged per D2).
- `tools/ci/ci_power.py`: also find `sub0perf.power` through `<builddir>/sub0math_tooling_dir.txt` (a consumer no longer carries `tools/perf` in-tree),
  and update the duplication note. `tools/ci/hook_check.py`: wording only, unless its lookup assumes Sub0Math.
- ADR 0004 to *Accepted* once phases 2 and 3 are merged.

### Phase 5: consumers, one PR each, in this order

| Repo | Change |
|---|---|
| Sub0Squad (`cmake/dependencies.cmake`) | Replace the Sub0Math `CPMAddPackage` with Sub0Perf at `<SUB0PERF_SHA>`; include `${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake`; fix the comment |
| Sub0Fabric (`CMakeLists.txt` ~59-94) | Same; the comment that cites "Sub0Math issue 5" now cites the transferred Sub0Perf issue |
| Sub0Canvas (`CMakeLists.txt` ~71-105) | Same as Fabric |
| Sub0Physics | Keep the Sub0Math dependency (it uses the maths); add Sub0Perf; `sub0math_target_deterministic_fp` now comes from Sub0Math's `Sub0MathFp.cmake` (bump the Sub0Math pin to the phase 3 merge); add a Sub0Perf entry to `SUB0MATH_PERF_PINS`; `wasm_gate.py` and `upstream_provenance.py` paths unchanged (D7) |

Each must pass its own build, `ctest`, `bench_check.py selftest` and `hook_check.py`. Its pre-push gate must still measure against
the same baseline (the baseline files and `.git/sub0perf/` are untouched). **(owner)** for each push.

### Phase 6: tidy up

- **(owner)** Transfer Sub0Math issues 4, 5, 6, 7 and 8 to Sub0Perf, each with a comment linking the ADR.
- Comment on Sub0Platform issue 2 and Sub0LlmEngine's review (section 8 table) that sub0perf now lives in Sub0Perf.
- Once every consumer has moved: remove the shim from Sub0Math (its own small PR).

### Phase 6b: clean-up after the post-move review (2026-11-24)

A review of the finished groundwork, done once all four consumers had moved, found these. Items 1-4 change no behaviour
and go together as **one Sub0Math PR plus one Sub0Perf PR**. Item 5 is issue bookkeeping. Item 6 changes behaviour, so it
goes in Sub0Perf after this plan (with issue 5).

1. **Sub0Math fetches Sub0Perf directly; delete the shim.** Today `CMakeLists.txt` includes the deprecated
   `cmake/Sub0MathTooling.cmake` with `SUB0MATH_TOOLING_QUIET`. No current consumer uses the shim any more: all four
   fetch Sub0Perf themselves, and an older Sub0Math pin carries its own tooling file. Replace it with a direct
   `CPMAddPackage(NAME Sub0Perf GIT_REPOSITORY ... GIT_TAG 3c74bce... DOWNLOAD_ONLY YES)` plus
   `include("${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake")`, still only when top level (D6). Keep `cmake/get_cpm.cmake`.
   Delete `cmake/Sub0MathTooling.cmake` and the `SUB0MATH_TOOLING_QUIET` flag. Update the mentions in README, AGENTS.md,
   CLAUDE.md and docs/CI.md (grep `Sub0MathTooling`, `shim`).
2. **Delete Sub0Math `tests/vendor/nanobench.h`.** It is unused: `Sub0Math_Bench` gets nanobench from Sub0Perf's `vendor/`
   (through `sub0math_add_benchmark`), and `tests/vendor` is left with `doctest.h` only. Check the bench still builds.
3. **Delete the transition copy of `sub0math_target_deterministic_fp` in Sub0Perf `cmake/Sub0Perf.cmake`** (the
   `if(NOT COMMAND ...)` block, about line 104, and its line in the header comment). No consumer needs it: Sub0Physics gets
   it from Sub0Math's `Sub0MathFp.cmake`, and two definitions mean the first one included silently wins. This replaces D4's
   "until phase 7". Point the header comment and Sub0Perf's docs at Sub0Math for the function. Then bump Sub0Math's pin from
   item 1 to the new Sub0Perf SHA (no consumer pin needs bumping for this).
4. **Plan status:** set the status line to "done (phases 0-6); phase 7 open", mark phases 4-6 done with their PRs, and
   replace the stale hand-over section with a short "Outcome" listing what merged where and what is left (items 6 and 7 below, phase 7).
5. **Issues:** make Sub0Perf issue 3 (machine-wide scheduling) the umbrella. Comment on it linking Sub0Perf issues 4, 7
   and 8 and Sub0Platform issue 2, and comment on 7 and 8 pointing back to 3, so there is one lock design, not three. The
   comment on the private family review (Sub0LlmEngine `docs/reviews/2026-11-24-family-responsibilities.md`, section 8) is still owed:
   add a row saying sub0perf now lives in Sub0Perf and issues 4-8 moved there.
6. **(after this plan, in Sub0Perf, with issue 5)** Sub0Fabric and Sub0Canvas each carry the same ~25-line CMake block:
   reading the host from `SUB0MATH_PERF_HOST` (cache or environment), installing the gate only when
   `perf/baselines/<host>.json` exists, and never from a linked worktree. That is gate policy, so move it into
   `Sub0Perf.cmake` as a new default `SUB0MATH_PERF_GIT_HOOKS=AUTO` (off on CI, as now). Then each consumer keeps only the
   `CPMAddPackage` and the `include`. This also fixes issue 5's worktree cases in Sub0Perf itself. File it as a comment on
   Sub0Perf issue 5 now; build it as a separate change.

Must pass: Sub0Math `cmake --preset default`, build, `ctest --preset default`, `Sub0Math_Bench` builds, `hook_check.py --repo .`,
`local_ci.py --level quick`. Sub0Perf: python tests, build, `ctest`, selftest. Sub0Physics still configures against the new
Sub0Math (`CPM_Sub0Math_SOURCE=D:/Craig/GitHub/Sub0Math`), with no pin change needed.

### Phase 7: later, separate changes (not part of this plan's execution)

- Rename to `sub0perf_*` functions and `SUB0PERF_*` variables, reading the old names as fallbacks for one release. Same for `sub0perf_tooling_dir.txt`.
- Parallel benchmark rows (issue 8; the goal is shorter gate and compare runs, and correctness comes first). Contract:
  1. **Correctness before timing.** A row's deterministic metrics and smoke run (`--smoke`, `BenchRunner::metric`) pass before any of
     its timings are taken, serial or parallel. A row that fails them is reported and never timed.
  2. **Declared eligibility, default serial.** The author opts a row in: `BenchRunner` gets a per-row flag (for example
     `.parallel(maxWidth)`), and `sub0math_add_benchmark` gets `PARALLEL <k>` as a per-executable ceiling (0 = never). Rows that
     use threads, large shared state, the GPU, files or the network stay serial by not opting in. Nothing becomes parallel by default.
  3. **Measured safety per host.** Topology probe (performance and efficiency cores, SMT siblings never paired; Sub0Platform `topology`
     once it lands, issue 4), then interference calibration per eligible row: alone versus with k-1 copies pinned to other cores. The
     safe width is the largest k whose median shift stays under a third of the gate tolerance and whose spread does not widen. Verdicts go
     in a host profile beside `perf/baselines/<host>.json`, keyed by host, power state and toolchain. A key mismatch means serial
     until re-calibrated.
  4. **Scheduling and guardrail.** `gate`, `auto` and `compare` run safe rows concurrently, each process pinned to its own cores, and
     the rest serially. On first use, and on a sample afterwards, cross-check against a serial run; a mismatch turns parallel mode off
     and reports it. The whole run holds the machine lease (issue 7), because parallel rows make the machine even less shareable.
  5. Baselines remain serial measurements; parallel runs are judged against them only through the calibrated shift, never recorded as baselines.
- Fix Sub0Perf issues 5 and 6, then 4 and 7 against Sub0Platform (`platform_proc.py`, the machine lease). Move the power and clock readings to
  Sub0Platform's Python `host` module.

## Rules for the executor

- Public repositories never name private ones: run the `.github` `tools/ci/private_names.py` on Sub0Perf and on each PR. Use generic examples only.
- Never commit to `main` directly; topic branch and PR per repository. Never push, create a repository or transfer an issue without the owner.
- Don't change behaviour during the move (D2). If a test needs a behaviour change to pass, stop and report it.
- Sub0Squad, Sub0Fabric and Sub0Canvas may have other sessions working in them: check `git status` and the current branch before editing, and use a worktree if they're busy.

## Hand-over (paused 2026-11-24)

Done
- Phase 1-2: CraigHutchinson/Sub0Perf exists with history; main is `3c74bce22e848d3e4e13dfcb5eb9c480703b7db8` (PR 1 merged: Release presets,
  chmod in `DirectGateWiringTest`). CI green (smoke, tools-tests, full tier by dispatch). `<SUB0PERF_SHA>` is that SHA.
- Phase 3: Sub0Math PR 9 merged (`67ede5a`). The shim pins `3c74bce`. The full tier's macOS leg fails on the unsupported runner (issue 3);
  the merge used `ci:local-verified` plus `local-ci/full`, with the owner's explicit yes.
- Phase 4: `.github` PR 2 (branch `sub0perf-adr-0004`): ADR 0004 (Accepted), ci-strategy.md, templates/pre-push, `ci_power.py` finding
  `sub0perf.power` through `sub0math_tooling_dir.txt`. Pushed, **not merged**; `tools/ci` tests pass (139).

Left, in order
1. Merge `.github` PR 2 (consumers' `local_ci.py`/`hook_check.py` come from this repository).
2. Phase 5, one PR per repository, each checked for `git status`/branch first (other sessions): Sub0Squad (`cmake/dependencies.cmake`,
   replace the Sub0Math CPM entry by `CPMAddPackage(NAME Sub0Perf ... GIT_TAG 3c74bce...)` with `DOWNLOAD_ONLY YES` and include
   `${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake`; fix the comment), Sub0Fabric (`CMakeLists.txt` ~59-94; its comment citing "Sub0Math issue 5"
   cites the transferred Sub0Perf issue), Sub0Canvas (~71-105), Sub0Physics (keep Sub0Math, bump its pin to `67ede5a`, add Sub0Perf, add a
   Sub0Perf entry to `SUB0MATH_PERF_PINS`; `sub0math_target_deterministic_fp` now comes from Sub0Math's `Sub0MathFp.cmake`). Each must pass its
   build, `ctest`, `bench_check.py selftest` and `hook_check.py`, with the baseline files and `.git/sub0perf/` untouched.
   Learned: the consumer's `.ci/local.json` `"selftest"` has no `{sub0math_tooling}` placeholder (hook_check expands only `{python} {repo} {ci_dir}`);
   Sub0Math reads `build/sub0math_tooling_dir.txt` in a `python -c` one-liner instead.
3. Phase 6: transfer Sub0Math issues 4, 5, 6, 7 and 8 to Sub0Perf (`gh issue transfer`, **owner**), each with a comment linking ADR 0004;
   comment on Sub0Platform issue 2 and the family responsibilities review; once every consumer has moved, remove the shim (own PR).
4. Sub0Perf issue 2 (gate: a docs-only push of a *new* branch should skip measuring; the owner asked for it on 2026-11-24). Existing
   behaviour: `documentation_only` skips docs-only ranges but treats a new branch (zero remote sha) as unknown and measures. A behaviour
   change, so after the move (D2). Until then a docs-only first push of a branch needs `SUB0MATH_PERF_SKIP=<reason>` on a noisy machine.
5. Phase 7 stays out of scope: rename, and issue 8's contract (above).

Notes for the next session
- The Sub0Perf selftest needs the pre-push hook and a baseline: Sub0Perf's configure installs the hook (off on CI) and
  `perf/baselines/<host>.json` holds a sample baseline for the recording host (`croglegion`).
- `D:\Craig\GitHub\Sub0Perf` is on `main` (clean); Sub0Math's `main` includes the merge. The `.github` clone is on branch `sub0perf-adr-0004`.
- Open items for the ADR follow-ups: D7 (wasm_gate.py, tools/provenance), phase 7.
