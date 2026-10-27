#pragma once

#include <cmath>

#include "sub0math/number/det_math.hpp"
#include "sub0math/number/real.hpp"
#include "sub0math/units/dimension.hpp"
#include "sub0math/units/quantity.hpp"

namespace sub0math
{

/**
 * Computes the absolute value.
 *
 * @param q A quantity.
 * @return |q|, same dimension.
 */
template<typename D, Real Rep>
[[nodiscard]] constexpr Quantity<D, Rep> abs(const Quantity<D, Rep>& q) noexcept
{
    // PERF: std::abs faster than compare-and-negate, which branched.
    return Quantity<D, Rep>::fromSi(std::abs(q.si()));
}

/**
 * Computes a square root; only dimensions with even exponents have one.
 *
 * @param q A quantity, e.g. an area or a squared velocity.
 * @return The root, e.g. a length or a speed.
 */
template<typename D, Real Rep>
    requires HasSquareRoot<D>
[[nodiscard]] Quantity<DimensionSqrt<D>, Rep> sqrt(const Quantity<D, Rep>& q) noexcept
{
    return Quantity<DimensionSqrt<D>, Rep>::fromSi(std::sqrt(q.si()));
}

/**
 * Computes the sine of an angle; only angles are accepted, so degrees cannot be passed by mistake.
 *
 * Uses the deterministic `det::sin`, not the C library: the same bits on every platform.
 *
 * @param angle An angle.
 * @return Its sine.
 */
template<Real Rep>
[[nodiscard]] constexpr Rep sin(const Quantity<dim::Angle, Rep>& angle) noexcept
{
    return det::sin(angle.si());
}

/**
 * Computes the cosine of an angle.
 *
 * @param angle An angle.
 * @return Its cosine.
 */
template<Real Rep>
[[nodiscard]] constexpr Rep cos(const Quantity<dim::Angle, Rep>& angle) noexcept
{
    return det::cos(angle.si());
}

/**
 * Computes the sine and cosine of an angle with one argument reduction; equal to `sin` and `cos`.
 *
 * @param angle An angle.
 * @return Its sine and cosine.
 */
template<Real Rep>
[[nodiscard]] constexpr det::SinCos<Rep> sincos(const Quantity<dim::Angle, Rep>& angle) noexcept
{
    return det::sincos(angle.si());
}

/**
 * Computes the tangent of an angle with det::tan (fdlibm, bit-identical on every platform).
 *
 * @param angle An angle.
 * @return Its tangent.
 */
template<Real Rep>
[[nodiscard]] constexpr Rep tan(const Quantity<dim::Angle, Rep>& angle) noexcept
{
    return det::tan(angle.si());
}

/**
 * Computes the angle of a direction from two components of the same dimension, with det::atan2 (bit-identical on
 * every platform).
 *
 * @param y Second component.
 * @param x First component.
 * @return The angle in (-pi, pi].
 */
template<typename D, Real Rep>
[[nodiscard]] constexpr Quantity<dim::Angle, Rep> atan2(const Quantity<D, Rep>& y, const Quantity<D, Rep>& x) noexcept
{
    return Quantity<dim::Angle, Rep>::fromSi(det::atan2(y.si(), x.si()));
}

/**
 * Makes the angle whose sine is a ratio. Not named `asin`: the C library's `asin(double)` would win
 * overload resolution for plain arguments and silently return a number.
 *
 * @param ratio A number in [-1, 1].
 * @return The angle.
 */
template<Real Rep>
[[nodiscard]] Quantity<dim::Angle, Rep> angleFromSine(Rep ratio) noexcept
{
    return Quantity<dim::Angle, Rep>::fromSi(std::asin(ratio));
}

/**
 * Makes the angle whose cosine is a ratio.
 *
 * @param ratio A number in [-1, 1].
 * @return The angle.
 */
template<Real Rep>
[[nodiscard]] Quantity<dim::Angle, Rep> angleFromCosine(Rep ratio) noexcept
{
    return Quantity<dim::Angle, Rep>::fromSi(std::acos(ratio));
}

/**
 * Treats the radian as 1, removing the angle from a dimension.
 *
 * Use it where physics does exactly that, and nowhere else: arc length (`dropAngle(angle * radius)`),
 * tangential speed, rotational kinetic energy.
 *
 * @param q A quantity with an angle exponent.
 * @return The same value without it.
 */
template<typename D, Real Rep>
[[nodiscard]] constexpr Quantity<DimensionWithoutAngle<D>, Rep> dropAngle(const Quantity<D, Rep>& q) noexcept
{
    return Quantity<DimensionWithoutAngle<D>, Rep>::fromSi(q.si());
}

/**
 * Reports whether a quantity is finite.
 *
 * @param q A quantity.
 * @return False for infinities and NaN.
 */
template<typename D, Real Rep>
[[nodiscard]] bool isFinite(const Quantity<D, Rep>& q) noexcept
{
    return std::isfinite(q.si());
}

} // namespace sub0math
