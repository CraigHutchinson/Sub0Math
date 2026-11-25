# Plan: extract sub0perf into its own repository (Sub0Perf)

Status (2026-11-25): phases 0-6 and 6b done. Phase 7: 7.1-7.4 done (7.4's fallbacks still to drop), 7.5 in progress, 7.6 and 7.7 not
started. The work still to do is consolidated in "Next iteration" near the end of this file; "Outcome" records what merged.
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

### Phase 4: the `.github` repository (one PR) [done]

- `docs/ci-strategy.md`: "the performance gate (`Sub0Math` tooling)" and its other mentions become Sub0Perf; the reference implementations line gains Sub0Perf.
- `templates/pre-push`: the comments name Sub0Perf (the mechanism, `sub0math_tooling_dir.txt`, is unchanged per D2).
- `tools/ci/ci_power.py`: also find `sub0perf.power` through `<builddir>/sub0math_tooling_dir.txt` (a consumer no longer carries `tools/perf` in-tree),
  and update the duplication note. `tools/ci/hook_check.py`: wording only, unless its lookup assumes Sub0Math.
- ADR 0004 to *Accepted* once phases 2 and 3 are merged.

### Phase 5: consumers, one PR each, in this order [done]

| Repo | Change |
|---|---|
| Sub0Squad (`cmake/dependencies.cmake`) | Replace the Sub0Math `CPMAddPackage` with Sub0Perf at `<SUB0PERF_SHA>`; include `${Sub0Perf_SOURCE_DIR}/cmake/Sub0Perf.cmake`; fix the comment |
| Sub0Fabric (`CMakeLists.txt` ~59-94) | Same; the comment that cites "Sub0Math issue 5" now cites the transferred Sub0Perf issue |
| Sub0Canvas (`CMakeLists.txt` ~71-105) | Same as Fabric |
| Sub0Physics | Keep the Sub0Math dependency (it uses the maths); add Sub0Perf; `sub0math_target_deterministic_fp` now comes from Sub0Math's `Sub0MathFp.cmake` (bump the Sub0Math pin to the phase 3 merge); add a Sub0Perf entry to `SUB0MATH_PERF_PINS`; `wasm_gate.py` and `upstream_provenance.py` paths unchanged (D7) |

Each must pass its own build, `ctest`, `bench_check.py selftest` and `hook_check.py`. Its pre-push gate must still measure against
the same baseline (the baseline files and `.git/sub0perf/` are untouched). **(owner)** for each push.

### Phase 6: tidy up [done]

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

### Phase 7: fixes and follow-ups in Sub0Perf (planned 2026-11-24 from the open issues)

Work in Sub0Perf unless a step names another repository. One PR per numbered item, in this order. Each item is a behaviour
change, so it carries a test and a `selftest` case where the selftest covers that area, and its commit names the consumers it
affects. Items 7.1-7.3 need no design and can start now. 7.4 waits for 7.1-7.3, so fixes aren't rebased across the rename.
7.6 waits for its ADR.

Issue coverage (every open issue reported against the tooling, as of 2026-11-24):

| Issue | What is still wrong in the code (Sub0Perf main `10466f8`) | Item |
|---|---|---|
| Sub0Perf 5: linked worktrees | `SUB0MATH_PERF_GIT_HOOKS=AUTO` (PR 10) stops a *CMake* configure in a linked worktree from installing. Still open: `hooks.is_tracked_dir` treats an absolute `core.hooksPath` into the main checkout as untracked, so an explicit `install-hooks` or `ON` from a linked worktree still overwrites committed hooks. Every build directory shares `.git/sub0perf/config.json`, so a second build directory in the main checkout (local_ci's `build-local-*`) repoints the gate, which later fails with "building failed". A hook-time configure with `GIT_DIR` set can write `core.bare=true` and an absolute `core.hooksPath` into the shared git config. Tests that make temporary repositories inherit the hook's `GIT_DIR`. | 7.1 |
| Sub0Perf 6: compare silent and slow; a gate row stalled for 70 min | No progress output, no overall budget, no per-process wall-clock cap, and a benchmark's one-shot setup is redone in every process round. | 7.2 |
| Sub0Perf 2: docs-only push of a new branch | `gitstate.documentation_only` returns False for a zero remote sha. | 7.3 |
| (plan D2) rename | The `sub0math_*` / `SUB0MATH_*` names remain. | 7.4 |
| Sub0Perf 4, first box: processes from Sub0Platform | `platform_proc.py` is still a local copy. | 7.5 |
| Sub0Perf 3 (umbrella), 7 (machine lease), 8 (parallel rows), 4 second box (topology); Sub0Platform 2 | Design not settled; only per-repository `PerfLock` exists. | 7.6 |
| Sub0Canvas 7, item 1: a heap-byte metric differs under clang | A metrics spec has one expectation per metric, with no per-toolchain form. | 7.7 |

**7.1 Linked worktrees and extra build directories (issue 5).** [done: Sub0Perf PR 11]
- `is_tracked_dir`: a hooks directory is tracked when it lies inside *any* worktree of the repository (the main worktree is
  the parent of `git rev-parse --path-format=absolute --git-common-dir`; or use `git worktree list --porcelain`) and outside the common dir.
- From a linked worktree, `install-hooks` (any mode) and any configure-time install refuse with one line: install from the
  main checkout. They never write the common-dir git config or `.git/sub0perf/config.json`.
- The config records the build directory that wrote it. A configure from another build directory in the same checkout
  leaves the config alone while the recorded build directory still exists, and prints one line saying which one the gate uses.
  `ON` still takes it over on purpose.
- The gate refuses in one line, naming the fix ("reconfigure <build dir>"), when the configured executable or build directory is
  missing or lies outside the current worktree. Today it reports "building failed" or measures another worktree's binary.
- `tests/helpers.py` unsets `GIT_DIR`, `GIT_WORK_TREE` and `GIT_INDEX_FILE` for every temporary repository (the fix Sub0Canvas made locally).
- Tests: a temporary repository with a linked worktree covers each case above. Comment on issue 5 and close it.

**7.2 Bounded, visible runs (issue 6).** [done: Sub0Perf PR 12; the empty-body stall is bounded by the wall-clock cap, not a nanobench limit]
- `compare`, `gate` and `auto` print one progress line per round on stderr in every output format, giving round n/N, rows and ETA.
  stdout stays the output contract.
- `compare --budget SECONDS` (default 300). Without `--filter`, when the planned run (from the probe costs) exceeds it, refuse with the
  estimate and the `--filter` / `--budget` to use.
- A hard wall-clock cap per benchmark process (`platform_proc.launch(timeout=...)`): at the quick level, a tenth of the budget
  or 3x the row's recorded cost, whichever is larger. A row that hits it is killed and gets verdict `TIMEOUT`. `TIMEOUT` is listed
  in the outcome JSON `changed[]`, never blocks, and gives the row's command as `next_action`.
- `BenchRunner` gains a per-process one-shot setup (for example `runner.setup("corpus 1e5", fn)`), built once per process and not
  timed, so a large corpus is no longer rebuilt every round. This is an API addition in `bench_runner.hpp`; existing rows are unchanged.
- Also guard against a row whose timed body is empty (about 1 ns, iteration scaling never converges; the 70-minute stall). nanobench's
  epoch iterations get an upper bound, and such a row reports `TIMEOUT` instead of stalling.

**7.3 Docs-only new branch (issue 2).** [done: Sub0Perf PR 13] For a zero remote sha, diff against the merge-base with the branch's upstream,
else the remote's default branch (`refs/remotes/<remote>/HEAD`). If neither resolves, keep measuring. Add the selftest
"docs-only range" case and a unit test with a temporary repository.

**7.4 Rename (D2's deferred half).** [done except dropping the fallbacks: Sub0Perf PR 14, .github PR 3, consumers Sub0Math 13, Squad 10, Fabric 6, Canvas 9, Physics 5; the fallbacks (envnames.py, old CMake names, old record) go one release later] Add `sub0perf_*` functions and `SUB0PERF_*` cache and environment variables. Read the old
names as fallbacks for one release, with a `DEPRECATION` message for the CMake functions only, so environment variables stay
quiet. Write both `sub0perf_tooling_dir.txt` and `sub0math_tooling_dir.txt` for that release. Update `.github` `ci_power.py`,
`hook_check.py`, `templates/pre-push` and `docs/ci-strategy.md` to accept either name. Then move each consumer (Sub0Math,
Squad, Fabric, Canvas, Physics) in its own PR, then drop the fallbacks.

**7.5 Processes from Sub0Platform (issue 4, first box).** [done except the host module: Sub0Platform PR 3 (`proc.launch`, 0.2.0, tag `v0.2.0`), Sub0Perf PRs 15 (merge `e23371b`: CPM at the tag `v0.2.0`, no build-record scan, `CPM_Sub0Platform_SOURCE` is the local override); the power and clock readings wait for Platform's `host` module, tracked on Sub0Perf issue 4 and Sub0Platform issue 1] Replace `platform_proc.py` with the Python `sub0platform` package's
`proc` (`spawnDetached`, `alive`, `terminate`, `cpuSeconds`). Keep `launch(timeout)` from 7.2: add it to Sub0Platform first
if `proc` lacks it. **(owner decision)** How the tooling finds the package. Recommendation: Sub0Perf fetches Sub0Platform with CPM
`DOWNLOAD_ONLY` at a pinned SHA, and the tooling record gains its `python/` path, which `bench_check.py` puts on `sys.path`. That
keeps the tooling standard-library only, with nothing pip-installed. Also move the power and clock readings (`power.py`, `speed.py`) to
Sub0Platform's planned Python `host` module, and drop the duplicate `.github` `tools/ci/ci_power.py` once it can import that.

**7.6 Machine-wide scheduling (issue 3, then 7, 8 and topology).**
1. Write the interface from issue 3 as an ADR in `.github` **(owner)**. Job classes (`shared`, `parallel-eligible`, `exclusive`,
   `hold-system`) are data, and resources are cores, memory bandwidth, GPU and power state. A blocked job reports who holds what
   (a `blocked` object in `sub0perf-outcome-1`). Default is fail-fast for the gate, with `--wait`. Correctness runs never need a lease.
   It must reconcile with Sub0LlmEngine's `supervision/machine_lease` record (`pid`, `time`, `what`, `tool`) and Sub0Platform issue 2, so there is one lock.
2. Sub0Platform issue 2: C++ state directory, shared lock mode, and the `lease` module in Python and C++.
3. Issue 7: `gate`, `auto` and workload runs take the lease (`exclusive`), with `PerfLock` kept inside it. Prototype the `blocked`
   report first: it helps today's single lock.
4. Issue 4, second box: Sub0Platform `topology` (performance and efficiency cores, SMT siblings).
5. Issue 8: parallel rows as the `parallel-eligible` class, under this contract:
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
   Item 1 (correctness before timing) needs nothing from steps 1-4. It may ship first, on its own, as a quick win that also saves time.
6. Consumers adopt the lease: Sub0Squad issue 8 (its host guard), Sub0LlmEngine (fit, bench, serve), Sub0Canvas timing.

**7.7 Toolchain-dependent metrics (Sub0Canvas issue 7, item 1).** First find out whether the clang difference in
`live heap bytes` is real: a different standard library allocates differently, so the metric is genuinely per toolchain.
If it is, let a metric entry carry per-toolchain values (for example `"by_toolchain": {"clang": {"limit": ...}}`, matched on the
compiler id the executable reports). `metrics accept` then edits only the running toolchain's value. If it is a bug, fix it in Sub0Canvas
and change nothing here.

Reported with the migration but not Sub0Perf's to fix (listed so nothing is lost):

| Issue | Home |
|---|---|
| Sub0Math 3, Sub0Physics 4 item 2: macOS runner unsupported | `.github` `cpp-full.yml`: a runner input or a supported label, then re-run both full tiers |
| Sub0Fabric 4 item 2, Sub0Canvas 7 item 2: Windows clang `-Werror` on `strerror` in Sub0Platform's `file.cpp` blocks `local-ci/full` | Sub0Platform (use `strerror_s` on Windows), or mark the step optional in each `.ci/local.json` |
| Sub0Fabric 4 items 1, 3, 4; Sub0Canvas 7 item 3: missing `ci:*` labels; no scheduled full runs; red full history | `.github` ci-strategy: `hook_check.py` (or a new check) verifies that the labels the policy reads exist and that the weekly full run is enabled |
| Sub0Physics 4 item 1: MSVC C3615 in `AxialSpringDamper::create` | Sub0Physics |
| Sub0Canvas 7 items 1 (`test_consumer_*`) and 4 | Sub0Canvas |
| Sub0LlmEngine 7: `tests` red on main | Sub0LlmEngine |

## Next iteration (consolidated 2026-11-25)

Progress review: Sub0Perf issues 2, 5 and 6 are closed by 7.3, 7.1 and 7.2 (Sub0Perf PRs 11-13). The rename (7.4) is merged everywhere
(Sub0Perf 14, `.github` 3, Sub0Math 13, Squad 10, Fabric 6, Canvas 9, Physics 5), and every consumer pins Sub0Perf `3518ede`.
Still open: Sub0Perf 3, 4, 7 and 8. Work in this order; one PR per item, with the same rules as before.

**N1. Finish 7.5 (processes from Sub0Platform), with three corrections to the work in progress.** [corrections done: Sub0Perf PR 15, `e23371b`, pinned at the tag `v0.2.0` as the owner chose; consumer pin bumps in progress]
- **Pin Sub0Platform exactly.** The branch sets `SUB0PERF_PLATFORM_TAG` to `main`. Every other family dependency is pinned to an exact
  commit (ADR 0001 decision 6; Sub0Physics' pin guard), and a gate that measures against a moving dependency can't be reproduced.
  Pin `v0.2.0` (the first release with `proc.launch`), or its SHA. Fail at configure if a reused `Sub0Platform_SOURCE_DIR` lacks `proc.launch`:
  Sub0Squad fetches its own Sub0Platform pin and it is reused, so check it rather than assume it.
- **Find the package in exactly two places, then stop.** `platform_path.py` currently tries four places, the last a scan of
  `build*/` directories newest first. That scan is the same guess issue 5 removed: it can pick another build directory's record.
  Keep "already importable", `SUB0PERF_PLATFORM_DIR`, and the `platform_python` entry `install-hooks` writes to the sub0perf config. If none
  resolves, fail in one line, naming the configure step to run.
- **One fetch path.** Drop the `FetchContent` fallback. Sub0Perf requires CPM like every consumer: include a `get_cpm.cmake` when
  `CPMAddPackage` is missing, as Sub0Math does.
- Then: commit, PR, and merge on green. Bump Sub0Perf in the five consumers (one PR each). Close the first box of issue 4.
- Move `power.py`/`speed.py` to Sub0Platform's Python `host` module **only once that module exists**. It is Sub0Platform issue 1's
  work, not Sub0Perf's, so that part leaves 7.5: track it as a comment on issue 4 and on Sub0Platform issue 1.

**N2. Releases, so that "one release later" means something.** Sub0Perf has no tags yet. After N1, tag `v0.2.0` (the rename and the
Sub0Platform processes), note it in README, and move consumers to pin the tag's SHA. Then drop 7.4's fallbacks (`envnames.py`, the old
CMake names, `sub0math_tooling_dir.txt`) in `v0.3.0`, with `.github` `ci_power.py`, `hook_check.py` and the template accepting only the
new names in the same wave. Check that no consumer still uses an old name first (grep `SUB0MATH_PERF_\|sub0math_add_benchmark\|sub0math_tooling_dir`).

**N3. 7.7: investigate the clang heap-byte metric (Sub0Canvas issue 7, item 1).** Read-only first. It can run alongside N1. Report
whether the difference is the standard library's allocation (then build per-toolchain metric values) or a Canvas bug (then fix it in Canvas).

**N4. 7.6 step 5 item 1: correctness before timing.** `gate`, `auto` and `compare` run the smoke and metrics checks first and
never time a row that fails them. Needs no lease or ADR, and saves time on every push. Ship after N1.

**N5. 7.6 steps 1-4: the scheduling ADR (owner).** Draft the ADR in `.github` from issue 3 and stop for the owner. Nothing in
7.6 steps 2-6 or issue 8's parallel rows starts before it is accepted.

Not Sub0Perf's work, unchanged: the table under phase 7 ("Reported with the migration but not Sub0Perf's to fix").

## Rules for the executor

- Public repositories never name private ones: run the `.github` `tools/ci/private_names.py` on Sub0Perf and on each PR. Use generic examples only.
- Never commit to `main` directly; topic branch and PR per repository. Never push, create a repository or transfer an issue without the owner.
- Don't change behaviour during the move (D2). If a test needs a behaviour change to pass, stop and report it.
- Sub0Squad, Sub0Fabric and Sub0Canvas may have other sessions working in them: check `git status` and the current branch before editing, and use a worktree if they're busy.

## Outcome (2026-11-24)

Merged: Sub0Perf (main, history kept; PRs 1 and 9), `.github` PR 2 (ADR 0004, Accepted), Sub0Math PRs 9, 10 and the phase 6b clean-up,
Sub0Squad PR 9, Sub0Fabric PR 3, Sub0Canvas PR 6, Sub0Physics PR 3. Sub0Math fetches Sub0Perf directly (top level only); the shim and
the unused `tests/vendor/nanobench.h` are gone; Sub0Perf no longer carries a copy of `sub0math_target_deterministic_fp`.
Issues: Sub0Math 4-8 moved to Sub0Perf as 4-8; Sub0Perf 2 (docs-only push of a new branch) and 3 (machine-wide scheduling, the umbrella for
4, 7 and 8); CI-adoption gaps in Sub0Physics 4, Sub0Fabric 4 and Sub0Canvas 7 (Canvas PR 6 merged with `ci/policy` red, by the owner's decision).

Item 6 is done too: `SUB0MATH_PERF_GIT_HOOKS=AUTO` (Sub0Perf PR 10, main `10466f8`), with the Sub0Fabric (PR 5) and Sub0Canvas (PR 8)
blocks removed. Phase 7 so far (2026-11-25): Sub0Perf PRs 11-14 (7.1-7.4; issues 2, 5 and 6 closed), `.github` PR 3, consumer rename PRs
Sub0Math 13, Squad 10, Fabric 6, Canvas 9, Physics 5, and Sub0Platform PR 3 (`proc.launch`, 0.2.0). Left: "Next iteration" N1-N5. The wasm
gate and provenance tools stay in Sub0Math (D7).
