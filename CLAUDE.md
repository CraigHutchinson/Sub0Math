# Sub0Math Project Rules

Read [AGENTS.md](AGENTS.md) first.

## Build & Test

```bash
cmake --preset default          # Configure (Release + tests + benchmarks)
cmake --build --preset default  # Build
ctest --preset default          # Run tests
```

`build/tests/Sub0Math_Bench` runs the paired benchmarks; budgets and profilers are in docs/PERFORMANCE.md.
A git pre-push hook runs the tests: `git config core.hooksPath .githooks`.

## Commit Rules

- A commit that changes the public API in `include/sub0math/` says so in its message.
- A change to `cmake/Sub0MathTooling.cmake` or `tools/perf/` says which consumers it affects.
- Tests pass locally before committing.

## Branch Strategy

`main` is the integration branch. Use focused topic branches and pull requests.
