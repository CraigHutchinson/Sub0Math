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
        for (std::size_t i = 0; i < N; ++i)
        {
            box.lo[i] = std::min(a[i], b[i]);
            box.hi[i] = std::max(a[i], b[i]);
        }
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
        for (std::size_t i = 0; i < N; ++i)
        {
            if (p[i] < lo[i] || p[i] > hi[i])
            {
                return false;
            }
        }
        return true;
    }

    /**
     * Reports whether two boxes overlap or touch.
     *
     * @param other The other box.
     * @return True when they share at least one point.
     */
    [[nodiscard]] constexpr bool intersects(const Aabb& other) const noexcept
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            if (other.hi[i] < lo[i] || other.lo[i] > hi[i])
            {
                return false;
            }
        }
        return true;
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
        for (std::size_t i = 0; i < N; ++i)
        {
            box.lo[i] = std::min(box.lo[i], p[i]);
            box.hi[i] = std::max(box.hi[i], p[i]);
        }
        return box;
    }

    friend constexpr bool operator==(const Aabb&, const Aabb&) = default;
};

} // namespace sub0math
