# Sub0Math

The shared math vocabulary of the [Sub0 family](https://github.com/CraigHutchinson/.github/blob/main/docs/sub0-family.md):
vectors, points, matrices, quaternions and rigid transforms, numbers that carry their physical
dimension, and named argument roles. Header-only C++23, no dependencies, zero runtime overhead.

Decision record: [ADR 0001](https://github.com/CraigHutchinson/.github/blob/main/docs/decisions/0001-sub0math.md).
Design: [docs/DESIGN.md](docs/DESIGN.md). Performance method and tooling: [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Why

One `Vec3` for physics, rendering, games and firmware, instead of a copy per library. And two common
bug classes turned into compile errors:

```cpp
using namespace sub0math::literals;

Length h = 1.2_m;
Time dt = 500_us;
Velocity v = h / dt;                  // Length / Time is a Velocity
auto wrong = h + dt;                  // does not compile: Length + Time
float mm = h.in(millimetres);         // units named on the way out...
Length l = millimetres(350.0f);       // ...and on the way in
float s = sin(30_deg);                // trigonometry takes angles only

using Height = sub0math::Role<struct HeightTag, Length>;
using Width = sub0math::Role<struct WidthTag, Length>;
using Depth = sub0math::Role<struct DepthTag, Length>;
Box makeBox(Height h, Width w, Depth d);

makeBox(Height{1_m}, Width{5_m}, Depth{6_m});  // compiles
makeBox(Width{5_m}, Height{1_m}, Depth{6_m});  // does not: swapped
makeBox(1_m, 5_m, 6_m);                        // does not: unnamed
Height{1};                                     // does not: a length needs its unit
```

## What's in it

| Area | Header | Provides |
|---|---|---|
| Numbers | `sub0math/number.hpp` | `DefaultReal` (float), `Real` concept, `isNear` tolerance compare |
| Units | `sub0math/units.hpp` | `Quantity<Dimension, Rep>`, named dimensions, units as exact SI ratios, `f32`/`f64` alias families, literals, `sqrt`, angle-only trigonometry, `dropAngle` |
| Roles | `sub0math/role.hpp` | `Role<Tag, T>`: named argument and value types |
| Linear algebra | `sub0math/linear.hpp` | `Vec<T, N>` (named `x y z w`), column-major `Mat<T, R, C>`, `Quat<T>`, `RigidTransform<T, L>` |
| Geometry | `sub0math/geometry.hpp` | `Point<T, N>` with affine rules, `Aabb<T, N>` |

Vectors, points and transforms accept quantities as components: `Vec3<f64::Velocity> * f64::Time` is a
`Vec3<f64::Length>`, and a rotation keeps a force a force.

## Float by default, double by choice

Unqualified names (`Length`, the `_m` literals) are float: enough for rendering, firmware (single-precision
FPUs) and most interfaces. Simulation state, accumulated time and world positions use double explicitly
through `sub0math::f64` (`f64::Length`, `using namespace sub0math::f64::literals`). Float widens to double
implicitly; double narrows to float only explicitly.

## Build and test

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
build/tests/Sub0Math_Bench            # paired benchmarks: plain numbers vs quantities, float vs double
```

Consume with CPM at an exact commit:

```cmake
CPMAddPackage(NAME Sub0Math GITHUB_REPOSITORY CraigHutchinson/Sub0Math
    GIT_TAG <full-sha> EXCLUDE_FROM_ALL YES OPTIONS "SUB0MATH_BUILD_TESTING OFF")
target_link_libraries(app PRIVATE Sub0Math::Sub0Math)
```

Sub0Math also hosts the family's performance tooling (`cmake/Sub0MathTooling.cmake`, `tools/perf/`):
benchmark budgets, VTune, Advisor, `perf` and vectorization reports, one command each. Any library
that depends on Sub0Math can use it; see [docs/PERFORMANCE.md](docs/PERFORMANCE.md).

## Licence

[MIT](LICENSE.md). Copyright Craig Hutchinson.
