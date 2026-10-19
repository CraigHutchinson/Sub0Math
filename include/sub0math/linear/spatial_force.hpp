#pragma once

#include <type_traits>

#include "sub0math/config.hpp"
#include "sub0math/linear/spatial_motion.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A spatial force vector: the moment about a frame's origin and the resultant force, in Plücker
 * coordinates of that frame. Also carries momenta and the rates of change of momenta.
 *
 * The dual of SpatialMotion: transforms act on it with the inverse transpose, and `dot(motion, force)`
 * is the only product between the two (power, for a velocity and a force). Components are plain SI
 * numbers (N m and N for a force).
 *
 * @tparam T Floating-point type; double for dynamics.
 */
template<Real T>
struct SpatialForce
{
    Vec<T, 3> angular{}; ///< Moment about the frame origin, n.
    Vec<T, 3> linear{};  ///< Resultant force, f.

    constexpr SpatialForce& operator+=(const SpatialForce& other) noexcept
    {
        angular += other.angular;
        linear += other.linear;
        return *this;
    }

    constexpr SpatialForce& operator-=(const SpatialForce& other) noexcept
    {
        angular -= other.angular;
        linear -= other.linear;
        return *this;
    }

    friend constexpr SpatialForce operator+(SpatialForce a, const SpatialForce& b) noexcept
    {
        a += b;
        return a;
    }

    friend constexpr SpatialForce operator-(SpatialForce a, const SpatialForce& b) noexcept
    {
        a -= b;
        return a;
    }

    friend constexpr SpatialForce operator-(const SpatialForce& f) noexcept
    {
        return {-f.angular, -f.linear};
    }

    friend constexpr bool operator==(const SpatialForce&, const SpatialForce&) = default;
};

/**
 * Scales a force vector.
 *
 * @param f The force vector.
 * @param factor The factor.
 * @return f * factor.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialForce<T> operator*(const SpatialForce<T>& f, T factor) noexcept
{
    return {f.angular * factor, f.linear * factor};
}

/**
 * Scales a force vector.
 *
 * @param factor The factor.
 * @param f The force vector.
 * @return factor * f.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialForce<T> operator*(T factor, const SpatialForce<T>& f) noexcept
{
    return f * factor;
}

/**
 * Computes the scalar product of a motion and a force: the power of a force on a velocity.
 *
 * @param m The motion vector.
 * @param f The force vector, in the same frame.
 * @return m . f
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr T dot(const SpatialMotion<T>& m, const SpatialForce<T>& f) noexcept
{
    return dot(m.angular, f.angular) + dot(m.linear, f.linear);
}

/**
 * Computes the scalar product of a force and a motion.
 *
 * @param f The force vector.
 * @param m The motion vector, in the same frame.
 * @return f . m
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr T dot(const SpatialForce<T>& f, const SpatialMotion<T>& m) noexcept
{
    return dot(m, f);
}

/**
 * Computes the spatial cross product of a motion and a force vector (Featherstone's `crf(v) f`).
 *
 * The rate of change of `f`, fixed in a body, as seen from a frame in which the body moves with `v`.
 * It is minus the transpose of the motion cross product: `dot(m, cross(v, f)) == -dot(cross(v, m), f)`.
 *
 * @param v The velocity of the moving frame.
 * @param f The force vector.
 * @return v x* f.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialForce<T> cross(const SpatialMotion<T>& v,
                                                                    const SpatialForce<T>& f) noexcept
{
    return {cross(v.angular, f.angular) + cross(v.linear, f.linear), cross(v.angular, f.linear)};
}

using SpatialForced = SpatialForce<double>; ///< Double spatial force vector.

static_assert(sizeof(SpatialForced) == 6 * sizeof(double));
static_assert(std::is_trivially_copyable_v<SpatialForced>);

} // namespace sub0math
