# Repository guidance

Sub0Math is the product-agnostic math vocabulary of the Sub0 family: quantities with dimensions, named
roles, vectors, matrices, quaternions, transforms and points. Header-only C++23 with no dependencies.
Use generic examples in source, tests, documentation, issues and pull requests; no product names.

- Read [STYLE_GUIDE.md](STYLE_GUIDE.md) (profile `sub0`) and [docs/DESIGN.md](docs/DESIGN.md) before
  writing C++. The design is fixed by ADR 0001 in `CraigHutchinson/.github`; propose changes there.
- Keep the scope: value types and their algebra. Numerical methods go to Sub0Physics, signal processing
  to Sub0Dsp.
- Every type stays zero-overhead: size and trivial-copy static assertions, and a paired benchmark
  against plain numbers for anything new.
- Every "this must not compile" rule gets a concept check in the tests.
- Performance claims come from `Sub0Math_Bench` with the host and compiler stated. This repository also
  owns the family's performance tooling (`cmake/Sub0MathTooling.cmake`, `tools/perf/`): changes there
  affect every consumer.

## C++ Skills

Three skills manage the C++ authoring and review workflow. Full reference material lives in `~/.claude/skills/cpp/references/`.

| When | Skill | Invoke condition |
|---|---|---|
| Before writing a new class, public API, or module | **cpp-write** | Proactively before authoring |
| After substantive C++ changes | **cpp-review** | Proactively after authoring |
| Before implementing -- to review a plan or design doc | **cpp-review** (plan mode) | Explicitly, with plan doc as input |
| After L0/L1 MUST findings | **cpp-simplify** | With Rewrite Brief from cpp-review as input |

The `.cursor/rules/cpp-standards.mdc` project rule auto-attaches the most important constraints to Agent sessions when C++ files are in context. The skills carry the full reference material and worked examples.
