#pragma once

namespace sub0math
{

/**
 * A physical dimension as integer exponents of the base dimensions.
 *
 * Angle is a base dimension on purpose, so degrees/radians and angle/length mix-ups do not compile.
 * Where physics treats the radian as 1 (arc length, rotational energy), drop it explicitly with
 * `dropAngle`.
 *
 * @tparam L Length exponent.
 * @tparam M Mass exponent.
 * @tparam T Time exponent.
 * @tparam A Plane-angle exponent.
 * @tparam K Thermodynamic temperature exponent.
 */
template<int L, int M, int T, int A, int K>
struct Dimension
{
    static constexpr int length = L;      ///< Length exponent.
    static constexpr int mass = M;        ///< Mass exponent.
    static constexpr int time = T;        ///< Time exponent.
    static constexpr int angle = A;       ///< Angle exponent.
    static constexpr int temperature = K; ///< Temperature exponent.
};

/**
 * The dimension of a product.
 *
 * @tparam A First factor's dimension.
 * @tparam B Second factor's dimension.
 */
template<typename A, typename B>
using DimensionProduct = Dimension<A::length + B::length, A::mass + B::mass, A::time + B::time,
                                   A::angle + B::angle, A::temperature + B::temperature>;

/**
 * The dimension of a quotient.
 *
 * @tparam A Dividend's dimension.
 * @tparam B Divisor's dimension.
 */
template<typename A, typename B>
using DimensionQuotient = Dimension<A::length - B::length, A::mass - B::mass, A::time - B::time,
                                    A::angle - B::angle, A::temperature - B::temperature>;

/**
 * True when every exponent is even, so a square root has a dimension.
 *
 * @tparam A The dimension.
 */
template<typename A>
concept HasSquareRoot = A::length % 2 == 0 && A::mass % 2 == 0 && A::time % 2 == 0 && A::angle % 2 == 0
    && A::temperature % 2 == 0;

/**
 * The dimension of a square root.
 *
 * @tparam A A dimension with even exponents.
 */
template<typename A>
    requires HasSquareRoot<A>
using DimensionSqrt = Dimension<A::length / 2, A::mass / 2, A::time / 2, A::angle / 2, A::temperature / 2>;

/**
 * A dimension with its angle exponent removed (the radian treated as 1).
 *
 * @tparam A The dimension.
 */
template<typename A>
using DimensionWithoutAngle = Dimension<A::length, A::mass, A::time, 0, A::temperature>;

/** Named dimensions. Exponents in the order length, mass, time, angle, temperature. */
namespace dim
{

using Dimensionless = Dimension<0, 0, 0, 0, 0>;       ///< Pure number.
using Length = Dimension<1, 0, 0, 0, 0>;              ///< m.
using Area = Dimension<2, 0, 0, 0, 0>;                ///< m^2.
using Volume = Dimension<3, 0, 0, 0, 0>;              ///< m^3.
using Mass = Dimension<0, 1, 0, 0, 0>;                ///< kg.
using Time = Dimension<0, 0, 1, 0, 0>;                ///< s.
using Angle = Dimension<0, 0, 0, 1, 0>;               ///< rad.
using Temperature = Dimension<0, 0, 0, 0, 1>;         ///< K.
using Frequency = Dimension<0, 0, -1, 0, 0>;          ///< Hz.
using Velocity = Dimension<1, 0, -1, 0, 0>;           ///< m/s.
using Acceleration = Dimension<1, 0, -2, 0, 0>;       ///< m/s^2.
using Force = Dimension<1, 1, -2, 0, 0>;              ///< N.
using Energy = Dimension<2, 1, -2, 0, 0>;             ///< J; also torque in N m.
using Power = Dimension<2, 1, -3, 0, 0>;              ///< W.
using Momentum = Dimension<1, 1, -1, 0, 0>;           ///< N s; also impulse.
using Pressure = Dimension<-1, 1, -2, 0, 0>;          ///< Pa.
using Density = Dimension<-3, 1, 0, 0, 0>;            ///< kg/m^3.
using Stiffness = Dimension<0, 1, -2, 0, 0>;          ///< N/m.
using Damping = Dimension<0, 1, -1, 0, 0>;            ///< N s/m.
using AngularVelocity = Dimension<0, 0, -1, 1, 0>;    ///< rad/s.
using AngularAcceleration = Dimension<0, 0, -2, 1, 0>; ///< rad/s^2.
using MomentOfInertia = Dimension<2, 1, 0, 0, 0>;     ///< kg m^2.

} // namespace dim

} // namespace sub0math
