# Performance

Sub0Math is the bottom of the Sub0 stack, so it hosts the family's performance tooling:
`cmake/Sub0MathTooling.cmake` and `tools/perf/`. Every library that depends on Sub0Math (Sub0Physics,
BikeDynamics, ...) gets the same benchmark runner, budgets and profiler targets:

```cmake
include("${Sub0Math_SOURCE_DIR}/cmake/Sub0MathTooling.cmake")   # after CPMAddPackage(Sub0Math ...)
sub0math_target_perf_options(MyLibrary)                       # profile / vectorization / native options
sub0math_add_benchmark(MyLibrary_Bench bench_my_library.cpp)    # plus _json/_check/_baseline/_vtune_*/...
```

Configuring writes the scripts' location to `<build>/sub0math_tooling_dir.txt`, so shells and CI can run
them without knowing where CPM put Sub0Math. Benchmarks include `<bench_runner.hpp>` and accept `--filter` (case-insensitive substring), `--json`,
`--soak <seconds>` (loop under a profiler) and `--list`.

## Choosing between alternatives

- **Three passes before judging.** A first implementation's number belongs to that implementation, not
  to the mechanism. Give an alternative three optimisation passes before comparing or parking it. Break-even
  or better: continue. Park only after three passes. Park a variant rather than reverting it, so a later
  change in context can revive it. This is the rule in
  [Sub0Llm's optimisation process](https://github.com/CraigHutchinson/Sub0Llm/blob/main/docs/OPTIMIZATION_PROCESS.md),
  adopted here.
- **Keep A/B paths open where they differ in kind,** not just in tuning. A uniform, matrix-shaped
  formulation can do more arithmetic than a structured one, yet map better onto the hardware: SIMD lanes,
  batching many instances (bikes, contacts), GPU-style throughput and fixed shapes. The structured form (a
  Schur complement, a tree sweep) wins on one scalar thread. Which wins depends on the deployment, so keep
  both behind one interface, measure each on the consumer scenario and on the batched shape it would
  scale to, and record the choice per target rather than deleting the loser.
- **Exception:** decide early only when the outcome is clear without further passes, for example an
  order-of-magnitude asymptotic difference at the sizes that matter.

## Codified gates

| Gate | Where | Contract |
|---|---|---|
| Zero size overhead | `static_assert`s in the tests | Quantities, roles, vectors and matrices are exactly the size of their numbers and trivially copyable |
| Zero time overhead | Paired benchmarks | `dot, f64::Length` and `x += v*dt, f64::Length` match their plain-double twins. Segment, triangle closest point and ray-triangle on `f64::Length` match their hand-written scalar kernels, within noise. Spatial transforms and inertia changes of frame match or beat hand-written scalar code. Box against blocks: see the open gaps |
| Wrong code does not compile | Concept checks in the tests | Dimension mismatches, swapped or unnamed roles, silent narrowing, point + point |
| Header self-containment | `Sub0Math_HeaderCheck` | Every public header compiles alone under the strict warning set |

### The hand-written reference is the fastest version

In each pair, the hand-written side is the fastest code we know how to write for that job, including
any shortcut the job allows (for example, block centres taken straight from a grid index). It is never
weakened to give the vocabulary an equal footing. Where the vocabulary cannot match it:

1. Iterate on the implementation.
2. If a gap remains, change the API's shape so callers can close it (an entry point taking the data the
   fast path needs, a prepared object that hoists invariant work).
3. If it still remains, record it under [Open gaps](#open-gaps) with its measured size and a focused
   follow-up.

## Timing budgets (per host)

```bash
cmake --build --preset default --target Sub0Math_Bench_check      # interleaved rounds; fails on a regression
cmake --build --preset default --target Sub0Math_Bench_baseline   # record this host's baseline from them
```

**Implicit after every rebuild.** A benchmark executable relinks only when its code, or a header it
uses, has changed. When it does, a quick check runs straight after the build (3 rounds at the
same epochs as baselines, so absolute times compare like for like; about 35 s here) and prints regressions and progressions. `SUB0MATH_PERF_ON_BUILD` controls this:

| Value | Effect |
|---|---|
| `REPORT` (default) | Report only |
| `ENFORCE` | A regression fails the build |
| `OFF` | No check (CI uses this; its own job runs the full rounds) |

**How a measurement is made robust** (`tools/perf/bench_check.py`, implemented in the `sub0perf` package, see [Tooling layout](#tooling-layout)):

| Threat | Countermeasure |
|---|---|
| Thermal and turbo drift during a run | **Interleaved rounds:** every benchmark once per round, each in its own process, in an order shuffled per round (seeded). Drift spreads over all benchmarks instead of biasing the last ones |
| Clock speed varying within and between runs | **Calibration:** a fixed dependent arithmetic chain (`--calibration`) is timed at the start and end of each round. Each sample is scaled by the clock interpolated at its position in the round. Baselines record calibration, so a slower session is corrected before judging. Measured here: the core runs about 3% slower at the end of a round than at its start |
| Environment changes the clock does not show (cache and memory contention, other load) | **Controls:** hand-written references do not use the library, so whatever moves them is the environment. After calibration, their median change is factored out, and a shift beyond half the tolerance is reported. Seen: a commit touching only Python and docs was blocked because every benchmark, the references included, ran 7–28% slower while the clock moved 4.5%. The controls showed +13.3%, and with that factored out nothing regressed, while an injected +30% on one library benchmark is still caught. Repositories without hand-written references get calibration only, and the report says so |
| Environment shifted too far to judge absolute times | When the controls move by more than the tolerance beyond the clock, absolute verdicts become **ENVIRONMENT** (inconclusive, never blocking). Pair ratios and instruction counts are relative to the same environment and still judge. Seen: a +41% shift while another process ran benchmark rounds |
| Core migration and preemption | Each measurement is pinned to one logical CPU (`--cpu`, default the last) at raised priority. Spreads fell from a 2.7% median and 17.6% worst to 1.1% and 7.6%, and calibration from 0.6% to 0.1% |
| Other load on the machine | **Load check:** CPU time used by other processes is sampled before each round. Above `--max-load` (20%) the run waits up to `--settle` seconds, then records the round as busy. The report states how many rounds ran busy |
| Two timing levels from process to process | **Bimodal detection:** a benchmark whose samples form two clusters (largest gap at least 2% of the median and four times the spread inside the clusters, at least two samples each, from five rounds) is checked level by level. A regression needs the *fast* level to be slower than the baseline by more than the threshold, an improvement the slow level faster; if only one level moved, the verdict is **NOISY** (marked `bimodal` in the report), which the gate retries with more rounds and never blocks on. Both levels moving is a real change and keeps its verdict. Pair ratios get the same treatment. See the 2026-10-20 finding for the evidence. Hardening (2026-10-21): a benchmark that is bimodal now or in the baseline is never used as an environment **control** (its level change would be "corrected" into every other benchmark); a baseline stores `modes` `[low, high]` for bimodal benchmarks and pairs, and a shift counts only if it clears the baseline's *slow* level (regression) or *fast* level (improvement), at the clock with and without the controls' correction; the report says `bimodal, both levels moved` when a bimodal row keeps REGRESSED/IMPROVED. The test needs 5 measured samples and 2 per side, so a confirmation or retry pass always gathers at least 10 measured samples (`stats.MODES_MIN_SAMPLES`) for what it re-measures, and the gate measures benchmarks the baseline knows to be bimodal that long from the start. **Cost of the rule:** a real regression smaller than the gap between the two levels can be hidden. With the box group's levels about 18% apart, a uniform slowdown of up to about 15% can read as one level moving. A bimodal benchmark is therefore weak evidence, and changes in its code should be judged by an explicit A/B or by its pair ratio, not by the gate alone |
| Noise mistaken for change | **Variance:** a robust spread (median absolute deviation) per benchmark across rounds. A change must exceed both the tolerance (10%) and 3x the noise. Spread above `--max-cv` (5%) is **NOISY**: the quick check adds rounds for those benchmarks (up to double) and then reports them as inconclusive, never as passed or failed |
| Clock-dependent time, and changes too small for timing noise | **Hardware counters, where the OS exposes them** (Linux perf_event on bare metal; not Windows, WSL2 or GitHub's hosted runners, which are virtual machines without the processor's counters). The report and checks use them per op: instructions are nearly deterministic, so they are judged at a 2% tolerance (`--instr-tolerance`) and catch added work that timing noise hides; cycles do not depend on clock speed; IPC and branch-miss rate are shown. Pairs add the instruction ratio against the hand-written reference. A counter regression blocks like a time regression, and `accept` logs counter changes too. Elsewhere the report says counters are unavailable |
| Host-dependent absolute times | **Pairs:** each hand-written reference's group is reported as a ratio taken from adjacent measurements in the same round. This is nearly independent of the host, so CI and new machines get it without a baseline |

**Verdicts, reported implicitly in every check:** REGRESSED, IMPROVED (a progression: record it with
`_baseline` and say why in the commit), ok, NOISY, new and missing, for benchmarks and for pair ratios
(pair tolerance 5%).

### Pre-push gate and explicit acceptance

Configuring a repository that builds benchmarks installs two git hooks, unless on CI or
`SUB0MATH_PERF_GIT_HOOKS=OFF`. A hook that is not ours is never overwritten: it is renamed to
`<hook>.user` and the installed hook runs it first (with the same arguments and stdin; if it fails, so
does the push). Paths in the hooks are shell-quoted, and when python or the tooling directory is gone
a hook prints a message and does nothing instead of failing.

- **post-checkout** notes the commit. The first clean build afterwards keeps its measurement as that
  commit's record: `.git/sub0perf/records/<target>/<commit>.json`, local to the clone and never
  committed. So every commit measured on a clean tree has a record of where a branch started.
- **pre-push** builds each benchmark target, measures HEAD (3 interleaved rounds, plus extra rounds
  for noisy benchmarks) and keeps that as HEAD's record. It then compares against:
  - the committed baseline;
  - the record of the upstream commit, or of the nearest earlier commit with one, which is like for
    like on this machine.

  Any regression against the baseline that persists blocks the push, with the exact command to accept it.

  **A difference from the local record blocks only if it reproduces.** The record was measured in an
  earlier session, and a difference from it was twice not the code (BikeDynamics "Suspension fork 4 s"
  and Sub0Physics "planar tree 10-DOF forward dynamics": REGRESSED against the record, identical
  times when the two builds were measured alternately). Likely causes, all properties of the *session*
  and none removable by scaling: the clock and power state, other load and thermal state at the time
  of the record (the calibration chain tracks the core clock but not cache and memory behaviour, which
  the dynamics benchmarks are bound by), a bimodal benchmark landing on a different level per process,
  and few rounds on either side. So each record keeps the benchmark executable that made it
  (`.git/sub0perf/bins/<target>/<commit>/`, newest 3 kept). A regression against the record is
  then confirmed by measuring that kept build and the new one alternately (order swapped each round, at
  least 10 rounds, the regressed groups only) and judging the new build against the *fresh* measurement
  of the old: only names that still regress block. Names that do not are reported as `not reproduced`
  (a WARNING, logged as `unreproduced_vs_record`). When the old build was not kept (records made before
  this existed, or pruned) or cannot run, nothing can be shown and the differences from the record only
  warn. The committed baseline is not affected: it is the reviewed reference, and a regression against it
  still blocks after the usual confirmation.

  **What is measured is what is pushed.** The hook reads the refs git passes on stdin. It refuses the
  push, saying why, when tracked files have uncommitted changes (the build would include them), or when
  the commit being pushed for any ref (deletions excepted) is not the checked-out HEAD. A push that
  only deletes refs is not measured, and neither is one whose every update against the remote changes only
  documentation (Markdown, `docs/`, licence files, provenance manifests under `upstream/`): measuring then could only find noise, and a bimodal
  benchmark once blocked an AGENTS.md change. The skip is logged. A new branch, or a remote commit not
  present locally, is always measured. Otherwise the build is the pushed commit and the gate proceeds.
  Run by hand (`bench_check.py gate`, without `--refs-from-stdin`) it only warns about a dirty tree.

  **Inconclusive results retry, then warn.** If the result includes NOISY or ENVIRONMENT verdicts
  (against either comparison), the groups involved are measured once more with as many extra rounds
  as the first measurement had (so the rounds double). What is still inconclusive then does not block:
  the gate prints a WARNING naming each benchmark, and stores the list in the local record
  (`inconclusive`, with `inconclusive_retried`) and in `.git/sub0perf/log.jsonl`. Only REGRESSED blocks.
  A method mismatch (different epochs or pinned CPU, including unpinned against pinned) is reported
  as inconclusive but not retried, since more rounds cannot fix it.

  **One run at a time.** Measuring runs on a repository (`gate`, and `auto` with `--repo`, which the
  after-build check uses) take the lock `.git/sub0perf/lock` (pid, time, what), so concurrent runs
  cannot interleave and disturb each other's timing. A second run waits (`--lock-timeout`, default
  900 s) and then gives up. A lock whose process has gone, or older than six hours, is taken over.

  **Bypass.** `SUB0MATH_PERF_SKIP=<reason> git push` skips the gate; the reason is printed and logged
  as a `skip` event. (The gate's own build sets it internally so the after-build check does not run.)

  **Dependency pins.** A repository that pins a dependency by commit (`set(X_REVISION <sha>)` read by
  `CPMAddPackage(... GIT_TAG ${X_REVISION})`) must not push the pin before the dependency itself: CI
  then fails to fetch it ("unable to read tree"). Pins are declared at hook install, generically, as
  `install-hooks --pin file=cmake/Dependencies.cmake,var=X_REVISION,repo=owner/name[,checkout=../X][,name=X]`
  (repeatable; from CMake, the list `SUB0MATH_PERF_PINS`) and kept under `pins` in
  `.git/sub0perf/config.json`. When the pushed range changes the pin file (a new branch, whose range is
  unknown, counts), the sha is read from the pushed commit and looked up: `gh api repos/<owner>/<repo>/commits/<sha>`
  when `gh` is available (404/422 means absent), otherwise the sibling checkout's remote-tracking refs
  (`git branch -r --contains <sha>`, as fresh as its last fetch). Known absent: the push is refused with
  `push <name> first`. Unverifiable (offline, no `gh`, commit not in the checkout): a WARNING, the push
  goes on. A range that leaves the pin file alone, such as a documentation-only push, is not checked,
  which keeps the check free. It runs before the performance gate and is **not** bypassed by
  `SUB0MATH_PERF_SKIP`; `SUB0MATH_PINS_SKIP=<reason>` bypasses it, logged as `pins-skip`.

  Records (`.git/sub0perf/records/...`), samples files and baselines are written atomically (temp
  file, then replace), and records keep the shuffle `seed` of their rounds.

  **Confirmation pass.** Three rounds are not enough evidence to block on. Whatever is judged
  regressed on the first pass (a benchmark, a pair or a counter, against either comparison) is
  measured again, alone with its pair references, for `--confirm-rounds` more interleaved rounds
  (default 4, calibration as usual); both comparisons are then re-judged with all rounds. The gate
  prints `confirmed:` and `not confirmed:` lists and blocks only on the former. `auto` does the same.
  The pass is at least long enough to give every re-measured benchmark 10 measured samples (see bimodal
  detection), so with the default 3 initial rounds it adds 7, not 4, when a benchmark is short of them.

  **No controls.** A repository without a hand-written benchmark cannot factor out environment shifts.
  There, a change must also exceed 3x the suite's own spread of changes (median absolute deviation
  across all benchmarks in that comparison): it is judged against
  `max(tolerance, 3 x noise, 3 x suite spread)`, and the report says
  `No controls: suite spread X% used as an extra noise floor`. Repositories with controls keep the
  plain rule. The logic is covered by `python -m unittest discover -s tools/perf`.

### Tooling layout

`tools/perf/bench_check.py` is a thin shim with the command line below, so consumers (which find it
through `sub0math_tooling_dir.txt`) and installed git hooks (which call `bench_check.py gate
--refs-from-stdin` by path) are unaffected by the internals. The code is the `tools/perf/sub0perf`
package, lowest layer first:

| Module | Holds |
|---|---|
| `common` | Constants (`CALIBRATION`, `REFERENCE_MARK`), `group_of`, `read_json`, `atomic_write_text`, `tooling_script` |
| `platform_proc` | CPU pinning and priority, launching a measurement, machine CPU time counters, `pid_alive` |
| `load` | System load sampling, waiting for a quiet machine |
| `stats` | Robust CV, bimodal detection (`modes`, `rounds_needed`), calibration-scaled medians (`summarise`), `pair_ratios` |
| `baseline` | The two file formats: samples record (`make_record`, `write_samples`) and baseline (`baseline_data`, `baseline_tuple`, `write_baseline`) |
| `gitstate` | `git`, HEAD, clean tree, `.git/sub0perf` (records, log), pushed refs, documentation-only pushes |
| `runner` | One process per benchmark, interleaved calibrated rounds, `extend_rounds`, `measure_all`, `rerunner` |
| `judge` | Verdicts for benchmarks, pairs and counters, environment controls, method mismatch; `evaluate`, `verdict_hits` |
| `confirm` | Confirming regressions, retrying inconclusive results |
| `reproduce` | Measuring an earlier record's kept build beside the new one to reproduce a difference from the record |
| `pins` | Dependency pins: parsing, reading the sha, checking it is on the dependency's remote |
| `report` | The markdown tables and job summary |
| `accept` | `baseline` and `accept` commands (the logged `accepted` list) |
| `lock`, `hooks`, `gate` | One run at a time; installing and chaining hooks; the pre-push gate |
| `cli` | Argument parsing and the commands |

Tests live in `sub0perf/tests/`, one file per area, and are found by `python -m unittest discover -s
tools/perf` (the CI `python-tests` job). Change behaviour in the module that owns it; shared logic
(for example, "names with a verdict among benchmarks, pairs and counters") exists once, in `judge`.

A regression is accepted only explicitly, and only in the baseline you push:

```bash
python tools/perf/bench_check.py accept --results .git/sub0perf/records/<target>/<commit>.json     --baseline perf/baselines/<host>.json --benchmark "<name>" --reason "why this is acceptable"
```

`accept` needs measured samples for every named benchmark in `--results`: a benchmark that is absent, or
has only filled-in copies of a median, is an error naming it (exit 2, baseline untouched) rather than a
silent no-change entry. Measure it first, for example `bench_check.py run --exe <bench> --filter "<name or
group>" --out samples.json`, then accept from that file.

`accept` and `baseline` also refuse a run whose calibration kernel varied by more than 1.25x across its
rounds (exit 2, baseline untouched), because the machine was not steady. In one run the calibration read
5,046 ns and 9,042 ns, and scaling by it moved every accepted median by 30–40%. Re-measure on a quiet machine.

This moves those benchmarks' baseline (and pair ratios) to the new measurement and appends an entry to
the baseline's `accepted` log: date, commit, reason, before and after. Commit the file with the change.
The gate then lets a regression through against the earlier record only if an acceptance for that
benchmark was added since that commit (read from the baseline committed at HEAD, not the working tree). `--all-changed` accepts every regressed or improved benchmark.
Re-recording a whole baseline also requires a reason once a baseline exists, and is logged the same
way: `bench_check.py baseline --reason "..."`, or
`SUB0MATH_PERF_REASON="..." cmake --build build --target <target>_baseline`. `git push --no-verify` bypasses the hook, as with
any git hook; the baseline log is the record that should explain why.

Baselines live in `perf/baselines/<host>.json`: median, spread and rounds per benchmark, calibration
time, and pair ratios. Timings from one machine say nothing about another, so each host has its own.
Baselines in the older format (a number per benchmark) are still read, judged against the suite's
median drift, and should be re-recorded. CI uses `--report-only` against `github-ubuntu` and writes
the report to the job summary, because shared runners are noisy.

## Floating-point determinism

Parity tests and replay need the same bits from every build. The compiler can break that by fusing
`a * b + c` into one fused multiply-add (FMA), which rounds once instead of twice. Fusing is allowed by
default in some places: GCC in gnu mode (`-std=gnu++23`, the CMake default unless `CXX_EXTENSIONS OFF`)
uses `-ffp-contract=fast`, and any build with FMA hardware enabled (`-march=native`,
`SUB0MATH_NATIVE_ARCH`, `-march=x86-64-v3`, `/arch:AVX2`) then fuses. The same source gives different
results with and without it.

```cmake
sub0math_target_deterministic_fp(MyLibrary_Tests)             # PRIVATE: this target's own translation units
sub0math_target_deterministic_fp(MyLibrary INTERFACE)         # header-only: every consumer inherits it
```

The helper adds `-ffp-contract=off` (GCC, Clang) or `/fp:precise` (MSVC; clang-cl gets `/fp:precise` and
`/clang:-ffp-contract=off`). Scope defaults to `PRIVATE`. Sub0Math applies it to its own tests and
benchmarks; a library whose inline headers are compiled in consumers' targets exports it as `INTERFACE`.
The cost is the speed FMA would give, so a deliberate performance experiment with
`SUB0MATH_NATIVE_ARCH` can leave it off, but then compares nothing against goldens. CI has one leg that
builds the tests with `-march=x86-64-v3` (FMA available) and must still pass. Turning the helper on in a
repository moves its timings: re-record the baselines deliberately, with the reason.

On MinGW-w64 GCC, the helper also adds `-Wa,-muse-unaligned-vector-move` when the assembler supports it.
Windows guarantees only 16-byte stack alignment, but AVX code spills 32-byte vectors with aligned moves,
which fault. A consumer's station tests segfaulted under `-march=x86-64-v3` until this was added
(2026-10-20).

## Profiling

Build with the `profile` preset (optimised, debug info, frame pointers), then:

```bash
cmake --preset profile -DSUB0MATH_PROFILE_FILTER="rotate" -DSUB0MATH_PROFILE_SECONDS=5
cmake --build --preset profile --target Sub0Math_Bench_vtune_hotspots    # where time goes
cmake --build --preset profile --target Sub0Math_Bench_vtune_uarch       # why: front/back-end, ports, memory
cmake --build --preset profile --target Sub0Math_Bench_advisor_roofline  # vectorization and roofline
cmake --build --preset profile --target Sub0Math_Bench_perf_record       # Linux perf
```

Each target soaks the selected benchmark under the profiler, writes results to
`build-profile/profiles/<analysis>-<filter>-<time>/`, prints the summary and says how to open the GUI.
`tools/perf/profile.py` does the same without CMake.

- **VTune** reads GCC/Clang DWARF and MSVC PDB symbols on Windows and Linux.
- **Advisor** on Windows gives full loop and FLOP data for MSVC or Intel compiler builds; GCC builds
  still show loops and time.
- **ITT** task markers for VTune timelines belong with runtime code (`sub0physics::ProfileTask`).

## Vectorization and aliasing

```bash
cmake --preset vectorization
python tools/perf/vectorization_report.py --build-dir build-vec --target Sub0Math_Bench
```

The report rebuilds with GCC `-fopt-info-vec-*`, Clang `-Rpass*` or MSVC `/Qvec-report:2` and lists
remarks for this project's own files only. Tests and header checks build with
`-fstrict-aliasing -Wstrict-aliasing=2`.

Rules for math code:

- Mark short functions that consumers call in inner loops `SUB0MATH_FORCE_INLINE`; abstraction depth,
  not size, is what defeats the inliner. Judge with a consumer's benchmark, not only Sub0Math's.
- Value types only: trivially copyable, contiguous, no virtual calls, no allocation.
- Quantities and roles must cost nothing: every abstraction gets a paired benchmark against plain numbers.
- Prefer float where precision allows: it halves memory traffic and doubles SIMD width.
- Structure-of-arrays layouts and explicit SIMD belong in the consumer's hot loops, not in the vocabulary.

## Subnormal (denormal) numbers

Values below the smallest normal float (about 1.2e-38; 2.2e-308 for double) are subnormal. On x86 most
arithmetic on them takes a microcode assist.

**Measured on the development host** (GCC 16, -O3): 4096 float damped oscillators stepped 2000 times.

| Case | Time |
|---|---|
| Stays in the normal range | 1.0 ms |
| Decays into the subnormal range | 105 ms (about 100×) |
| The same, with flush-to-zero and denormals-are-zero (FTZ/DAZ) on | 1.1 ms |

**Why it matters for physics.** Anything that decays toward zero produces subnormals: a settled
suspension, the velocity of a resting body, an exponential filter, a contact force fading at separation,
soil rebound. With float as the default, that floor is reached sooner than with double. The result is
a sudden cost cliff in exactly the quiet scenes a real-time budget assumes are cheap.

**Is flushing physically acceptable?** Yes. Every quantity we store is in SI, and 1e-38 m, N or m/s is
far below any tolerance or measurement. The error introduced is negligible.

**Caveats:**

- **Per-thread processor state.** FTZ/DAZ live in MXCSR on x86 and FPCR on AArch64, per thread. Every
  physics worker thread must set them. A library must never change them implicitly.
- **Determinism.** Results differ between modes. The mode is part of a run's replay manifest, and is
  the same on every thread and machine that must agree.
- **Division.** A tiny nonzero denominator becomes zero, so the quotient becomes infinite. Code must
  not divide by a value that can approach zero without a guard. That is already the rule, for example
  no division by speed; Sub0Physics faults on non-finite state.
- **`-ffast-math`.** GCC's `-ffast-math` turns FTZ on at start-up implicitly. We do not use it, so the
  mode must be set explicitly.
- **Other processors.** Many ARM cores handle subnormals at full speed. Measure on each target before
  assuming either way.

**Recommendation (for later).** Adopt FTZ/DAZ for physics threads in the application (PhantomFlow),
set at thread start and recorded in the replay manifest. Sub0Math would then provide:

- a scoped guard that saves, sets and restores the mode;
- a query so Sub0Physics can assert the mode in debug builds;
- an informational benchmark of this decay kernel, to watch for the cliff.

Separately, models should still snap settled states to exact zero where that is their physics
(sleeping bodies), rather than rely on the flush. The trigger is the PhantomFlow threading integration
(terrain gap 17), or the first real-time run.

## Findings

| Date | Finding | Evidence | Action |
|---|---|---|---|
| 2026-10-15 | Component loops over the runtime `at(i)` cost ~45% in a 2D point-to-segment kernel; the `Vec<double>` vs `Vec<Length>` pairs could not see it because both sides paid | Sub0Physics budget gate (+52% on its clearance benchmark after adopting Sub0Math) | `forEachComponent` expands every component operation at compile time; new hand-written-vs-vocabulary benchmark pair |
| 2026-10-15 | Zero-initialised sums in `dot` and `Mat * Vec` cost an extra add each: IEEE signed zero forbids folding `0.0 + x` | Same pair, remaining 12% | Sums start from the first product; the pair is now equal (1565 vs 1561 ns) |
| 2026-10-15 | Adopting `closestPoint` in Sub0Physics made GCC stop inlining the caller's helper (one more call level): clearance +60%, invisible in Sub0Math's own benchmark | Consumer budget gate; `nm` showed an out-of-line `.isra` clone | `SUB0MATH_FORCE_INLINE` (`config.hpp`) on small functions that sit in callers' inner loops; clearance back to 414 ns |
| 2026-10-15 | Defaulted `<=>` on quantities goes through `std::partial_ordering` | Reasoning; no measurable change here | Direct relational operators on the stored value, kept for predictable codegen |
| 2026-10-16 | `abs` on quantities was `q < 0 ? -q : q`. GCC cannot turn that into the sign-clearing instruction (it differs from `fabs` for -0 and NaN), so it branched | Box-against-blocks pair: 2.7 to 2.0 µs after the change | `abs(Quantity)` uses `std::abs` on the stored value; this affects every quantity user |
| 2026-10-16 | A general box-against-box separating-axis test, called once per block, recomputes the box's rotation terms per block and loses to a hand-written loop that rejects most blocks after one comparison. Vectorising the branch-free form did not help: early rejection wins when most candidates miss | Vectorisation report: our loop vectorised, the faster hand-written one did not | `AabbOverlapTester`: box terms computed once at construction; coordinate axes tested first, with early exit |
| 2026-10-16 | Float decay into subnormals is about 100x slower; FTZ/DAZ restores normal speed | Scratch kernel on the development host (above) | Recorded for later adoption by the application; see Subnormal numbers |
| 2026-10-16 | Passing blocks as `Aabb` corners costs about 30% against the hand-written reference, which takes centres from the grid index | Box-against-blocks pair | API changed: `AabbOverlapTester::overlapsCentred(centre, half)` for grid walks; the remaining gap is under Open gaps |
| 2026-10-16 | nanobench 4.3.11 → 4.6.0 made the hand-written box-against-blocks reference about 11% faster (1,184–1,244 vs 1,362 ns); the vocabulary testers did not change, so their pair ratios rose from 0.95–0.98× to 1.06–1.09× | A/B: 4.6.0, 4.3.11, 4.6.0 again, 5 interleaved rounds each | The faster reference is the bar: open gap updated, baseline re-recorded with the reason logged |
| 2026-10-19 | The pre-push gate blocked three pushes in a row in BikeDynamics, each time on a different benchmark and with no relevant code change: the repository has no hand-written controls, so environment shifts could not be factored out, and one 3-round measurement was enough to block | Gate logs: "Suspension analytical comparison", then "Terrain probe" | Confirmation pass (regressions re-measured for `--confirm-rounds` more rounds; only persisting ones block) and, without controls, a suite-spread noise floor (3x MAD of changes) |
| 2026-10-19 | Spatial algebra: carrying an inertia to a parent frame (`X^T I X`) computed the full 3x3 `E^T I E`; the hand-written reference computes the six symmetric entries | Pair `inertia to parent x1024`: 1.15x, then 1.02x (5 interleaved rounds) | `applyInverse(SpatialInertia)` computes the upper triangle and mirrors it |
| 2026-10-19 | `PluckerTransform` apply/applyInverse runs 0.82x of hand-written scalar code: GCC 16 SLP-vectorises the column-sum form (`Mat * Vec`) but not the scalar expressions, even with stores deferred | Pair `spatial transform x1024`, vectorization remarks | The vocabulary is the fastest form known here; the scalar reference stays as the plain-numbers control |
| 2026-10-20 | Grid ownership (`cellOf`) with `std::optional`-free `GridCell{index, known}` and a coverage query that clamps in floating point before converting: both at parity with plain scalar code on 1024 inputs, one in six outside the grid | Pairs `grid cellOf x1024` 1.01-1.04x, `grid cellsTouching x1024` 0.99-1.08x (7 interleaved rounds, noisy host) | One deferred finite test in `cellsTouching` (a branch per axis measured 1.08x); bitwise-or of the range tests in `cellOf` |
| 2026-10-27 | Two causes behind the box group's false alarms. (1) Each round's start calibration read about 1.8x slow (9,030 vs 4,900 ns, every round): the quiet-wait idles the pinned CPU and its clock drops, so every sample interpolated towards that reading was mis-scaled. (2) The pair ratio is code-layout sensitive: with the box code unchanged, adding M2b's benchmarks to bench_math.cpp made the library corners kernel 6% slower and the hand-written SAT 9% faster (pair 1.17x to 1.37x) | Round calibrations printed per round; A/B of e695adc against 54e6a02, 6 interleaved runs each | (1) `steady_calibration`: repeat until two readings agree within 5%, so start and end now agree (about 4,700 to 5,200 ns). `accept` and `baseline` refuse runs whose calibration varied over 1.25x. (2) Accepted with the A/B as the reason. The absolute times were recorded in a slower machine state, and calibration scaling does not fully remove that, so these three entries carry a few percent of extra uncertainty. Follow-up: isolate each benchmark kernel (its own translation unit, or `noinline` and aligned) so adding benchmarks cannot move the pairs |
| 2026-10-21 | The gate falsely blocked five pushes in two days, each disproved by an interleaved A/B of the two builds (identical times): (1) box vs 1024 blocks (`AabbOverlapTester` forms and the hand-written SAT) alternates between about 1175 and 1390 ns per process, and one row was labelled bimodal yet still REGRESSED (+15%); (2) two pairs REGRESSED against the previous push's local record (1.455x to 1.56x, 1.107x to 1.169x) that A/B showed identical (e.g. 338 vs 338 us); (3) a consumer pushed a dependency pin before the dependency, failing all CI | Gate output and A/B runs of the BikeDynamics and Sub0Physics sessions; the causes below are inferred from the code and the earlier bimodal evidence, not re-measured here. (1) The test needs 5 samples and the gate had 3 to 7, so it often could not run, and a bimodal hand-written control (the SAT is a control) moves the factor applied to every other benchmark by its own level change. (2) A record is another session's measurement; nothing in it can say whether a difference is the code | (1) Bimodal controls excluded, baselines remember two levels, shifts judged against both levels and both scalings, 10 measured samples before blocking. (2) Kept executables and side-by-side re-measurement; a difference from a record blocks only if it reproduces, else warns. (3) Pin check at push time (above). Unit tests use synthetic samples, records and git repositories only |
| 2026-10-20 | The group `box vs 1024 blocks` (hand-written 2D SAT and both `AabbOverlapTester` forms) moves between two timing levels from process to process (about 5–10%), which produced confirmed REGRESSED verdicts on unrelated pushes | Repeated `--exact` runs pinned to one CPU, interleaved in time, Windows, GCC 16, a host with other agents building (calibration load 8–11%, so not a quiet machine). **(1)** Within one process, per-epoch times shift by 5–10% over seconds and the first epoch can be 20% faster (turbo, then settling); epoch error inside a process is under 1% in a calm moment, so the level is a machine state, not fixed per process. **(2)** The loops read no arrays (centres come from the grid index), so heap or array alignment, the usual suspect, does not apply. **(3)** Disabling ASLR (fixed image base, `--disable-dynamicbase`) left the spread unchanged (n = 40 each, interleaved). **(4)** `-falign-loops=64 -falign-functions=64 -falign-jumps=32` moved medians by at most 3%, in different directions per benchmark: code alignment is a small effect, not the 5–10%. **(5)** The calibration kernel itself varies 4.6–5.6% across processes (tails to +50%) and correlates only 0.2–0.5 with the benchmark times, so scaling by the clock cannot undo it: it is measured in other processes at other moments | There is no source change that removes a machine state, and the benchmark is already free of data layout effects; the hand-written reference is not touched. The fix is in the tooling: bimodal detection (above) turns a one-level shift into NOISY. Not yet shown, for a quiet-machine session: a longer A/B of the baseline entries and an in-process repeat to separate frequency from contention. The three baseline entries were not re-recorded: this host was not quiet, so re-recording is left to the architect via `accept --reason` from fresh samples |
| 2026-10-27 | `contact(Capsule, ConvexPolygon)` out of line measured 1.25x to 1.4x its hand-written twin (same closed form on raw doubles, same outputs): GCC kept the function as a call per (edge, piece) pair, so the result aggregate went through memory | Pair `capsule-slab contact 64x4`, 3 interleaved rounds per compiler | `SUB0MATH_FORCE_INLINE` on the polygon contact, and the two end candidates as a loop instead of a lambda: 1.0x to 1.1x (GCC 16), about 0.9x (Clang 22) over 5 rounds |

## Measured (development host, GCC 16, -O3)

| Benchmark, 1024 elements | ns |
|---|---:|
| dot, double / f64::Length | 711 / 688 |
| x += v*dt, double / f64::Length / float | 359 / 321 / 162 |
| quaternion rotate, double / float | 2061 / 1084 |
| Mat4 * Vec4, double / float | 1251 / 823 |
| segment distance^2, hand-written scalars / Point2<f64::Length> | 1561 / 1565 |
| triangle closest point, hand-written scalars / Triangle<f64::Length> (1024 triangles) | 3021 / 3140; parity within noise across runs |
| ray vs triangles, hand-written Moller-Trumbore / Ray<f64::Length> (1024 triangles) | 2966 / 2860; parity |
| box vs 1024 blocks: hand-written (centres from index) / tester (centre, half) / tester (Aabb corners) | 921 / 1022 / 1309 |
| spatial transform (motion `X m` and force `X^T f`), hand-written scalars / `PluckerTransform` | 8031 / 6716; pair 0.82x |
| inertia to parent frame `X^T I X`, hand-written scalars / `PluckerTransform::applyInverse` | 13105 / 13296; pair 1.02x |
| grid `cellOf` x1024, hand-written scalars / `RegularGrid<f64::Length>::cellOf` | pair 1.01-1.04x |
| grid `cellsTouching` x1024, hand-written scalars / `RegularGrid<f64::Length>::cellsTouching` | pair 0.99-1.08x |
| capsule-slab contact (64 casing edges x 4 slabs, gap, witness point and normal), hand-written scalars / `contact(Capsule, ConvexPolygon<double, 4>)` | GCC 16: 4460-4770 / 4690-5030, pair 1.0-1.1x (median 1.06x); Clang 22: 4680-5110 / 4470-4640, pair about 0.9x (`-march=x86-64-v3`, 5 interleaved rounds, loaded host) |

Timings vary by a few percent run to run on a shared desktop; compare pairs measured together, and use
best-of-N when judging a change.

## Open gaps

Deficiencies against the hand-written reference, kept until closed or accepted with a reason.

| Since | Pair | Gap | Cause | Follow-up |
|---|---|---|---|---|
| 2026-10-16 | box vs 1024 blocks, `AabbOverlapTester::overlapsCentred` | about 1.06–1.09× interleaved under nanobench 4.6.0 (0.95–0.98× under 4.3.11; the new harness lets the hand-written loop compile about 11% faster, A/B'd both ways) | The box-face radius `sum(half[j] * |r[i][j]|)` is recomputed per block. The hand-written loop folds it once, because every block has the same half size | When Sub0Physics' coverage rasterisation (terrain step G2) is profiled: a tester prepared for a uniform block size (radii precomputed), or a grid walk that steps centres incrementally. Close it or accept it, with the consumer's numbers |
| 2026-10-16 | box vs 1024 blocks, `AabbOverlapTester(Aabb)` | about 1.4× | Deriving centre and half from corners per block | By design: use `overlapsCentred` on regular grids; the `Aabb` form is for irregular boxes |
| 2026-10-16 | A zero-length branch in `closestPoint(Segment)` measured within noise in this repository's pair, but cost about 1.5x in Sub0Physics' clearance (465 to 552 ns, budget gate) | Consumer gate, then an A/B with the guard removed | A branch-free `max(length², smallest normal)` divisor fixed the consumer (464 ns) but left this pair 1.06x behind the hand-written kernel. So the check moved out: `closestPoint(Segment)` keeps its non-zero-length precondition (parity again), and `Capsule`, the one shape that can be a point, handles that case. Lesson: a change to a hot vocabulary function is measured in its consumers too, and robustness goes where it is needed, not into every caller's inner loop |
| 2026-10-16 | `std::optional` results in a hot query loop stay in memory on GCC 16: 1.7x on a terrain height lookup, and `optional<float>` is no better | Sub0Physics `page height x1024` | Hot-path queries return a plain aggregate with a status (`HeightSample`); see STYLE_GUIDE. `intersect(Ray, Triangle)` still returns an optional; it is at parity in its pair, where hits are rare. Revisit it with a hit-heavy consumer |
| 2026-10-16 | The pre-push gate blocked a Python-and-docs-only commit: every benchmark ran 7–28% slower than the previous local record, hand-written references included, with only 4.5% of it explained by the clock | Gate comparison against the record of the parent commit | Hand-written references became environment controls (median change factored out; shift reported). A real +30% on one library benchmark is still caught (+32.7% judged, pair ratio 1.30x) |
