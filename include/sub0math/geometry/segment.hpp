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
 * The nearest pair of points between two segments, and where each lies along its segment.
 *
 * @tparam T Coordinate type.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct SegmentPair
{
    Point<T, N> first;   ///< Nearest point on the first segment.
    Point<T, N> second;  ///< Nearest point on the second segment.
    ScalarOf<T> firstT;  ///< Position along the first segment: exactly 0 at `a`, exactly 1 at `b`.
    ScalarOf<T> secondT; ///< Position along the second segment, likewise.
};

/**
 * Finds the nearest pair of points between two segments, in closed form (Ericson, Real-Time Collision
 * Detection, 5.1.9), in any dimension. Segments that cross (in 2D) are at distance zero, with the crossing as
 * both points. A pair at equal distance everywhere along an overlap (parallel segments) reports the pair that
 * starts at the first segment's `a`, deterministically. Either segment may be a point.
 *
 * `firstT` and `secondT` are exactly 0 or 1 at an end and the returned point is then the end itself, so a
 * caller can classify the feature (vertex or interior) from the parameter alone.
 *
 * @param s1 The first segment.
 * @param s2 The second segment.
 * @return The nearest points and their parameters.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr SegmentPair<T, N> closestPoints(const Segment<T, N>& s1, const Segment<T, N>& s2) noexcept
{
    using Scalar = ScalarOf<T>;
    using Area = ProductType<T, T>;
    // A pair whose directions differ by less than this fraction of |d1||d2| (sine squared) counts as parallel.
    constexpr Scalar ParallelFraction = Scalar{1e-14};

    const Vec<T, N> d1 = s1.b - s1.a;
    const Vec<T, N> d2 = s2.b - s2.a;
    const Vec<T, N> r = s1.a - s2.a;
    const Area a = dot(d1, d1);
    const Area e = dot(d2, d2);
    const Area f = dot(d2, r);
    const Area zero{};
    Scalar s{0};
    Scalar t{0};
    if (a == zero)
    {
        if (e != zero)
        {
            t = std::clamp(static_cast<Scalar>(f / e), Scalar{0}, Scalar{1});
        }
    }
    else
    {
        const Area c = dot(d1, r);
        if (e == zero)
        {
            s = std::clamp(static_cast<Scalar>(-c / a), Scalar{0}, Scalar{1});
        }
        else
        {
            const Area b = dot(d1, d2);
            const auto denom = a * e - b * b;
            if (denom > ParallelFraction * (a * e))
            {
                s = std::clamp(static_cast<Scalar>((b * f - c * e) / denom), Scalar{0}, Scalar{1});
            }
            t = static_cast<Scalar>((b * s + f) / e);
            if (t < Scalar{0})
            {
                t = Scalar{0};
                s = std::clamp(static_cast<Scalar>(-c / a), Scalar{0}, Scalar{1});
            }
            else if (t > Scalar{1})
            {
                t = Scalar{1};
                s = std::clamp(static_cast<Scalar>((b - c) / a), Scalar{0}, Scalar{1});
            }
        }
    }
    const Point<T, N> p1 = s == Scalar{0} ? s1.a : (s == Scalar{1} ? s1.b : s1.a + d1 * s);
    const Point<T, N> p2 = t == Scalar{0} ? s2.a : (t == Scalar{1} ? s2.b : s2.a + d2 * t);
    return {p1, p2, s, t};
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
