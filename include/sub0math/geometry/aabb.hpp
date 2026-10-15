#pragma once

#include <algorithm>
#include <cstddef>

#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"

namespace sub0math
{

/**
 * An axis-aligned box from `lo` to `hi`, inclusive. Valid when lo <= hi on every axis; `fromCorners`
 * guarantees it.
 *
 * @tparam T Coordinate type: a floating-point number or a Quantity.
 * @tparam N Number of axes.
 */
template<typename T, std::size_t N>
struct Aabb
{
    Point<T, N> lo; ///< Smallest corner.
    Point<T, N> hi; ///< Largest corner.

    /**
     * Makes the box spanned by two corners in any order.
     *
     * @param a One corner.
     * @param b The opposite corner.
     * @return The box.
     */
    [[nodiscard]] static constexpr Aabb fromCorners(const Point<T, N>& a, const Point<T, N>& b) noexcept
    {
        Aabb box{a, a};
        detail::forEachComponent<N>([&](auto i) {
            box.lo.template get<i>() = std::min(a.template get<i>(), b.template get<i>());
            box.hi.template get<i>() = std::max(a.template get<i>(), b.template get<i>());
        });
        return box;
    }

    /** @return The extent along each axis. */
    [[nodiscard]] constexpr Vec<T, N> size() const noexcept
    {
        return hi - lo;
    }

    /**
     * Reports whether a point is inside or on the boundary.
     *
     * @param p The point.
     * @return True when lo <= p <= hi on every axis.
     */
    [[nodiscard]] constexpr bool contains(const Point<T, N>& p) const noexcept
    {
        bool inside = true;
        detail::forEachComponent<N>([&](auto i) {
            inside = inside && !(p.template get<i>() < lo.template get<i>() || p.template get<i>() > hi.template get<i>());
        });
        return inside;
    }

    /**
     * Reports whether two boxes overlap or touch.
     *
     * @param other The other box.
     * @return True when they share at least one point.
     */
    [[nodiscard]] constexpr bool intersects(const Aabb& other) const noexcept
    {
        bool overlaps = true;
        detail::forEachComponent<N>([&](auto i) {
            overlaps = overlaps
                && !(other.hi.template get<i>() < lo.template get<i>() || other.lo.template get<i>() > hi.template get<i>());
        });
        return overlaps;
    }

    /**
     * Grows the box to include a point.
     *
     * @param p The point.
     * @return The smallest box containing this one and p.
     */
    [[nodiscard]] constexpr Aabb including(const Point<T, N>& p) const noexcept
    {
        Aabb box = *this;
        detail::forEachComponent<N>([&](auto i) {
            box.lo.template get<i>() = std::min(box.lo.template get<i>(), p.template get<i>());
            box.hi.template get<i>() = std::max(box.hi.template get<i>(), p.template get<i>());
        });
        return box;
    }

    friend constexpr bool operator==(const Aabb&, const Aabb&) = default;
};

} // namespace sub0math
