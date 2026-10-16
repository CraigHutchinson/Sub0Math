#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "sub0math/config.hpp"
#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A triangle with vertices a, b, c. In 3D, counter-clockwise seen from the front: its normal is
 * (b - a) x (c - a).
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct Triangle
{
    Point<T, N> a; ///< First vertex.
    Point<T, N> b; ///< Second vertex.
    Point<T, N> c; ///< Third vertex.

    friend constexpr bool operator==(const Triangle&, const Triangle&) = default;
};

/** Which part of a triangle a nearest point lies on. Vertex and edge results have no unique normal. */
enum class TriangleFeature : std::uint8_t
{
    VertexA,
    VertexB,
    VertexC,
    EdgeAB,
    EdgeBC,
    EdgeCA,
    Face
};

/**
 * The point of a triangle nearest to a query, and the feature it lies on.
 *
 * @tparam T Coordinate type.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct TrianglePoint
{
    Point<T, N> point;       ///< The nearest point.
    TriangleFeature feature; ///< Vertex, edge or interior of the face.
};

/**
 * Finds the point of a triangle nearest to a query point, by its Voronoi regions (Ericson, Real-Time
 * Collision Detection, 5.1.5). The triangle must not be degenerate.
 *
 * @param tri The triangle.
 * @param p The query point.
 * @return The nearest point and the feature it lies on.
 */
template<typename T, std::size_t N>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr TrianglePoint<T, N> closestPoint(const Triangle<T, N>& tri,
                                                                              const Point<T, N>& p) noexcept
{
    using S = ScalarOf<T>;
    using A = ProductType<T, T>;
    const A zero{};
    const Vec<T, N> ab = tri.b - tri.a;
    const Vec<T, N> ac = tri.c - tri.a;
    const Vec<T, N> ap = p - tri.a;
    const A d1 = dot(ab, ap);
    const A d2 = dot(ac, ap);
    if (d1 <= zero && d2 <= zero) return {tri.a, TriangleFeature::VertexA};

    const Vec<T, N> bp = p - tri.b;
    const A d3 = dot(ab, bp);
    const A d4 = dot(ac, bp);
    if (d3 >= zero && d4 <= d3) return {tri.b, TriangleFeature::VertexB};

    const auto vc = d1 * d4 - d3 * d2;
    if (vc <= decltype(vc){} && d1 >= zero && d3 <= zero)
        return {tri.a + ab * static_cast<S>(d1 / (d1 - d3)), TriangleFeature::EdgeAB};

    const Vec<T, N> cp = p - tri.c;
    const A d5 = dot(ab, cp);
    const A d6 = dot(ac, cp);
    if (d6 >= zero && d5 <= d6) return {tri.c, TriangleFeature::VertexC};

    const auto vb = d5 * d2 - d1 * d6;
    if (vb <= decltype(vb){} && d2 >= zero && d6 <= zero)
        return {tri.a + ac * static_cast<S>(d2 / (d2 - d6)), TriangleFeature::EdgeCA};

    const auto va = d3 * d6 - d5 * d4;
    if (va <= decltype(va){} && (d4 - d3) >= zero && (d5 - d6) >= zero)
        return {tri.b + (tri.c - tri.b) * static_cast<S>((d4 - d3) / ((d4 - d3) + (d5 - d6))), TriangleFeature::EdgeBC};

    const auto sum = va + vb + vc;
    return {tri.a + ab * static_cast<S>(vb / sum) + ac * static_cast<S>(vc / sum), TriangleFeature::Face};
}

/**
 * Computes the unit normal of a 3D triangle: (b - a) x (c - a), normalised.
 *
 * @param tri A non-degenerate triangle.
 * @return Its front-facing unit normal.
 */
template<typename T>
[[nodiscard]] Vec<ScalarOf<T>, 3> normal(const Triangle<T, 3>& tri) noexcept
{
    return normalized(cross(tri.b - tri.a, tri.c - tri.a));
}

/**
 * Computes the smallest axis-aligned box around a triangle.
 *
 * @param tri The triangle.
 * @return Its bounding box.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr Aabb<T, N> boundingBox(const Triangle<T, N>& tri) noexcept
{
    return Aabb<T, N>::fromCorners(tri.a, tri.b).including(tri.c);
}

} // namespace sub0math
