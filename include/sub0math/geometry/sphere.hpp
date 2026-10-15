#pragma once

#include <cstddef>

#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"

namespace sub0math
{

/**
 * A ball: every point within `radius` of `centre`. A circle in 2D, a sphere in 3D.
 *
 * Shapes are values in their own frame; placement is the caller's (a pose or a world-space copy).
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct Sphere
{
    Point<T, N> centre; ///< Centre.
    T radius;           ///< Radius; not negative.

    friend constexpr bool operator==(const Sphere&, const Sphere&) = default;
};

template<typename T>
using Circle = Sphere<T, 2>; ///< A circle.

/**
 * Reports whether a point is inside or on the boundary.
 *
 * @param sphere The sphere.
 * @param p The point.
 * @return True when |p - centre| <= radius.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr bool contains(const Sphere<T, N>& sphere, const Point<T, N>& p) noexcept
{
    return lengthSquared(p - sphere.centre) <= sphere.radius * sphere.radius;
}

/**
 * Computes the smallest axis-aligned box around a sphere.
 *
 * @param sphere The sphere.
 * @return Its bounding box.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr Aabb<T, N> boundingBox(const Sphere<T, N>& sphere) noexcept
{
    Vec<T, N> extent{};
    detail::forEachComponent<N>([&](auto i) { extent.template get<i>() = sphere.radius; });
    return {sphere.centre - extent, sphere.centre + extent};
}

} // namespace sub0math
