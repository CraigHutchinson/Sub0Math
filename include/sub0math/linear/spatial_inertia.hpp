#pragma once

#include <cstddef>
#include <type_traits>

#include "sub0math/config.hpp"
#include "sub0math/linear/mat.hpp"
#include "sub0math/linear/spatial_force.hpp"
#include "sub0math/linear/spatial_motion.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * The spatial inertia of a rigid body (or a rigid assembly) in one frame, in compact form.
 *
 * Holds the mass, the first mass moment `h = mass * c` (c the centre of mass) and the rotational inertia
 * about the frame's origin, instead of the 6x6 matrix `[[I_o, h x], [-h x, mass 1]]`: 13 numbers rather
 * than 36, and sums of inertias in one frame are component-wise. Components are plain SI numbers (kg,
 * kg m, kg m^2). Build one from a body's centre of mass with `fromCentreOfMass`.
 *
 * @tparam T Floating-point type; double for dynamics.
 */
template<Real T>
struct SpatialInertia
{
    T mass{};                 ///< Mass.
    Vec<T, 3> firstMoment{};  ///< Mass times the centre of mass position, h.
    Mat3<T> rotational{};     ///< Rotational inertia about the frame origin (symmetric).

    /**
     * Makes the inertia of a body from its mass properties.
     *
     * @param mass The mass.
     * @param centreOfMass Position of the centre of mass in this frame.
     * @param inertiaAboutCentre Rotational inertia about the centre of mass, in this frame's axes;
     *        symmetric.
     * @return The spatial inertia about this frame's origin.
     */
    [[nodiscard]] static constexpr SpatialInertia fromCentreOfMass(T mass, const Vec<T, 3>& centreOfMass,
                                                                   const Mat3<T>& inertiaAboutCentre) noexcept
    {
        // Parallel axis theorem: I_o = I_c + mass (|c|^2 1 - c c^T).
        const T cc = dot(centreOfMass, centreOfMass);
        SpatialInertia out{mass, centreOfMass * mass, inertiaAboutCentre};
        for (std::size_t c = 0; c < 3; ++c)
        {
            for (std::size_t r = 0; r < 3; ++r)
            {
                const T diagonal = r == c ? cc : T{};
                out.rotational(r, c) += mass * (diagonal - centreOfMass[r] * centreOfMass[c]);
            }
        }
        return out;
    }

    /** @return The centre of mass position, h / mass; requires a positive mass. */
    [[nodiscard]] constexpr Vec<T, 3> centreOfMass() const noexcept
    {
        return firstMoment / mass;
    }

    constexpr SpatialInertia& operator+=(const SpatialInertia& other) noexcept
    {
        mass += other.mass;
        firstMoment += other.firstMoment;
        for (std::size_t c = 0; c < 3; ++c)
        {
            rotational.columns[c] += other.rotational.columns[c];
        }
        return *this;
    }

    friend constexpr SpatialInertia operator+(SpatialInertia a, const SpatialInertia& b) noexcept
    {
        a += b;
        return a;
    }

    friend constexpr bool operator==(const SpatialInertia&, const SpatialInertia&) = default;
};

/**
 * Applies an inertia to a motion: the momentum of a body moving with a velocity, or the force needed for
 * an acceleration.
 *
 * @param inertia The spatial inertia.
 * @param m The motion vector, in the same frame.
 * @return inertia * m, a force vector.
 */
template<Real T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr SpatialForce<T> operator*(const SpatialInertia<T>& inertia,
                                                                        const SpatialMotion<T>& m) noexcept
{
    return {inertia.rotational * m.angular + cross(inertia.firstMoment, m.linear),
            m.linear * inertia.mass - cross(inertia.firstMoment, m.angular)};
}

using SpatialInertiad = SpatialInertia<double>; ///< Double spatial inertia.

static_assert(sizeof(SpatialInertiad) == 13 * sizeof(double));
static_assert(std::is_trivially_copyable_v<SpatialInertiad>);

} // namespace sub0math
