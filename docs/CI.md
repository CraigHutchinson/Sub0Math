# CI

This repository follows the family CI strategy: [ci-strategy.md](https://github.com/CraigHutchinson/.github/blob/main/docs/ci-strategy.md).

| Workflow | Tier | Runs on |
|---|---|---|
| `ci.yml` | Smoke: configure, build with warnings as errors, `ctest --preset quick` (Linux, GCC) | Push to `main`, pull requests (not documentation-only changes) |
| `ci-full.yml` | Full: GCC and Clang, FMA leg, MSVC, macOS, sanitizers, `ctest --preset full`, provenance and reference suites | Tags `v*` and `milestone/*`, manual dispatch, pull requests labelled `ci:full`, weekly if `main` changed |
| `upstream-drift.yml` | Upstream drift report (cheap) | Weekly, manifest changes |

Performance is **not** measured in CI: shared runners are too noisy. The local perf gate owns it.

Off switches: repository variable `CI_MODE` (`off`, `smoke`, or unset), `[skip ci]` in a commit message, and the
`ci:skip` pull request label (then `local-ci/quick` or `local-ci/full` is required instead).

Local attestation: `python <path to CraigHutchinson/.github>/tools/ci/local_ci.py --level quick|full` runs the
checks declared in `.ci/local.json` on every compiler on this machine and posts a `local-ci/<level>` commit status
for a pushed commit (`--no-post` to only run, `--post-only` to attest after pushing).

## WebAssembly leg

Every digest the tests assert must come out the same on WebAssembly as on the native compilers (GCC, GCC
`-march=x86-64-v3`, Clang `-march=x86-64-v3`). The leg builds the repository with Emscripten and runs its tests
under Node:

```
python tools/wasm/wasm_gate.py --repo . --prefix SUB0MATH        # in Sub0Physics: python ../Sub0Math/tools/wasm/wasm_gate.py --repo . --prefix SUB0PHYSICS
```

`--prefix` is the repository's CMake option prefix (`SUB0MATH`, `SUB0PHYSICS`, `BIKEDYNAMICS`). It configures
`build-wasm` with `emcmake cmake` (Ninja, Release, benchmarks, perf hooks and the after-build perf check off,
warnings as errors), builds with 4 jobs and runs `ctest -LE full` through the emsdk's Node (`--all` adds the
suites labelled full). It is the `wasm` step of the `full` level in `.ci/local.json` of all three repositories.

- **Toolchain.** Install the Emscripten SDK once (`git clone https://github.com/emscripten-core/emsdk`, then
  `emsdk install latest` and `emsdk activate latest` without `--permanent`), next to the repositories (`../emsdk`),
  or point `EMSDK_DIR` at it. The script enters the SDK's environment for its own child processes only; nothing is
  registered in the machine's environment. Without an SDK the step prints `wasm gate: FAILED` and exits 2, so a
  full-level attestation is never posted without the WASM leg; `--allow-missing-sdk` turns that into a `SKIPPED`
  exit 0 for a deliberate partial run. Last verified with Emscripten 6.0.13 and Node 24.
- **Flags.** The leg adds `-fexceptions` (doctest reports a failed `REQUIRE` by throwing; without it the run aborts
  at the first failure), `-sNODERAWFS=1` (tests read golden files from the host file system) and
  `-sALLOW_MEMORY_GROWTH=1`. None changes a numeric result. `-ffp-contract=off` stays on through
  `sub0math_target_deterministic_fp`, and WebAssembly has no fused multiply-add in the base instruction set.
- **Portability.** wasm32 has a 32-bit `size_t` and `long`: widen to `std::uint64_t` before shifting or packing
  (a `size_t` shifted by 32 is undefined there). `std::uniform_real_distribution` and its relatives differ
  between libstdc++ and libc++: tests draw from the generator's raw bits instead (see `uniformSigned`).
- **A failing digest.** Find the first differing step, then the operation: a `std::` transcendental (route it
  through `sub0math::det`), a contraction, a reduction order or an integer width. Never change native numerics to
  match.
