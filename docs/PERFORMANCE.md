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

**How a measurement is made robust** (`tools/perf/bench_check.py`):

| Threat | Countermeasure |
|---|---|
| Thermal and turbo drift during a run | **Interleaved rounds:** every benchmark once per round, each in its own process, in an order shuffled per round (seeded). Drift spreads over all benchmarks instead of biasing the last ones |
| Clock speed varying within and between runs | **Calibration:** a fixed dependent arithmetic chain (`--calibration`) is timed at the start and end of each round. Each sample is scaled by the clock interpolated at its position in the round. Baselines record calibration, so a slower session is corrected before judging. Measured here: the core runs about 3% slower at the end of a round than at its start |
| Environment changes the clock does not show (cache and memory contention, other load) | **Controls:** hand-written references do not use the library, so whatever moves them is the environment. After calibration, their median change is factored out, and a shift beyond half the tolerance is reported. Seen: a commit touching only Python and docs was blocked because every benchmark, the references included, ran 7–28% slower while the clock moved 4.5%. The controls showed +13.3%, and with that factored out nothing regressed, while an injected +30% on one library benchmark is still caught. Repositories without hand-written references get calibration only, and the report says so |
| Environment shifted too far to judge absolute times | When the controls move by more than the tolerance beyond the clock, absolute verdicts become **ENVIRONMENT** (inconclusive, never blocking). Pair ratios and instruction counts are relative to the same environment and still judge. Seen: a +41% shift while another process ran benchmark rounds |
| Core migration and preemption | Each measurement is pinned to one logical CPU (`--cpu`, default the last) at raised priority. Spreads fell from a 2.7% median and 17.6% worst to 1.1% and 7.6%, and calibration from 0.6% to 0.1% |
| Other load on the machine | **Load check:** CPU time used by other processes is sampled before each round. Above `--max-load` (20%) the run waits up to `--settle` seconds, then records the round as busy. The report states how many rounds ran busy |
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

  Any regression that persists blocks the push, with the exact command to accept it.

  **What is measured is what is pushed.** The hook reads the refs git passes on stdin. It refuses the
  push, saying why, when tracked files have uncommitted changes (the build would include them), or when
  the commit being pushed for any ref (deletions excepted) is not the checked-out HEAD. A push that
  only deletes refs is not measured. Otherwise the build is the pushed commit and the gate proceeds.
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

  Records (`.git/sub0perf/records/...`), samples files and baselines are written atomically (temp
  file, then replace), and records keep the shuffle `seed` of their rounds.

  **Confirmation pass.** Three rounds are not enough evidence to block on. Whatever is judged
  regressed on the first pass (a benchmark, a pair or a counter, against either comparison) is
  measured again, alone with its pair references, for `--confirm-rounds` more interleaved rounds
  (default 4, calibration as usual); both comparisons are then re-judged with all rounds. The gate
  prints `confirmed:` and `not confirmed:` lists and blocks only on the former. `auto` does the same.

  **No controls.** A repository without a hand-written benchmark cannot factor out environment shifts.
  There, a change must also exceed 3x the suite's own spread of changes (median absolute deviation
  across all benchmarks in that comparison): it is judged against
  `max(tolerance, 3 x noise, 3 x suite spread)`, and the report says
  `No controls: suite spread X% used as an extra noise floor`. Repositories with controls keep the
  plain rule. The logic is covered by `python -m unittest discover -s tools/perf`.

A regression is accepted only explicitly, and only in the baseline you push:

```bash
python tools/perf/bench_check.py accept --results .git/sub0perf/records/<target>/<commit>.json     --baseline perf/baselines/<host>.json --benchmark "<name>" --reason "why this is acceptable"
```

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
