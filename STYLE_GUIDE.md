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

## Math types

- Value types are aggregates or have constexpr, noexcept members; static assertions pin size and
  trivial copyability.
- Public data members of aggregates have no suffix (`v.x`); private members use `camelCase_`.
- Literal suffixes are lower case (`_kmh`): an underscore followed by a capital letter is reserved.
- Do not name a function after a C library function that takes plain numbers (`asin`): the C overload
  wins for plain arguments.
- Every new abstraction ships with a paired benchmark against plain numbers.
