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
 * @param segment A segment of non-zero length. Callers that can meet a point (a degenerate capsule)
 *        handle it themselves, so this hot query carries no check for it.
 * @param p The query point.
 * @return The nearest point and its parameter.
 */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
// C4723 "potential divide by 0": MSVC reports it after constant-folding a zero-length segment into a
// branch its caller has already excluded (a degenerate Capsule). A false positive.
#pragma warning(disable : 4723)
#endif
// PERF: force-inlined; GCC outlining callers' loops cost 60%.
// PERF: no zero-length check: branch 1.5x, max 1.06x slower.
template<typename T, std::size_t N>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SegmentPoint<T, N> closestPoint(const Segment<T, N>& segment, const Point<T, N>& p) noexcept
{
    using Scalar = ScalarOf<T>;
    const Vec<T, N> ab = segment.b - segment.a;
    const Scalar t = std::clamp(static_cast<Scalar>(dot(p - segment.a, ab) / dot(ab, ab)), Scalar{0}, Scalar{1});
    return {segment.a + ab * t, t};
}

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

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
