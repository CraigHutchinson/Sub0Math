# Design

Intent: one small, fast, dependency-free math vocabulary for every Sub0 library and every KismoTech
project, in which unit and argument-order mistakes do not compile. Accepted in
[ADR 0001](https://github.com/CraigHutchinson/.github/blob/main/docs/decisions/0001-sub0math.md).

## Scope

In: value types and their algebra. Numbers (`number/`), dimensioned quantities and units (`units/`),
named roles (`role/`), vectors, matrices, quaternions and rigid transforms (`linear/`), points and boxes
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

## Shapes

Shapes are values in their own frame, generic over the coordinate type: `Sphere<T, N>` (`Circle<T>`),
`Segment<T, N>` with `closestPoint`, and `Aabb<T, N>`, each with `boundingBox` where it applies. They are
added when a consumer needs them, and the API is refined by using it (ADR 0002). Converting a shape to
triangles will be a function (`toMesh(shape, ...)`), not a type. Purposes that carry their own data
(collision materials, render materials) belong to the layer that owns that data.

## Performance rules

- Everything is `constexpr` where the standard library allows, `noexcept`, trivially copyable and the size
  of its components; static assertions pin it.
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
