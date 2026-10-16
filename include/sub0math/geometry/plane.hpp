#pragma once

#include <cstddef>

#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A plane (a line in 2D): the points x with dot(normal, x - origin) == offset.
 *
 * The normal is a unit vector of plain numbers; `offset` carries the unit of the coordinates. The
 * normal points out of the half-space the plane bounds, so signed distances are positive on its side.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct Plane
{
    Vec<ScalarOf<T>, N> normal; ///< Unit normal.
    T offset;                   ///< Signed distance of the plane from the origin along `normal`.

    /**
     * Makes the plane through a point with a given normal.
     *
     * @param p A point on the plane.
     * @param unitNormal Its unit normal.
     * @return The plane.
     */
    [[nodiscard]] static constexpr Plane through(const Point<T, N>& p, const Vec<ScalarOf<T>, N>& unitNormal) noexcept
    {
        return {unitNormal, dot(unitNormal, p - Point<T, N>::origin())};
    }

    friend constexpr bool operator==(const Plane&, const Plane&) = default;
};

/**
 * Measures the signed distance from a plane.
 *
 * @param plane The plane.
 * @param p The point.
 * @return Positive on the side the normal points to, negative on the other.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr T signedDistance(const Plane<T, N>& plane, const Point<T, N>& p) noexcept
{
    return dot(plane.normal, p - Point<T, N>::origin()) - plane.offset;
}

/**
 * Projects a point onto a plane.
 *
 * @param plane The plane.
 * @param p The point.
 * @return The nearest point on the plane.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr Point<T, N> closestPoint(const Plane<T, N>& plane, const Point<T, N>& p) noexcept
{
    return p - plane.normal * signedDistance(plane, p);
}

} // namespace sub0math
