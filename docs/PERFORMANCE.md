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
| Zero time overhead | Paired benchmarks | `dot, f64::Length` and `x += v*dt, f64::Length` match their plain-double twins. Segment, triangle closest point and ray-triangle on `f64::Length` match their hand-written scalar kernels, within noise. Box against blocks: see the open gaps |
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
cmake --preset default
cmake --build --preset default --target Sub0Math_Bench_check      # fails if >10% slower than baseline
cmake --build --preset default --target Sub0Math_Bench_baseline   # record this host's baseline
```

Budget targets run the benchmark `SUB0MATH_BENCH_REPEATS` times (default 5) and compare each benchmark's
best run: background load only slows runs down, so best-of-N is steady on a busy desktop where single runs
varied by 30%. Each benchmark is then judged relative to the suite's median change, because a busy or
throttled machine slows everything alike (seen: every benchmark, hand-written kernels included, +8-23% at
once). The host drift and raw changes are reported; `bench_check.py --absolute` judges without the
correction, for CI or quiet machines, since a change that slows every benchmark equally would otherwise
hide. Baselines live in `perf/baselines/<host>.json` (best median ns per op). Timings from one machine
say nothing about another, so each host has its own file; a host without one passes and is told how to
adopt the gate. Regenerate a baseline deliberately and say why in the commit. CI reports drift against
`github-ubuntu` without blocking, because shared runners are noisy.

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

Timings vary by a few percent run to run on a shared desktop; compare pairs measured together, and use
best-of-N when judging a change.

## Open gaps

Deficiencies against the hand-written reference, kept until closed or accepted with a reason.

| Since | Pair | Gap | Cause | Follow-up |
|---|---|---|---|---|
| 2026-10-16 | box vs 1024 blocks, `AabbOverlapTester::overlapsCentred` | about 1.1× (921 vs 1022 ns) | The box-face radius `sum(half[j] * |r[i][j]|)` is recomputed per block. The hand-written loop folds it once, because every block has the same half size | When Sub0Physics' coverage rasterisation (terrain step G2) is profiled: a tester prepared for a uniform block size (radii precomputed), or a grid walk that steps centres incrementally. Close it or accept it, with the consumer's numbers |
| 2026-10-16 | box vs 1024 blocks, `AabbOverlapTester(Aabb)` | about 1.4× | Deriving centre and half from corners per block | By design: use `overlapsCentred` on regular grids; the `Aabb` form is for irregular boxes |
