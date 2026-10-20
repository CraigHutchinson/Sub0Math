#pragma once

#include <cstddef>
#include <type_traits>

#include "sub0math/config.hpp"
#include "sub0math/linear/mat.hpp"
#include "sub0math/linear/quat.hpp"
#include "sub0math/linear/rigid_transform.hpp"
#include "sub0math/linear/spatial_force.hpp"
#include "sub0math/linear/spatial_inertia.hpp"
#include "sub0math/linear/spatial_motion.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

namespace detail
{

// E^T v: the columns of E dotted with v, without forming the transpose.
template<Real T>
SUB0MATH_FORCE_INLINE constexpr Vec<T, 3> transposeTimes(const Mat3<T>& e, const Vec<T, 3>& v) noexcept
{
    return {dot(e.columns[0], v), dot(e.columns[1], v), dot(e.columns[2], v)};
}

} // namespace detail

/**
 * A PlÃ¼cker coordinate transform `X` from frame A to frame B, for spatial motions, forces and inertias.
 *
 * Holds the rotation `E`, which maps A-axis coordinates to B-axis coordinates, and the position `r` of
 * B's origin in A coordinates; this is Featherstone's `B_X_A = rot(E) xlt(r)`. It applies to motion
 * vectors as `X`, to force vectors as the dual `X* = X^-T`, and in reverse (`applyInverse`) as `X^-1`
 * and `X^T` respectively, so motions and forces are always transformed consistently and
 * `dot(X m, X* f) == dot(m, f)`. Composition `a * b` applies b first. Default-constructed it is the
 * identity.
 *
 * @tparam T Floating-point type; double for dynamics.
 */
template<Real T>
struct PluckerTransform
{
    Mat3<T> rotation = Mat3<T>::identity(); ///< E: A-axis coordinates to B-axis coordinates.
    Vec<T, 3> translation{};               ///< r: position of B's origin, in A coordinates (SI).

    /**
     * Makes the transform into a frame placed by a pose.
     *
     * @tparam L Position type of the pose: T or a length quantity of representation T.
     * @param poseOfBInA The rigid transform that maps B coordinates to A coordinates (B's pose in A).
     * @return The PlÃ¼cker transform from A to B.
     */
    template<typename L>
    [[nodiscard]] static constexpr PluckerTransform fromPose(const RigidTransform<T, L>& poseOfBInA) noexcept
    {
        const Vec<L, 3>& t = poseOfBInA.translation;
        return {transpose(toMat3(poseOfBInA.rotation)), toSi(t)};
    }

    /**
     * Transforms a motion vector from A to B.
     *
     * @param m Motion in A coordinates.
     * @return X m, in B coordinates.
     */
    [[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialMotion<T> apply(const SpatialMotion<T>& m) const noexcept
    {
        return {rotation * m.angular, rotation * (m.linear - cross(translation, m.angular))};
    }

    /**
     * Transforms a force vector from A to B.
     *
     * @param f Force in A coordinates.
     * @return X* f, in B coordinates.
     */
    [[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialForce<T> apply(const SpatialForce<T>& f) const noexcept
    {
        return {rotation * (f.angular - cross(translation, f.linear)), rotation * f.linear};
    }

    /**
     * Transforms a motion vector from B back to A.
     *
     * @param m Motion in B coordinates.
     * @return X^-1 m, in A coordinates.
     */
    [[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialMotion<T> applyInverse(const SpatialMotion<T>& m) const noexcept
    {
        const Vec<T, 3> angular = detail::transposeTimes(rotation, m.angular);
        return {angular, detail::transposeTimes(rotation, m.linear) + cross(translation, angular)};
    }

    /**
     * Transforms a force vector from B back to A; this is the transpose `X^T` that accumulates child
     * forces onto a parent.
     *
     * @param f Force in B coordinates.
     * @return X^T f, in A coordinates.
     */
    [[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialForce<T> applyInverse(const SpatialForce<T>& f) const noexcept
    {
        const Vec<T, 3> linear = detail::transposeTimes(rotation, f.linear);
        return {detail::transposeTimes(rotation, f.angular) + cross(translation, linear), linear};
    }

    /**
     * Expresses in A an inertia given in B: `X^T I X`, as when a child's composite inertia is added to
     * its parent's.
     *
     * @param inertia Inertia in B coordinates.
     * @return The same inertia in A coordinates.
     */
    [[nodiscard]] constexpr SpatialInertia<T> applyInverse(const SpatialInertia<T>& inertia) const noexcept
    {
        // Featherstone RBDA table 2.5: h_A = E^T h + m r;
        // I_A = E^T I E - r x (E^T h) x - h_A x r x, expanded with a x b x = b a^T - (a . b) 1.
        const Vec<T, 3>& r = translation;
        const T m = inertia.mass;
        const Vec<T, 3> y = detail::transposeTimes(rotation, inertia.firstMoment);
        const T diagonal = T{2} * dot(r, y) + m * dot(r, r);
        SpatialInertia<T> out{m, y + r * m, {}};
        // PERF: six symmetric entries faster than the full 3x3 (1.15x).
        const Vec<T, 3> ie[3] = {inertia.rotational * rotation.columns[0], inertia.rotational * rotation.columns[1],
                                 inertia.rotational * rotation.columns[2]}; // columns of I E
        detail::forEachComponent<3>([&](auto c) {
            detail::forEachComponent<c + 1>([&](auto row) {
                // (E^T I E)(row, c) plus the shift terms.
                const T value = dot(rotation.columns[row], ie[c]) - y[row] * r[c] - r[row] * (y[c] + m * r[c]) +
                                (row == c ? diagonal : T{});
                out.rotational(row, c) = value;
                out.rotational(c, row) = value;
            });
        });
        return out;
    }

    friend constexpr bool operator==(const PluckerTransform&, const PluckerTransform&) = default;
};

/**
 * Composes transforms: with `a` from B to C and `b` from A to B, `a * b` is the transform from A to C.
 *
 * @param a Second transform.
 * @param b First transform.
 * @return The combined transform.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr PluckerTransform<T> operator*(const PluckerTransform<T>& a,
                                                                            const PluckerTransform<T>& b) noexcept
{
    return {a.rotation * b.rotation, b.translation + detail::transposeTimes(b.rotation, a.translation)};
}

/**
 * Inverts a transform.
 *
 * @param x The transform from A to B.
 * @return The transform from B to A.
 */
template<Real T>
[[nodiscard]] constexpr PluckerTransform<T> inverse(const PluckerTransform<T>& x) noexcept
{
    return {transpose(x.rotation), -(x.rotation * x.translation)};
}

/**
 * Recovers the pose that `PluckerTransform::fromPose` was made from.
 *
 * @tparam L Position type of the pose: a length quantity of representation T.
 * @param x The Plücker transform from A to B.
 * @return B's pose in A, with quantity positions.
 */
template<typename L, Real T>
    requires(!std::is_same_v<L, T>)
[[nodiscard]] RigidTransform<T, L> toRigidTransform(const PluckerTransform<T>& x) noexcept
{
    return {Quat<T>::fromRotationMatrix(transpose(x.rotation)), fromSi<L>(x.translation)};
}

/**
 * Recovers the pose that `PluckerTransform::fromPose` was made from.
 *
 * @param x The Plücker transform from A to B.
 * @return B's pose in A, with plain-number positions.
 */
template<Real T>
[[nodiscard]] RigidTransform<T, T> toRigidTransform(const PluckerTransform<T>& x) noexcept
{
    return {Quat<T>::fromRotationMatrix(transpose(x.rotation)), x.translation};
}

using PluckerTransformd = PluckerTransform<double>; ///< Double PlÃ¼cker transform.

static_assert(sizeof(PluckerTransformd) == 12 * sizeof(double));
static_assert(std::is_trivially_copyable_v<PluckerTransformd>);

} // namespace sub0math
