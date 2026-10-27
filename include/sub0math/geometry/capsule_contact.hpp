#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "sub0math/config.hpp"
#include "sub0math/geometry/capsule.hpp"
#include "sub0math/geometry/convex_polygon.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/geometry/segment.hpp"
#include "sub0math/geometry/witness.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * How a capsule meets a shape: the signed gap, where on each body, and the witness on the shape.
 *
 * @tparam T Coordinate type.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct CapsuleContact
{
    T gap;                    ///< Capsule surface to shape: positive apart, zero touching, negative overlapping.
    T axisDistance;           ///< Axis to shape; negative when the axis is inside the shape (minus its depth).
    ScalarOf<T> axisT;        ///< Position of `axisPoint` along the axis: exactly 0 at `a`, exactly 1 at `b`.
    bool axisCrosses;         ///< The axis itself touches or enters the shape (deep penetration, not contact).
    Point<T, N> axisPoint;    ///< The point of the axis nearest the shape (the deepest one when the axis crosses).
    Point<T, N> capsulePoint; ///< The capsule surface point facing the shape: `axisPoint` less `radius` along the normal.
    Witness<T, N> witness;    ///< The shape's feature and point; its normal points from the shape towards the capsule.
};

/**
 * Measures a capsule against a segment: the gap, the nearest points and the segment's feature.
 *
 * The witness is a `Vertex` (index 0 or 1) when the nearest point is an end of the segment, else the `Edge` (index
 * 0), with the true direction from the segment to the axis as its normal; nothing is averaged. An axis that meets
 * the segment has distance zero, so `gap` is minus the radius (not a penetration depth: use a polygon for that); in
 * 2D the normal is then the segment's normal on the side of the axis end farther from its line, in 3D the zero
 * vector.
 *
 * @param capsule The capsule.
 * @param segment The segment.
 * @return The contact.
 */
template<typename T, std::size_t N>
[[nodiscard]] CapsuleContact<T, N> contact(const Capsule<T, N>& capsule, const Segment<T, N>& segment) noexcept
{
    using Scalar = ScalarOf<T>;
    const SegmentPair<T, N> pair = closestPoints(capsule.axis, segment);
    const Vec<T, N> separation = pair.first - pair.second;
    const T distance = length(separation);
    Vec<Scalar, N> normal{};
    const bool crosses = !(distance > T{});
    if (crosses)
    {
        if constexpr (N == 2)
        {
            const Vec<T, 2> direction = segment.b - segment.a;
            const Vec<T, 2> perpendicular{-direction.y, direction.x};
            if (dot(direction, direction) > ProductType<T, T>{})
            {
                const auto sideA = dot(perpendicular, capsule.axis.a - segment.a);
                const auto sideB = dot(perpendicular, capsule.axis.b - segment.a);
                const Vec<Scalar, 2> left = normalized(perpendicular);
                using std::abs;
                normal = (abs(sideB) > abs(sideA) ? sideB : sideA) < decltype(sideA){} ? -left : left;
            }
            else
            {
                normal = {Scalar{1}, Scalar{0}};
            }
        }
    }
    else
    {
        detail::forEachComponent<N>([&](auto i) {
            normal.template get<i>() = static_cast<Scalar>(separation.template get<i>() / distance);
        });
    }
    const bool atEnd = pair.secondT == Scalar{0} || pair.secondT == Scalar{1};
    const std::size_t index = pair.secondT == Scalar{1} ? 1U : 0U;
    return {distance - capsule.radius,
            distance,
            pair.firstT,
            crosses,
            pair.first,
            pair.first - normal * capsule.radius,
            {pair.second, normal, atEnd ? WitnessKind::Vertex : WitnessKind::Edge, index}};
}

/**
 * Measures a capsule against a convex polygon in closed form: no iteration, no allocation.
 *
 * While the axis is clear of the polygon the distance is exact: the nearest pair always holds an end of the axis or
 * a vertex of the polygon, so each end is tried against the edges it faces, and the vertex nearest the axis's line
 * against the axis interior. The witness is the polygon's feature that answers: an `Edge` with its face normal when
 * the nearest point is inside an edge, or a `Vertex` with the true direction from the vertex to the axis point (a
 * direction inside `vertexNormalCone`); a vertex is never given an averaged normal. `gap` is the axis distance less
 * the radius.
 *
 * When the axis touches or enters the polygon (`axisCrosses`), the axis distance is minus the smallest translation
 * that separates the axis (the separating-axis result over the polygon's edge normals and the axis's normal, which
 * is complete for a segment and a convex polygon). The witness is then the edge of that axis, with the point on the
 * edge's line opposite the deepest end, or the vertex that pokes furthest through the axis's line. That state is deep
 * penetration, not contact; callers normally reject or split the step before it.
 *
 * @param capsule The capsule; a zero-length axis is a disc.
 * @param polygon The polygon.
 * @return The contact.
 */
template<typename T, std::size_t K>
// PERF: force-inlined; out of line 1.3x slower than twin.
[[nodiscard]] SUB0MATH_FORCE_INLINE CapsuleContact<T, 2> contact(const Capsule<T, 2>& capsule, const ConvexPolygon<T, K>& polygon) noexcept
{
    static_assert(Real<T>, "polygon contact works on plain floating-point coordinates");
    using Pt = Point<T, 2>;
    using V = Vec<T, 2>;
    constexpr T Infinity = std::numeric_limits<T>::infinity();

    const Pt a = capsule.axis.a;
    const Pt b = capsule.axis.b;
    const V ab = b - a;
    const T abLength2 = dot(ab, ab);
    const bool hasDirection = abLength2 > T{0};

    // Signed distance of each end from each edge's line (positive outside), and the best polygon-edge axis.
    std::array<T, K> outsideA;
    std::array<T, K> outsideB;
    T edgeGap = -Infinity;
    std::size_t edgeAxis = 0;
    for (std::size_t i = 0; i < K; ++i)
    {
        outsideA[i] = dot(polygon.normals[i], a - polygon.vertices[i]);
        outsideB[i] = dot(polygon.normals[i], b - polygon.vertices[i]);
        const T gap = std::min(outsideA[i], outsideB[i]);
        if (gap > edgeGap)
        {
            edgeGap = gap;
            edgeAxis = i;
        }
    }

    // Offsets of the vertices from the axis's line, scaled by |ab| (the line's normal is `perpendicular`).
    const V perpendicular{-ab.y, ab.x};
    T lowest = Infinity;
    T highest = -Infinity;
    std::size_t lowestVertex = 0;
    std::size_t highestVertex = 0;
    for (std::size_t k = 0; k < K; ++k)
    {
        const T offset = dot(perpendicular, polygon.vertices[k] - a);
        if (offset < lowest)
        {
            lowest = offset;
            lowestVertex = k;
        }
        if (offset > highest)
        {
            highest = offset;
            highestVertex = k;
        }
    }

    const bool lineMisses = hasDirection && (lowest > T{0} || highest < T{0});
    if (edgeGap > T{0} || lineMisses)
    {
        // The axis is clear of the polygon: the nearest pair holds an end of the axis or a vertex of the polygon.
        T bestSquared = Infinity;
        Pt bestOnAxis = a;
        Pt bestOnPolygon = a;
        WitnessKind bestKind = WitnessKind::Edge;
        std::size_t bestIndex = 0;
        T bestT = T{0};
        const auto tryEnd = [&](const Pt& end, const std::array<T, K>& outside, T endT) {
            for (std::size_t i = 0; i < K; ++i)
            {
                if (!(outside[i] > T{0}))
                {
                    continue; // The end is behind this edge's line: the edge cannot hold its nearest point.
                }
                const std::size_t next = i + 1 == K ? 0 : i + 1;
                const Pt& from = polygon.vertices[i];
                const V edge = polygon.vertices[next] - from;
                const T along = std::clamp(dot(end - from, edge) / dot(edge, edge), T{0}, T{1});
                const Pt onEdge = along == T{0} ? from : (along == T{1} ? polygon.vertices[next] : from + edge * along);
                const T squared = lengthSquared(end - onEdge);
                if (squared < bestSquared)
                {
                    bestSquared = squared;
                    bestOnAxis = end;
                    bestOnPolygon = onEdge;
                    bestT = endT;
                    bestKind = along == T{0} || along == T{1} ? WitnessKind::Vertex : WitnessKind::Edge;
                    bestIndex = along == T{1} ? next : i;
                }
            }
        };
        tryEnd(a, outsideA, T{0});
        tryEnd(b, outsideB, T{1});
        if (lineMisses)
        {
            // A vertex can face the interior of the axis: the support vertex on the polygon's side of the line.
            const std::size_t vertex = lowest > T{0} ? lowestVertex : highestVertex;
            const T along = dot(polygon.vertices[vertex] - a, ab) / abLength2;
            if (along > T{0} && along < T{1})
            {
                const Pt onAxis = a + ab * along;
                const T squared = lengthSquared(onAxis - polygon.vertices[vertex]);
                if (squared < bestSquared)
                {
                    bestSquared = squared;
                    bestOnAxis = onAxis;
                    bestOnPolygon = polygon.vertices[vertex];
                    bestT = along;
                    bestKind = WitnessKind::Vertex;
                    bestIndex = vertex;
                }
            }
        }
        if (bestSquared > T{0} && bestSquared < Infinity)
        {
            const T distance = std::sqrt(bestSquared);
            const V normal =
                bestKind == WitnessKind::Edge ? polygon.normals[bestIndex] : (bestOnAxis - bestOnPolygon) / distance;
            return {distance - capsule.radius,
                    distance,
                    bestT,
                    false,
                    bestOnAxis,
                    bestOnAxis - normal * capsule.radius,
                    {bestOnPolygon, normal, bestKind, bestIndex}};
        }
        // Rounding put both ends behind every edge line (the axis grazes the polygon): treated as touching below.
    }

    // The axis touches or enters the polygon: the smallest separating translation among the K + 1 axes.
    T axisGap = edgeGap;
    bool useEdge = true;
    bool positiveSide = false;
    T inverseLength = T{0};
    if (hasDirection)
    {
        inverseLength = T{1} / std::sqrt(abLength2);
        const T plus = -highest * inverseLength; // The capsule on the +perpendicular side of the polygon.
        const T minus = lowest * inverseLength;  // The capsule on the -perpendicular side.
        const T segmentGap = std::max(plus, minus);
        if (segmentGap > edgeGap)
        {
            axisGap = segmentGap;
            useEdge = false;
            positiveSide = plus >= minus;
        }
    }
    if (useEdge)
    {
        const bool endA = outsideA[edgeAxis] <= outsideB[edgeAxis];
        const Pt deepest = endA ? a : b;
        const V normal = polygon.normals[edgeAxis];
        return {axisGap - capsule.radius,
                axisGap,
                endA ? T{0} : T{1},
                true,
                deepest,
                deepest - normal * capsule.radius,
                {deepest - normal * axisGap, normal, WitnessKind::Edge, edgeAxis}};
    }
    const std::size_t vertex = positiveSide ? highestVertex : lowestVertex;
    const V normal = positiveSide ? perpendicular * inverseLength : -(perpendicular * inverseLength);
    const T along = std::clamp(dot(polygon.vertices[vertex] - a, ab) / abLength2, T{0}, T{1});
    const Pt onAxis = along == T{0} ? a : (along == T{1} ? b : a + ab * along);
    return {axisGap - capsule.radius,
            axisGap,
            along,
            true,
            onAxis,
            onAxis - normal * capsule.radius,
            {polygon.vertices[vertex], normal, WitnessKind::Vertex, vertex}};
}

} // namespace sub0math
