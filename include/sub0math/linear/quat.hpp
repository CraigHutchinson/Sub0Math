#pragma once

#include <cmath>

#include "sub0math/linear/mat.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"
#include "sub0math/units/dimension.hpp"
#include "sub0math/units/quantity.hpp"

namespace sub0math
{

/**
 * A rotation as a unit quaternion w + xi + yj + zk. Default-constructed it is the identity.
 *
 * Rotations are dimensionless, so a quaternion rotates vectors of numbers or quantities alike.
 * Composition `a * b` applies b first, then a.
 *
 * @tparam T Floating-point type.
 */
template<Real T>
struct Quat
{
    T w{1}; ///< Scalar part.
    T x{};  ///< i component.
    T y{};  ///< j component.
    T z{};  ///< k component.

    /**
     * Makes the rotation by an angle about an axis.
     *
     * @param unitAxis Axis of rotation; must have unit length.
     * @param angle Rotation angle, right-handed about the axis.
     * @return The rotation.
     */
    [[nodiscard]] static Quat fromAxisAngle(const Vec<T, 3>& unitAxis, const Quantity<dim::Angle, T>& angle) noexcept
    {
        const T half = angle.si() / T{2};
        const T s = std::sin(half);
        return Quat{std::cos(half), unitAxis.x * s, unitAxis.y * s, unitAxis.z * s};
    }

    /** @return The vector part (x, y, z). */
    [[nodiscard]] constexpr Vec<T, 3> vector() const noexcept
    {
        return {x, y, z};
    }

    friend constexpr bool operator==(const Quat&, const Quat&) = default;
};

/**
 * Composes rotations: `a * b` rotates by b, then by a.
 *
 * @param a Second rotation.
 * @param b First rotation.
 * @return The combined rotation.
 */
template<Real T>
[[nodiscard]] constexpr Quat<T> operator*(const Quat<T>& a, const Quat<T>& b) noexcept
{
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

/**
 * Makes the inverse of a unit quaternion.
 *
 * @param q A rotation.
 * @return The opposite rotation.
 */
template<Real T>
[[nodiscard]] constexpr Quat<T> conjugate(const Quat<T>& q) noexcept
{
    return {q.w, -q.x, -q.y, -q.z};
}

/**
 * Rescales to unit length, removing drift after many compositions.
 *
 * @param q A non-zero quaternion.
 * @return The unit quaternion in the same direction.
 */
template<Real T>
[[nodiscard]] Quat<T> normalized(const Quat<T>& q) noexcept
{
    const T n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w / n, q.x / n, q.y / n, q.z / n};
}

/**
 * Rotates a vector.
 *
 * @param q A unit quaternion.
 * @param v A vector of numbers or quantities.
 * @return The rotated vector, same component type.
 */
template<Real T, typename U>
[[nodiscard]] constexpr Vec<U, 3> rotate(const Quat<T>& q, const Vec<U, 3>& v) noexcept
{
    // v + 2w(u x v) + 2u x (u x v), with u the vector part.
    const Vec<T, 3> u = q.vector();
    const Vec<U, 3> t = cross(u, v) * T{2};
    return v + t * q.w + cross(u, t);
}

/**
 * Converts to a rotation matrix.
 *
 * @param q A unit quaternion.
 * @return The equivalent 3x3 rotation matrix.
 */
template<Real T>
[[nodiscard]] constexpr Mat3<T> toMat3(const Quat<T>& q) noexcept
{
    const T xx = q.x * q.x;
    const T yy = q.y * q.y;
    const T zz = q.z * q.z;
    const T xy = q.x * q.y;
    const T xz = q.x * q.z;
    const T yz = q.y * q.z;
    const T wx = q.w * q.x;
    const T wy = q.w * q.y;
    const T wz = q.w * q.z;
    Mat3<T> m{};
    m(0, 0) = T{1} - T{2} * (yy + zz);
    m(0, 1) = T{2} * (xy - wz);
    m(0, 2) = T{2} * (xz + wy);
    m(1, 0) = T{2} * (xy + wz);
    m(1, 1) = T{1} - T{2} * (xx + zz);
    m(1, 2) = T{2} * (yz - wx);
    m(2, 0) = T{2} * (xz - wy);
    m(2, 1) = T{2} * (yz + wx);
    m(2, 2) = T{1} - T{2} * (xx + yy);
    return m;
}

using Quatf = Quat<float>;  ///< Float rotation.
using Quatd = Quat<double>; ///< Double rotation.

} // namespace sub0math
