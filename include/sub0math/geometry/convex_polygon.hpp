#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

namespace detail
{

// z component of the 3D cross product of two planar vectors: the sine of the angle from a to b, times the lengths.
template<typename T>
[[nodiscard]] constexpr T cross2(const Vec<T, 2>& a, const Vec<T, 2>& b) noexcept
{
    return a.x * b.y - a.y * b.x;
}

} // namespace detail

/**
 * A convex polygon of a fixed number of vertices, counter-clockwise, with the outward unit normal of each edge.
 *
 * Edge `i` runs from `vertices[i]` to `vertices[(i + 1) % K]` and has the normal `normals[i]`. The vertex count is
 * a compile-time constant so queries unroll and nothing allocates; terrain pieces and boxes are small. The
 * polygon is strictly convex (no zero-length edge, no turn of 180 degrees or more at a vertex, no collinear
 * triple): the constructors do not check it.
 *
 * @tparam T Coordinate type: a floating-point number.
 * @tparam K Number of vertices, at least 3.
 */
template<typename T, std::size_t K>
struct ConvexPolygon
{
    static_assert(Real<T>, "ConvexPolygon holds plain floating-point coordinates");
    static_assert(K >= 3, "a polygon has at least three vertices");

    std::array<Point<T, 2>, K> vertices; ///< Corners, counter-clockwise.
    std::array<Vec<T, 2>, K> normals;    ///< Outward unit normal of the edge that starts at the same index.

    /**
     * Makes the polygon of counter-clockwise vertices, computing the edge normals.
     *
     * @param corners Vertices of a strictly convex polygon in counter-clockwise order.
     * @return The polygon.
     */
    [[nodiscard]] static ConvexPolygon fromVertices(const std::array<Point<T, 2>, K>& corners) noexcept
    {
        ConvexPolygon polygon{corners, {}};
        for (std::size_t i = 0; i < K; ++i)
        {
            const Vec<T, 2> edge = corners[(i + 1) % K] - corners[i];
            const T inverse = T{1} / std::sqrt(dot(edge, edge));
            polygon.normals[i] = {edge.y * inverse, -edge.x * inverse};
        }
        return polygon;
    }

    friend constexpr bool operator==(const ConvexPolygon&, const ConvexPolygon&) = default;
};

/**
 * The directions a vertex of a convex polygon can push along: every unit vector turning counter-clockwise from
 * `from` to `to`, the normals of the two edges that meet there. A contact at the vertex is admissible when its
 * normal lies in the cone; no unique normal exists.
 *
 * @tparam T Component type.
 */
template<typename T>
struct NormalCone
{
    Vec<T, 2> from; ///< Normal of the edge that ends at the vertex.
    Vec<T, 2> to;   ///< Normal of the edge that starts at the vertex; the cone turns counter-clockwise from `from` to `to`.

    /**
     * Tests whether a direction lies in the cone, with slack on both bounds.
     *
     * @param direction A unit direction.
     * @param angleTolerance Allowed excess beyond either bound, in radians (small angles; it is a sine).
     * @return True when the direction is within the cone or within the tolerance of it.
     */
    [[nodiscard]] constexpr bool contains(const Vec<T, 2>& direction, T angleTolerance = T{0}) const noexcept
    {
        return detail::cross2(from, direction) >= -angleTolerance && detail::cross2(direction, to) >= -angleTolerance;
    }
};

/**
 * Gives the cone of normals at a vertex of a convex polygon.
 *
 * @param polygon The polygon.
 * @param vertex Vertex index, less than K.
 * @return The cone between the normals of the edges before and after the vertex.
 */
template<typename T, std::size_t K>
[[nodiscard]] constexpr NormalCone<T> vertexNormalCone(const ConvexPolygon<T, K>& polygon, std::size_t vertex) noexcept
{
    return {polygon.normals[(vertex + K - 1) % K], polygon.normals[vertex]};
}

/**
 * Computes the smallest axis-aligned box around a polygon.
 *
 * @param polygon The polygon.
 * @return Its bounding box.
 */
template<typename T, std::size_t K>
[[nodiscard]] constexpr Aabb<T, 2> boundingBox(const ConvexPolygon<T, K>& polygon) noexcept
{
    Aabb<T, 2> box{polygon.vertices[0], polygon.vertices[0]};
    for (std::size_t i = 1; i < K; ++i)
    {
        box = box.including(polygon.vertices[i]);
    }
    return box;
}

} // namespace sub0math
