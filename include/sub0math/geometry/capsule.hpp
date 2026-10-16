#pragma once

#include <cstddef>

#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/geometry/segment.hpp"
#include "sub0math/linear/vec.hpp"

namespace sub0math
{

/**
 * Every point within `radius` of a segment: a cylinder with hemispherical ends (a stadium in 2D).
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct Capsule
{
    Segment<T, N> axis; ///< Core segment; may have zero length (then it is a sphere), handled here.
    T radius;           ///< Radius; not negative.

    friend constexpr bool operator==(const Capsule&, const Capsule&) = default;
};

/**
 * Measures how far a point is outside a capsule; negative inside.
 *
 * @param capsule The capsule.
 * @param p The point.
 * @return Distance to the surface: positive outside, zero on it, negative inside.
 */
template<typename T, std::size_t N>
[[nodiscard]] T signedDistance(const Capsule<T, N>& capsule, const Point<T, N>& p) noexcept
{
    const Point<T, N> nearest = capsule.axis.a == capsule.axis.b ? capsule.axis.a : closestPoint(capsule.axis, p).point;
    return length(p - nearest) - capsule.radius;
}

/**
 * Reports whether a point is inside or on the boundary.
 *
 * @param capsule The capsule.
 * @param p The point.
 * @return True when the point is within `radius` of the axis.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr bool contains(const Capsule<T, N>& capsule, const Point<T, N>& p) noexcept
{
    const Point<T, N> nearest = capsule.axis.a == capsule.axis.b ? capsule.axis.a : closestPoint(capsule.axis, p).point;
    return lengthSquared(p - nearest) <= capsule.radius * capsule.radius;
}

/**
 * Computes the smallest axis-aligned box around a capsule.
 *
 * @param capsule The capsule.
 * @return Its bounding box.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr Aabb<T, N> boundingBox(const Capsule<T, N>& capsule) noexcept
{
    const Aabb<T, N> core = boundingBox(capsule.axis);
    Vec<T, N> extent{};
    detail::forEachComponent<N>([&](auto i) { extent.template get<i>() = capsule.radius; });
    return {core.lo - extent, core.hi + extent};
}

} // namespace sub0math
