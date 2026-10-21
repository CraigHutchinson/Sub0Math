# Sub0Math Code Style Guide

style-profile: sub0

Sub0Math follows the Sub0 family style profile. This guide summarises how the rules apply here;
where it and the profile disagree, the profile wins.

## Naming

| Element | Convention | Example |
|---|---|---|
| Namespace | lowercase | `sub0math` |
| Classes, structs, enums, aliases, concepts | PascalCase | `Quantity`, `Vec`, `RigidTransform` |
| Functions and methods | camelCase | `lengthSquared`, `fromAxisAngle` |
| Members | camelCase with `_` suffix | `value_` (public aggregate fields have none) |
| Constants and enumerators | PascalCase, no prefix | `DefaultReal`, `dim::Length` |
| Macros and CMake options | `SUB0MATH_` prefix | `SUB0MATH_BUILD_TESTING` |
| Files | snake_case `.hpp` / `.cpp` | `rigid_transform.hpp`, `test_units.cpp` |

## Layout

4 spaces, no tabs; Allman braces for namespaces, types, functions and control flow. About 120
columns. `template<typename T>` with no inner spaces.

## Includes

System and third-party headers first (`<doctest.h>` included), then own headers quoted and
library-rooted (`"sub0math/units/quantity.hpp"`); each group alphabetical. Every folder has an umbrella
header (`linear/` → `linear.hpp`); `sub0math.hpp` includes everything.
`Sub0Math_HeaderCheck` compiles every public header on its own.

## Documentation

Doxygen `/** */` on every public declaration; the first sentence is the brief (no `@brief`);
`@param`, `@return` and `@tparam` throughout. `///<` for data members. State units (SI) and sign
conventions on every physical quantity.

## Performance notes in code

Where performance has been measured and iterated, leave a `// PERF:` comment at the code, ten words or
fewer, saying what was found faster than what: `// PERF: std::abs faster than compare-and-negate, which
branched.` The detail (numbers, benchmark, date) goes in `docs/PERFORMANCE.md` under Findings.

Hot-path queries return a plain aggregate with a status (a value and `known`, with an explicit
`operator bool`) rather than `std::optional`: on GCC 16 the optional kept results in memory and cost
1.7x in a terrain lookup. Use `std::optional` where the call is not in an inner loop.

PERF notes are hints, not rules. They record that this code has already been worked on, so the next
person measures before changing it. Later work may override a note when its own measurement says so,
and then updates the note.

## Math types

- Value types are aggregates or have constexpr, noexcept members; static assertions pin size and
  trivial copyability.
- Public data members of aggregates have no suffix (`v.x`); private members use `camelCase_`.
- Literal suffixes are lower case (`_kmh`): an underscore followed by a capital letter is reserved.
- Do not name a function after a C library function that takes plain numbers (`asin`): the C overload
  wins for plain arguments.
- Every new abstraction ships with a paired benchmark against plain numbers.

## Portability across the CI compilers

- **Don't iterate a view of a temporary in a range-for** (`for (x : make().view())`). C++23 (P2718) extends the
  temporary's lifetime, but GCC 14 in CI does not implement that yet, so the view dangles there. Name the
  result first: `const auto tiles = make(); for (x : tiles.view())`. This was caught by ASan
  (stack-use-after-scope) on 2026-10-21; GCC 16 passed locally.
