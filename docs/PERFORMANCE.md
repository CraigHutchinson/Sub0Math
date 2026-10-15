# Performance

Sub0Math is the bottom of the Sub0 stack, so it hosts the family's performance tooling:
`cmake/Sub0MathTooling.cmake` and `tools/perf/`. Every library that depends on Sub0Math (Sub0Physics,
BikeDynamics, ...) gets the same benchmark runner, budgets and profiler targets:

```cmake
include("${Sub0Math_SOURCE_DIR}/cmake/Sub0MathTooling.cmake")   # after CPMAddPackage(Sub0Math ...)
sub0math_target_perf_options(MyLibrary)                       # profile / vectorization / native options
sub0math_add_benchmark(MyLibrary_Bench bench_my_library.cpp)    # plus _json/_check/_baseline/_vtune_*/...
```

Benchmarks include `<bench_runner.hpp>` and accept `--filter` (case-insensitive substring), `--json`,
`--soak <seconds>` (loop under a profiler) and `--list`.

## Codified gates

| Gate | Where | Contract |
|---|---|---|
| Zero size overhead | `static_assert`s in the tests | Quantities, roles, vectors and matrices are exactly the size of their numbers and trivially copyable |
| Zero time overhead | Paired benchmarks | `dot, f64::Length` and `x += v*dt, f64::Length` match their plain-double twins within noise |
| Wrong code does not compile | Concept checks in the tests | Dimension mismatches, swapped or unnamed roles, silent narrowing, point + point |
| Header self-containment | `Sub0Math_HeaderCheck` | Every public header compiles alone under the strict warning set |

## Timing budgets (per host)

```bash
cmake --preset default
cmake --build --preset default --target Sub0Math_Bench_check      # fails if >10% slower than baseline
cmake --build --preset default --target Sub0Math_Bench_baseline   # record this host's baseline
```

Baselines live in `perf/baselines/<host>.json` (median ns per op from nanobench). Timings from one machine
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

- Value types only: trivially copyable, contiguous, no virtual calls, no allocation.
- Quantities and roles must cost nothing: every abstraction gets a paired benchmark against plain numbers.
- Prefer float where precision allows: it halves memory traffic and doubles SIMD width.
- Structure-of-arrays layouts and explicit SIMD belong in the consumer's hot loops, not in the vocabulary.

## Measured (development host, GCC 16, -O3)

| Benchmark, 1024 elements | ns |
|---|---:|
| dot, double / f64::Length | 627 / 624 |
| x += v*dt, double / f64::Length / float | 372 / 380 / 150 |
| quaternion rotate, double / float | 1745 / 912 |
| Mat4 * Vec4, double / float | 1271 / 966 |
