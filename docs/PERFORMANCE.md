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
| Zero time overhead | Paired benchmarks | `dot, f64::Length` and `x += v*dt, f64::Length` match their plain-double twins, and `segment distance^2, Point2<f64::Length>` matches the hand-written scalar kernel, within noise |
| Wrong code does not compile | Concept checks in the tests | Dimension mismatches, swapped or unnamed roles, silent narrowing, point + point |
| Header self-containment | `Sub0Math_HeaderCheck` | Every public header compiles alone under the strict warning set |

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

## Findings

| Date | Finding | Evidence | Action |
|---|---|---|---|
| 2026-10-15 | Component loops over the runtime `at(i)` cost ~45% in a 2D point-to-segment kernel; the `Vec<double>` vs `Vec<Length>` pairs could not see it because both sides paid | Sub0Physics budget gate (+52% on its clearance benchmark after adopting Sub0Math) | `forEachComponent` expands every component operation at compile time; new hand-written-vs-vocabulary benchmark pair |
| 2026-10-15 | Zero-initialised sums in `dot` and `Mat * Vec` cost an extra add each: IEEE signed zero forbids folding `0.0 + x` | Same pair, remaining 12% | Sums start from the first product; the pair is now equal (1565 vs 1561 ns) |
| 2026-10-15 | Adopting `closestPoint` in Sub0Physics made GCC stop inlining the caller's helper (one more call level): clearance +60%, invisible in Sub0Math's own benchmark | Consumer budget gate; `nm` showed an out-of-line `.isra` clone | `SUB0MATH_FORCE_INLINE` (`config.hpp`) on small functions that sit in callers' inner loops; clearance back to 414 ns |
| 2026-10-15 | Defaulted `<=>` on quantities goes through `std::partial_ordering` | Reasoning; no measurable change here | Direct relational operators on the stored value, kept for predictable codegen |

## Measured (development host, GCC 16, -O3)

| Benchmark, 1024 elements | ns |
|---|---:|
| dot, double / f64::Length | 711 / 688 |
| x += v*dt, double / f64::Length / float | 359 / 321 / 162 |
| quaternion rotate, double / float | 2061 / 1084 |
| Mat4 * Vec4, double / float | 1251 / 823 |
| segment distance^2, hand-written scalars / Point2<f64::Length> | 1561 / 1565 |

Timings vary by a few percent run to run on a shared desktop; compare pairs measured together, and use
best-of-N when judging a change.
