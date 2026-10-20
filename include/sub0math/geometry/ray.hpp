#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include "sub0math/config.hpp"
#include "sub0math/geometry/detail/overlap.hpp"
#include "sub0math/geometry/plane.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/geometry/triangle.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"
#include "sub0math/units/functions.hpp"

namespace sub0math
{

/**
 * A half-line from `origin` along a unit `direction`.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct Ray
{
    Point<T, N> origin;                ///< Start.
    Vec<ScalarOf<T>, N> direction;     ///< Unit direction of travel.

    /**
     * Finds the point at a distance along the ray.
     *
     * @param distance Distance from the origin.
     * @return origin + direction * distance.
     */
    [[nodiscard]] constexpr Point<T, N> at(T distance) const noexcept
    {
        return origin + direction * distance;
    }

    friend constexpr bool operator==(const Ray&, const Ray&) = default;
};

/**
 * Where a ray meets a triangle.
 *
 * @tparam T Coordinate type.
 */
template<typename T>
struct RayHit
{
    T distance;     ///< Distance along the ray.
    ScalarOf<T> u;  ///< Barycentric weight of vertex b.
    ScalarOf<T> v;  ///< Barycentric weight of vertex c; a's weight is 1 - u - v.
};

/**
 * Intersects a ray with a triangle from either side (Moller and Trumbore 1997).
 *
 * A ray parallel to the triangle's plane, within floating-point tolerance, does not hit it. Hits on an
 * edge or vertex count.
 *
 * @param ray The ray.
 * @param tri A non-degenerate triangle.
 * @param maxDistance Ignore hits farther than this.
 * @return The hit, or nothing.
 */
template<typename T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr std::optional<RayHit<T>> intersect(const Ray<T, 3>& ray, const Triangle<T, 3>& tri,
                                                                                T maxDistance) noexcept
{
    using S = ScalarOf<T>;
    const Vec<T, 3> e1 = tri.b - tri.a;
    const Vec<T, 3> e2 = tri.c - tri.a;
    const Vec<T, 3> pv = cross(ray.direction, e2);
    const auto det = dot(e1, pv);
    const auto scale = lengthSquared(e1) + lengthSquared(e2);
    if (detail::absolute(det) <= scale * (std::numeric_limits<S>::epsilon() * S{8})) return std::nullopt;

    const Vec<T, 3> s = ray.origin - tri.a;
    const S u = static_cast<S>(dot(s, pv) / det);
    if (u < S{0} || u > S{1}) return std::nullopt;
    const auto q = cross(s, e1);
    const S v = static_cast<S>(dot(ray.direction, q) / det);
    if (v < S{0} || u + v > S{1}) return std::nullopt;
    const T t = dot(e2, q) / det;
    if (t < T{} || t > maxDistance) return std::nullopt;
    return RayHit<T>{t, u, v};
}

/**
 * Intersects a ray with a plane from either side.
 *
 * @param ray The ray.
 * @param plane The plane.
 * @param maxDistance Ignore hits farther than this.
 * @return The distance along the ray, or nothing when parallel, behind or too far.
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr std::optional<T> intersect(const Ray<T, N>& ray, const Plane<T, N>& plane, T maxDistance) noexcept
{
    using S = ScalarOf<T>;
    const S denom = dot(plane.normal, ray.direction);
    if (std::abs(denom) <= std::numeric_limits<S>::epsilon()) return std::nullopt;
    const T t = -signedDistance(plane, ray.origin) / denom;
    if (t < T{} || t > maxDistance) return std::nullopt;
    return t;
}

} // namespace sub0math
