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
