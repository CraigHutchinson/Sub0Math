# Design

Intent: one small, fast, dependency-free math vocabulary for every Sub0 library and every KismoTech
project, in which unit and argument-order mistakes do not compile. Accepted in
[ADR 0001](https://github.com/CraigHutchinson/.github/blob/main/docs/decisions/0001-sub0math.md).

## Scope

In: value types and their algebra. Numbers (`number/`), dimensioned quantities and units (`units/`),
named roles (`role/`), vectors, matrices, quaternions, rigid transforms and spatial (6D) algebra (`linear/`), points and boxes
(`geometry/`).

Out: numerical methods (integrators, solvers: Sub0Physics), signal processing (Sub0Dsp), rendering,
spatial queries over meshes or profiles (they stay with their consumers until shared), and anything with
a product name in it.

## Quantities

- `Quantity<D, Rep>`: a `Rep` with a compile-time `Dimension<L, M, T, A, K>` (length, mass, time, angle,
  temperature exponents). Electric current and amount of substance are added when a consumer needs them.
- **Algebra:** `+`, `-` and comparisons need equal dimensions; `*` and `/` add and subtract exponents;
  `sqrt` needs even exponents. A dimensionless result converts to a plain number.
- **Storage is always coherent SI** (m, kg, s, rad, K). Units are not part of the type.
- **Units** are exact ratios to SI (`millimetres = 1/1000`, `degrees = pi/180`), so conversions round
  once: `millimetres(350.0)` is exactly the double nearest 0.35. A unit of the wrong dimension does not
  compile in `in()`.
- **Angle is a dimension.** `sin`/`cos`/`tan` take angles only. Where physics treats the radian as 1
  (arc length, tangential speed, rotational energy), `dropAngle` says so explicitly. Inverse trigonometry
  is `angleFromSine` / `angleFromCosine`: named `asin` it would lose to the C library's `asin(double)`.
- **Temperature** is kelvin only; Celsius is affine and is converted by the caller.
- **No implicit unwrap:** `si()`, `in(unit)` and `fromSi()` mark every boundary.
- Torque and energy share a dimension (N m = J); `Torque` is an alias kept for readability.

## Float and double

`DefaultReal` is float. The unqualified aliases and `sub0math::literals` are float; `sub0math::f64` holds
double aliases and literals. Rules:

- Float for rendering, GPU data, firmware and interfaces.
- Double for simulation state, accumulated time, world positions and energy budgets. Evidence: BikeLab's
  float32 solver trial reversed step-refinement trends; a float clock cannot resolve 0.5 ms after a few
  hours; nRF54-class MCUs have single-precision FPUs only, which is why float stays the default.
- Widening (float to double) is implicit; narrowing is explicit. Mixed-precision arithmetic other than
  `+`/`-` (where the float operand widens) does not compile: convert first.
- A translation unit uses one literal namespace; using both is ambiguous by design.

## Deterministic transcendentals

Simulation state must be bit-identical across Windows, Linux and macOS. IEEE-754 fixes `+ - * / sqrt` exactly
but not `sin`, `cos`, `exp`, `pow` and the rest: each C library rounds them differently (a pinned ring digest
matched on every Windows build and differed on Linux and macOS, with libm `sin`/`cos` the only cause).
`sub0math::det` (`number/det_math.hpp`) therefore supplies our own, and state paths use them in place of `<cmath>`.

- **Algorithm:** Sun fdlibm (`__kernel_sin`, `__kernel_cos`, `__ieee754_rem_pio2` with Payne-Hanek for huge
  arguments), the code behind V8's `Math.sin`/`Math.cos`. Results are bit-identical to V8, which gives
  JavaScript parity for ports. Under 0.8 ulp worst case over 60,000 inputs of every magnitude. The fdlibm
  notice is preserved in the header and in `NOTICE.md`; its terms are compatible with the MIT licence.
- **Provided:** `det::sin`, `det::cos`, `det::sincos` (one shared reduction), double and float (float is
  the double result rounded, so it is deterministic too), `constexpr` and `noexcept`; any finite input,
  NaN for infinities and NaN. Not yet provided: `tan`, `atan2`, `exp`, `log`, `pow`; a call site that needs one
  on state adds it here (fdlibm again), not a libm call.
- **Units:** the `Angle`-typed `sin`, `cos` and the new `sincos` call `det::`; `tan` still calls the C library
  and is documented as unfit for state. `sqrt` stays `std::sqrt` (exactly rounded by IEEE).
- **Contract:** only `+ - *` and exact scaling are used, so the bits depend on the compiler not fusing
  multiply-add: build with `sub0math_target_deterministic_fp` (`-ffp-contract=off`). Measured: identical with
  GCC, GCC `-march=x86-64-v3` and Clang `-march=x86-64-v3`.

## Roles

`Role<Tag, T>` wraps a value with a role. Explicit construction, no conversion between roles, no
arithmetic of its own (`value()` returns the underlying quantity or number). Use them wherever a function
takes two or more parameters of the same type, in Sub0 and KismoTech APIs alike. Declare inline:

```cpp
using Height = sub0math::Role<struct HeightTag, Length>;
using RetryCount = sub0math::Role<struct RetryCountTag, std::uint32_t>;
```

## Linear algebra and geometry

- `Vec<T, N>`: aggregate with named components for N = 2..4 (`x y z w`), an array beyond; contiguous,
  same size as N values. Components may be quantities; products follow the dimensions (`dot` of lengths
  is an area, `cross(lever, force)` is a torque). Scaling by a plain number keeps the component type, so
  a float vector stays float.
- `Point<T, N>`: positions with affine rules: point - point is a Vec, point + Vec is a point, point +
  point does not compile.
- `Mat<T, R, C>`: plain numbers, **column-major** (`columns[c]`), matching GPU and Filament layouts.
  Multiplying by a vector of quantities keeps the quantities.
- `Quat<T>`: unit quaternion rotations; `rotate` works on vectors of numbers or quantities.
- `RigidTransform<T, L>`: rotation then translation; `L` is the position type (`f64::Length` for physics).
- `Aabb<T, N>`: inclusive axis-aligned boxes.
- Vectors convert at unit boundaries with `toSi(v)` and `fromSi<Q>(v)` (plain SI numbers and back) and answer `isFinite(v)`.
- **Spatial algebra** for rigid-body dynamics (Featherstone's 6D vectors), added for Sub0Physics'
  multibody core (ADR 0003, step M1a) and grown by use like the shapes:
  - `SpatialMotion<T>` (velocities, accelerations, joint axes) and `SpatialForce<T>` (forces, momenta)
    are separate types: they cannot be added together, and the only product between them is
    `dot(motion, force)`, a power. `cross(v, m)` is Featherstone's `crm`, `cross(v, f)` his `crf`.
  - `SpatialInertia<T>` is compact: mass, first moment `h = m c` and rotational inertia about the frame
    origin (13 numbers, not 36); `fromCentreOfMass` applies the parallel-axis theorem, sums are
    component-wise, and `inertia * motion` is a force.
  - `PluckerTransform<T>` (Featherstone's `B_X_A`: rotation `E` and translation `r`) is made from a
    `RigidTransform` pose and applies to motions as `X`, to forces as the dual `X*`, and in reverse
    (`applyInverse`: `X^-1` for motions, `X^T` for forces, `X^T I X` for inertias). Composition and
    `inverse` follow `RigidTransform`. `toRigidTransform(x)` recovers the pose (using
    `Quat::fromRotationMatrix`, Shepperd's method), so a pose can be composed in either form.
  - Components are plain SI numbers, like `Mat`: one type carries velocities, accelerations and unit
    axes, so units belong to the interfaces that build them. Use double for dynamics.

## Shapes

Shapes are values in their own frame, generic over the coordinate type. They are added when a consumer
needs them, and the API is refined by using it (ADR 0002).

| Shape | Queries | First consumer |
|---|---|---|
| `Sphere<T, N>` (`Circle<T>`) | `contains`, `boundingBox` | Terrain clearance probe |
| `Segment<T, N>` | `closestPoint` (with exact end parameters), `closestPoints` between two segments (closed form, any N, exact end parameters), `boundingBox` | Terrain clearance probe; casing edge distances |
| `Aabb<T, N>` | `contains`, `intersects`, `including` | Bounds everywhere |
| `OrientedBox<T, N>` | `closestPoint`, `contains`, `boundingBox`, `overlaps` with boxes (separating axes, 2D and 3D); `AabbOverlapTester` for one box against many blocks | Terrain corridor sweep boxes and coverage blocks |
| `Capsule<T, N>` | `signedDistance`, `contains`, `boundingBox`; `contact` with a `Segment` (any N) and a `ConvexPolygon` (2D), returning `CapsuleContact` (signed gap, axis point, capsule point and a `Witness` on the other shape) | Wheel interaction envelope; casing edges against terrain pieces |
| `ConvexPolygon<T, K>` | Fixed vertex count, counter-clockwise, with outward unit edge normals (`fromVertices`); `vertexNormalCone` (`NormalCone::contains`), `boundingBox` | Terrain pieces (prisms of a polyline) for exact casing contact |
| `Witness<T, N>` | `{point, normal, kind, index}` with `WitnessKind` `Vertex`, `Edge` or `Face`: the feature that answers a query, never an averaged normal | Every nearest-point and contact query; Sub0Physics' `Witness2`/`Witness3` are aliases of it |
| `Plane<T, N>` | `signedDistance`, `closestPoint`, `Plane::through` | Ground and ramp references |
| `Triangle<T, N>` | `closestPoint` reporting the vertex, edge or face it lies on; `normal`; `boundingBox` | Exact terrain height queries |
| `Ray<T, N>` | `intersect` with `Triangle` (Moller–Trumbore) and `Plane` | Rays along a wheel's support normal |
| `RegularGrid<T, N>` | `nodeAt`; ownership `cellOf` (half-open, far edge clamps, returns `GridCell{index, known}`); coverage `cellsTouching(Aabb)` (closed: touching counts). Non-finite or out-of-range input gives `known == false` or an empty range, never an integer conversion | Terrain pages and coverage blocks |

Directions (`Plane::normal`, `Ray::direction`, box axes) are plain unit vectors; only positions and
extents carry the coordinate unit, so a length can never be passed as a direction. The shapes added on
2026-10-16 serve the terrain plan's groundwork (steps G1–G3), whose needs were measured first by the D0
experiment. The contact shapes added on 2026-10-27 (step M2b of the deformable wheel) are planar first: the polygon
contact is 2D and floating-point only, while the segment pair, the capsule-segment contact and `Witness` work in any
dimension and with quantities. Converting a shape to
triangles will be a function (`toMesh(shape, ...)`), not a type. Purposes that carry their own data
(collision materials, render materials) belong to the layer that owns that data.

## Performance rules

- Everything is `constexpr` where the standard library allows, `noexcept`, trivially copyable and the size
  of its components; static assertions pin it.
- Floating-point results must not depend on whether the build has FMA: tests and benchmarks use
  `sub0math_target_deterministic_fp` (see [PERFORMANCE.md](PERFORMANCE.md#floating-point-determinism)).
- Paired benchmarks prove that quantities cost the same as plain numbers (`dot, double` vs
  `dot, f64::Length`); budgets per host catch regressions. See [PERFORMANCE.md](PERFORMANCE.md).
- No hand-written SIMD yet: named-component vectors in arrays vectorize well (the float axpy benchmark
  runs about 2.5 times the double one). SIMD layouts (structure of arrays) belong to the consumer's
  hot loops, not to the vocabulary types.

## Planned, with their first consumer

| Item | Trigger |
|---|---|
| Fixed-point `Rep` | KismoEdge firmware math |
| Electric current, amount of substance | KismoEdge sensor drivers |
| `interop/` converters for Filament, glm, Eigen | KismoEngine / Crucible renderer adoption |
| `std::format` support with unit symbols | First logging consumer (Sub0Log) |
| Adoption in Sub0HexGrid (`Point` becomes `Point2d`) | Coordinated with Crucible's pin |
