#pragma once

#include <algorithm>
#include <cstddef>

#include "sub0math/config.hpp"
#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A straight line segment from `a` to `b`.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct Segment
{
    Point<T, N> a; ///< Start.
    Point<T, N> b; ///< End.

    friend constexpr bool operator==(const Segment&, const Segment&) = default;
};

/**
 * The point of a segment nearest to a query, and where it lies along the segment.
 *
 * @tparam T Coordinate type.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct SegmentPoint
{
    Point<T, N> point; ///< The nearest point.
    ScalarOf<T> t;     ///< Position along the segment: exactly 0 at `a`, exactly 1 at `b`.
};

/**
 * Finds the point of a segment nearest to a query point.
 *
 * `t` is clamped to [0, 1], so a result at an end is exactly 0 or 1 and identifies a vertex.
 *
 * @param segment A segment of non-zero length.
 * @param p The query point.
 * @return The nearest point and its parameter.
 */
template<typename T, std::size_t N>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SegmentPoint<T, N> closestPoint(const Segment<T, N>& segment, const Point<T, N>& p) noexcept
{
    using Scalar = ScalarOf<T>;
    const Vec<T, N> ab = segment.b - segment.a;
    const Scalar t = std::clamp(static_cast<Scalar>(dot(p - segment.a, ab) / dot(ab, ab)), Scalar{0}, Scalar{1});
    return {segment.a + ab * t, t};
}

/**
 * Computes the smallest axis-aligned box around a segment.
 *
 * @param segment The segment.
 * @return Its bounding box.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr Aabb<T, N> boundingBox(const Segment<T, N>& segment) noexcept
{
    return Aabb<T, N>::fromCorners(segment.a, segment.b);
}

} // namespace sub0math
