#pragma once

#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/quat.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A rotation followed by a translation: a rigid-body pose or a change of frame.
 *
 * Applied to a point it rotates about the origin then translates; applied to a vector it only rotates.
 * Composition `a * b` applies b first. Default-constructed it is the identity.
 *
 * @tparam T Floating-point type of the rotation.
 * @tparam L Coordinate type of positions: T, or a length quantity of representation T.
 */
template<Real T, typename L = T>
struct RigidTransform
{
    Quat<T> rotation{};      ///< Orientation.
    Vec<L, 3> translation{}; ///< Position of the transformed origin.

    /**
     * Transforms a position.
     *
     * @param p The point.
     * @return rotation(p) + translation.
     */
    [[nodiscard]] constexpr Point<L, 3> apply(const Point<L, 3>& p) const noexcept
    {
        return Point<L, 3>::origin() + (rotate(rotation, p - Point<L, 3>::origin()) + translation);
    }

    /**
     * Transforms a direction or displacement (rotation only).
     *
     * @tparam U Component type of the vector.
     * @param v The vector.
     * @return rotation(v).
     */
    template<typename U>
    [[nodiscard]] constexpr Vec<U, 3> applyToVector(const Vec<U, 3>& v) const noexcept
    {
        return rotate(rotation, v);
    }

    friend constexpr bool operator==(const RigidTransform&, const RigidTransform&) = default;
};

/**
 * Composes transforms: `a * b` applies b, then a.
 *
 * @param a Second transform.
 * @param b First transform.
 * @return The combined transform.
 */
template<Real T, typename L>
[[nodiscard]] constexpr RigidTransform<T, L> operator*(const RigidTransform<T, L>& a,
                                                       const RigidTransform<T, L>& b) noexcept
{
    return {a.rotation * b.rotation, rotate(a.rotation, b.translation) + a.translation};
}

/**
 * Inverts a transform.
 *
 * @param t A transform with a unit rotation.
 * @return The transform that undoes t.
 */
template<Real T, typename L>
[[nodiscard]] constexpr RigidTransform<T, L> inverse(const RigidTransform<T, L>& t) noexcept
{
    const Quat<T> back = conjugate(t.rotation);
    return {back, -rotate(back, t.translation)};
}

using RigidTransformf = RigidTransform<float>;  ///< Float, plain-number positions.
using RigidTransformd = RigidTransform<double>; ///< Double, plain-number positions.

} // namespace sub0math
