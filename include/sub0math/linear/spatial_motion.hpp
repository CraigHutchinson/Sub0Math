#pragma once

#include <type_traits>

#include "sub0math/config.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A spatial motion vector: angular and linear parts of a rigid body's velocity or acceleration, or a
 * joint's motion axis, in Plücker coordinates of one frame.
 *
 * `linear` is the velocity of the body-fixed point that coincides with the frame's origin, not of the
 * centre of mass. Components are plain SI numbers (rad/s and m/s for a velocity), because the same type
 * also carries accelerations and unit joint axes: units are named at the interfaces that build these
 * vectors. Motion and force vectors are separate types, so they cannot be added to each other, and the
 * only product between them is the scalar `dot` (a power for velocity and force).
 *
 * @tparam T Floating-point type; double for dynamics.
 */
template<Real T>
struct SpatialMotion
{
    Vec<T, 3> angular{}; ///< Angular part, omega.
    Vec<T, 3> linear{};  ///< Linear part: velocity of the point at the frame origin.

    constexpr SpatialMotion& operator+=(const SpatialMotion& other) noexcept
    {
        angular += other.angular;
        linear += other.linear;
        return *this;
    }

    constexpr SpatialMotion& operator-=(const SpatialMotion& other) noexcept
    {
        angular -= other.angular;
        linear -= other.linear;
        return *this;
    }

    friend constexpr SpatialMotion operator+(SpatialMotion a, const SpatialMotion& b) noexcept
    {
        a += b;
        return a;
    }

    friend constexpr SpatialMotion operator-(SpatialMotion a, const SpatialMotion& b) noexcept
    {
        a -= b;
        return a;
    }

    friend constexpr SpatialMotion operator-(const SpatialMotion& m) noexcept
    {
        return {-m.angular, -m.linear};
    }

    friend constexpr bool operator==(const SpatialMotion&, const SpatialMotion&) = default;
};

/**
 * Scales a motion vector, for example a joint axis by a joint rate.
 *
 * @param m The motion vector.
 * @param factor The factor.
 * @return m * factor.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialMotion<T> operator*(const SpatialMotion<T>& m, T factor) noexcept
{
    return {m.angular * factor, m.linear * factor};
}

/**
 * Scales a motion vector.
 *
 * @param factor The factor.
 * @param m The motion vector.
 * @return factor * m.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialMotion<T> operator*(T factor, const SpatialMotion<T>& m) noexcept
{
    return m * factor;
}

/**
 * Computes the spatial cross product of two motion vectors (Featherstone's `crm(v) m`).
 *
 * The rate of change of `m`, fixed in a body, as seen from a frame in which the body moves with `v`.
 *
 * @param v The velocity of the moving frame.
 * @param m The motion vector.
 * @return v x m.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialMotion<T> cross(const SpatialMotion<T>& v,
                                                                     const SpatialMotion<T>& m) noexcept
{
    return {cross(v.angular, m.angular), cross(v.angular, m.linear) + cross(v.linear, m.angular)};
}

using SpatialMotiond = SpatialMotion<double>; ///< Double spatial motion vector.

static_assert(sizeof(SpatialMotiond) == 6 * sizeof(double));
static_assert(std::is_trivially_copyable_v<SpatialMotiond>);

} // namespace sub0math
