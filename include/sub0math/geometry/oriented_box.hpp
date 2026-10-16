#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include "sub0math/config.hpp"
#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/mat.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"
#include "sub0math/units/functions.hpp"

namespace sub0math
{

/**
 * A box with its own orientation: a centre, half extents along its local axes, and those axes.
 *
 * Like every shape it is a value in the frame that contains it; a body's pose places it.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct OrientedBox
{
    using Axes = Mat<ScalarOf<T>, N, N>;

    Point<T, N> centre;              ///< Centre.
    Vec<T, N> halfExtents;           ///< Half size along each local axis; not negative.
    Axes axes = Axes::identity();    ///< Column i is local axis i in the containing frame; orthonormal.

    friend constexpr bool operator==(const OrientedBox&, const OrientedBox&) = default;
};

/**
 * Makes the oriented box equal to an axis-aligned one.
 *
 * @param box The axis-aligned box.
 * @return The same box with identity axes.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr OrientedBox<T, N> toOrientedBox(const Aabb<T, N>& box) noexcept
{
    const Vec<T, N> half = (box.hi - box.lo) / 2;
    return {box.lo + half, half, OrientedBox<T, N>::Axes::identity()};
}

/**
 * Finds the point of a box nearest to a query point. A point inside is its own nearest point.
 *
 * @param box The box.
 * @param p The query point.
 * @return The nearest point of the solid box.
 */
template<typename T, std::size_t N>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr Point<T, N> closestPoint(const OrientedBox<T, N>& box,
                                                                      const Point<T, N>& p) noexcept
{
    const Vec<T, N> d = p - box.centre;
    Point<T, N> q = box.centre;
    detail::forEachComponent<N>([&](auto i) {
        const T h = box.halfExtents.template get<i>();
        q += box.axes.columns[i] * std::clamp(dot(box.axes.columns[i], d), -h, h);
    });
    return q;
}

/**
 * Reports whether a point is inside or on the boundary.
 *
 * @param box The box.
 * @param p The point.
 * @return True when the point's local coordinates are within the half extents.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr bool contains(const OrientedBox<T, N>& box, const Point<T, N>& p) noexcept
{
    const Vec<T, N> d = p - box.centre;
    bool inside = true;
    detail::forEachComponent<N>([&](auto i) {
        const T l = dot(box.axes.columns[i], d);
        const T h = box.halfExtents.template get<i>();
        inside = inside && !(l < -h || l > h);
    });
    return inside;
}

/**
 * Computes the smallest axis-aligned box around an oriented box.
 *
 * @param box The box.
 * @return Its bounding box.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr Aabb<T, N> boundingBox(const OrientedBox<T, N>& box) noexcept
{
    Vec<T, N> extent{};
    detail::forEachComponent<N>([&](auto row) {
        T e{};
        detail::forEachComponent<N>([&](auto column) {
            const ScalarOf<T> a = box.axes.columns[column].template get<row>();
            e += (a < ScalarOf<T>{0} ? -a : a) * box.halfExtents.template get<column>();
        });
        extent.template get<row>() = e;
    });
    return {box.centre - extent, box.centre + extent};
}

namespace detail
{

/**
 * The separating-axis test given b's orientation and centre expressed in a's frame.
 *
 * @param r r[i][j] = a.axis_i . b.axis_j.
 * @param ea a's half extents.
 * @param eb b's half extents.
 * @param t b's centre minus a's centre, in a's frame.
 * @return True when no axis separates the boxes.
 */
template<typename T, std::size_t N>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr bool separatingAxesOverlap(const std::array<std::array<ScalarOf<T>, N>, N>& r,
                                                                        const Vec<T, N>& ea, const Vec<T, N>& eb,
                                                                        const std::array<T, N>& t) noexcept
{
    using S = ScalarOf<T>;
    const auto absolute = [](auto x) {
        using std::abs; // quantities find sub0math::abs by argument lookup
        return abs(x);
    };
    const S tolerance = std::numeric_limits<S>::epsilon() * S{16};
    std::array<std::array<S, N>, N> absR{}; // |r| + tolerance: near-parallel edges cannot fake a separation
    for (std::size_t i = 0; i < N; ++i)
    {
        for (std::size_t j = 0; j < N; ++j) absR[i][j] = absolute(r[i][j]) + tolerance;
    }

    // Face axes without early exits: every test is evaluated and combined with a bitwise or. In a loop
    // over many candidates this stays branch-free, so the compiler can vectorise across candidates.
    bool separated = false;
    for (std::size_t i = 0; i < N; ++i) // a's face axes
    {
        T rb{};
        for (std::size_t j = 0; j < N; ++j) rb += eb[j] * absR[i][j];
        separated |= absolute(t[i]) > ea[i] + rb;
    }
    for (std::size_t j = 0; j < N; ++j) // b's face axes
    {
        T ra{};
        T tj{};
        for (std::size_t i = 0; i < N; ++i)
        {
            ra += ea[i] * absR[i][j];
            tj += t[i] * r[i][j];
        }
        separated |= absolute(tj) > ra + eb[j];
    }
    if (separated) return false;
    if constexpr (N == 3) // edge-pair axes a_i x b_j
    {
        for (std::size_t i = 0; i < 3; ++i)
        {
            const std::size_t i1 = (i + 1) % 3, i2 = (i + 2) % 3;
            for (std::size_t j = 0; j < 3; ++j)
            {
                const std::size_t j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                const T ra = ea[i1] * absR[i2][j] + ea[i2] * absR[i1][j];
                const T rb = eb[j1] * absR[i][j2] + eb[j2] * absR[i][j1];
                if (absolute(t[i2] * r[i1][j] - t[i1] * r[i2][j]) > ra + rb) return false;
            }
        }
    }
    return true;
}

} // namespace detail

/**
 * Reports whether two oriented boxes overlap or touch, by the separating-axis test.
 *
 * Tests the 4 face axes in 2D, and the 6 face axes plus 9 edge-pair axes in 3D (Gottschalk, Lin and
 * Manocha 1996; arrangement after Ericson, Real-Time Collision Detection, 4.4.1). A small tolerance
 * on the rotation terms keeps near-parallel edges from producing a false separation.
 *
 * @param a One box.
 * @param b The other box.
 * @return True when the boxes share at least one point.
 */
template<typename T, std::size_t N>
    requires(N == 2 || N == 3)
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr bool overlaps(const OrientedBox<T, N>& a, const OrientedBox<T, N>& b) noexcept
{
    std::array<std::array<ScalarOf<T>, N>, N> r{};
    for (std::size_t i = 0; i < N; ++i)
    {
        for (std::size_t j = 0; j < N; ++j) r[i][j] = dot(a.axes.columns[i], b.axes.columns[j]);
    }
    const Vec<T, N> d = b.centre - a.centre;
    std::array<T, N> t{};
    for (std::size_t i = 0; i < N; ++i) t[i] = dot(a.axes.columns[i], d);
    return detail::separatingAxesOverlap(r, a.halfExtents, b.halfExtents, t);
}

/**
 * Tests one oriented box against many axis-aligned boxes, such as the blocks of a grid it may cover.
 *
 * Construction computes everything that depends only on the oriented box: its radius along each
 * coordinate axis, and the absolute rotation terms. Each test then checks the coordinate axes first,
 * which is one comparison per axis and rejects most distant blocks. Only then does it check the box's
 * own axes and, in 3D, the edge-pair axes.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions: 2 or 3.
 */
template<typename T, std::size_t N>
    requires(N == 2 || N == 3)
class AabbOverlapTester
{
public:
    /**
     * Prepares tests against one oriented box.
     *
     * @param box The oriented box.
     */
    explicit constexpr AabbOverlapTester(const OrientedBox<T, N>& box) noexcept : box_(box)
    {
        // PERF: box terms once here, faster than per-block recomputation.
        using S = ScalarOf<T>;
        const S tolerance = std::numeric_limits<S>::epsilon() * S{16};
        for (std::size_t i = 0; i < N; ++i)
        {
            for (std::size_t j = 0; j < N; ++j)
            {
                r_[i][j] = box.axes.columns[i][j];
                const S a = r_[i][j] < S{0} ? -r_[i][j] : r_[i][j];
                absR_[i][j] = a + tolerance;
            }
        }
        for (std::size_t j = 0; j < N; ++j)
        {
            T radius{};
            for (std::size_t i = 0; i < N; ++i) radius += box.halfExtents[i] * absR_[i][j];
            worldRadius_[j] = radius;
        }
    }

    /**
     * Reports whether the oriented box overlaps or touches an axis-aligned box.
     *
     * @param b The axis-aligned box.
     * @return True when they share at least one point.
     */
    [[nodiscard]] SUB0MATH_FORCE_INLINE constexpr bool operator()(const Aabb<T, N>& b) const noexcept
    {
        const Vec<T, N> half = (b.hi - b.lo) / 2;
        return overlapsCentred(b.lo + half, half);
    }

    /**
     * Reports whether the oriented box overlaps or touches an axis-aligned box given by its centre and
     * half size. A regular grid knows both without corners: this is the fast path for rasterising.
     *
     * @param centre Centre of the axis-aligned box.
     * @param half Its half size along each coordinate axis; not negative.
     * @return True when they share at least one point.
     */
    // PERF: centre+half input ~30% faster than Aabb corners on grids.
    [[nodiscard]] SUB0MATH_FORCE_INLINE constexpr bool overlapsCentred(const Point<T, N>& centre, const Vec<T, N>& half) const noexcept
    {
        const auto absolute = [](auto x) {
            using std::abs; // quantities find sub0math::abs by argument lookup
            return abs(x);
        };
        const Vec<T, N> d = centre - box_.centre;
        // PERF: early-exit coordinate axes beat branch-free vectorised SAT here.
        for (std::size_t j = 0; j < N; ++j) // coordinate axes: the block's faces
        {
            if (absolute(d[j]) > worldRadius_[j] + half[j]) return false;
        }
        std::array<T, N> t{};
        for (std::size_t i = 0; i < N; ++i) // the oriented box's faces
        {
            t[i] = dot(box_.axes.columns[i], d);
            T rb{};
            for (std::size_t j = 0; j < N; ++j) rb += half[j] * absR_[i][j];
            if (absolute(t[i]) > box_.halfExtents[i] + rb) return false;
        }
        if constexpr (N == 3) // edge-pair axes a_i x e_j
        {
            const auto& ea = box_.halfExtents;
            for (std::size_t i = 0; i < 3; ++i)
            {
                const std::size_t i1 = (i + 1) % 3, i2 = (i + 2) % 3;
                for (std::size_t j = 0; j < 3; ++j)
                {
                    const std::size_t j1 = (j + 1) % 3, j2 = (j + 2) % 3;
                    const T ra = ea[i1] * absR_[i2][j] + ea[i2] * absR_[i1][j];
                    const T rb = half[j1] * absR_[i][j2] + half[j2] * absR_[i][j1];
                    if (absolute(t[i2] * r_[i1][j] - t[i1] * r_[i2][j]) > ra + rb) return false;
                }
            }
        }
        return true;
    }

private:
    OrientedBox<T, N> box_;
    std::array<std::array<ScalarOf<T>, N>, N> r_{};    // r_[i][j] = box axis i . coordinate axis j
    std::array<std::array<ScalarOf<T>, N>, N> absR_{}; // |r_| + tolerance
    std::array<T, N> worldRadius_{};                   // the box's half extent along coordinate axis j
};

/**
 * Reports whether an oriented box and an axis-aligned box overlap or touch. For many axis-aligned boxes
 * against the same oriented box, construct one AabbOverlapTester instead.
 *
 * @param a The oriented box.
 * @param b The axis-aligned box.
 * @return True when they share at least one point.
 */
template<typename T, std::size_t N>
    requires(N == 2 || N == 3)
[[nodiscard]] constexpr bool overlaps(const OrientedBox<T, N>& a, const Aabb<T, N>& b) noexcept
{
    return AabbOverlapTester<T, N>(a)(b);
}

} // namespace sub0math
