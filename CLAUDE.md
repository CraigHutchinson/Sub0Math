# Sub0Math Project Rules

Read [AGENTS.md](AGENTS.md) first.

## Build & Test

```bash
cmake --preset default          # Configure (Release + tests + benchmarks)
cmake --build --preset default  # Build
ctest --preset default          # Run tests
```

`build/tests/Sub0Math_Bench` runs the paired benchmarks; budgets and profilers come from Sub0Perf (docs/PERFORMANCE.md).
The pre-push hook is the performance gate that configuring installs into `.git/hooks` (Sub0Perf's sub0perf, docs/PERFORMANCE.md); no hook is committed and `core.hooksPath` stays unset. Tests run in local CI (`local_ci.py --level quick`), and `hook_check.py --repo .` from the .github repository's `tools/ci` proves the gate works.

## Commit Rules

- A commit that changes the public API in `include/sub0math/` says so in its message.
- A change to `cmake/Sub0MathFp.cmake` says which consumers it affects; so does a bump of the Sub0Perf pin in `CMakeLists.txt` (it touches Sub0Math's own gate only).
- Tests pass locally before committing.

## Branch Strategy

`main` is the integration branch. Use focused topic branches and pull requests.
