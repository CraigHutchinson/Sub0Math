#pragma once

#include <numbers>

#include "sub0math/units/dimension.hpp"
#include "sub0math/units/unit.hpp"

namespace sub0math
{

// Units for entering and leaving quantities. Values are always stored in coherent SI; a unit is only
// its size in SI as an exact ratio (numerator, denominator) and its symbol.

inline constexpr Unit<dim::Dimensionless> one{1.0, 1.0, ""};    ///< A pure number.
inline constexpr Unit<dim::Dimensionless> percent{1.0, 100.0, "%"}; ///< Hundredths.

inline constexpr Unit<dim::Length> metres{1.0, 1.0, "m"};          ///< SI length.
inline constexpr Unit<dim::Length> kilometres{1000.0, 1.0, "km"};  ///< 1000 m.
inline constexpr Unit<dim::Length> centimetres{1.0, 100.0, "cm"};   ///< 0.01 m.
inline constexpr Unit<dim::Length> millimetres{1.0, 1000.0, "mm"};  ///< 0.001 m.
inline constexpr Unit<dim::Length> micrometres{1.0, 1e6, "um"};   ///< 1e-6 m.

inline constexpr Unit<dim::Mass> kilograms{1.0, 1.0, "kg"}; ///< SI mass.
inline constexpr Unit<dim::Mass> grams{1.0, 1000.0, "g"};    ///< 0.001 kg.

inline constexpr Unit<dim::Time> seconds{1.0, 1.0, "s"};         ///< SI time.
inline constexpr Unit<dim::Time> milliseconds{1.0, 1000.0, "ms"}; ///< 0.001 s.
inline constexpr Unit<dim::Time> microseconds{1.0, 1e6, "us"};  ///< 1e-6 s.
inline constexpr Unit<dim::Time> minutes{60.0, 1.0, "min"};      ///< 60 s.
inline constexpr Unit<dim::Time> hours{3600.0, 1.0, "h"};        ///< 3600 s.

inline constexpr Unit<dim::Angle> radians{1.0, 1.0, "rad"};                          ///< SI angle.
inline constexpr Unit<dim::Angle> degrees{std::numbers::pi, 180.0, "deg"};     ///< pi/180 rad.
inline constexpr Unit<dim::Angle> revolutions{2.0 * std::numbers::pi, 1.0, "rev"};  ///< 2 pi rad.

inline constexpr Unit<dim::Temperature> kelvin{1.0, 1.0, "K"}; ///< SI temperature; Celsius is affine and not a unit here.

inline constexpr Unit<dim::Frequency> hertz{1.0, 1.0, "Hz"}; ///< 1/s.

inline constexpr Unit<dim::Velocity> metresPerSecond{1.0, 1.0, "m/s"};          ///< SI velocity.
inline constexpr Unit<dim::Velocity> kilometresPerHour{1000.0, 3600.0, "km/h"}; ///< 1/3.6 m/s.

inline constexpr Unit<dim::Acceleration> metresPerSecondSquared{1.0, 1.0, "m/s^2"}; ///< SI acceleration.
inline constexpr Unit<dim::Acceleration> standardGravities{9.80665, 1.0, "g"};      ///< Standard gravity, exact.

inline constexpr Unit<dim::Force> newtons{1.0, 1.0, "N"};        ///< SI force.
inline constexpr Unit<dim::Force> kilonewtons{1000.0, 1.0, "kN"}; ///< 1000 N.

inline constexpr Unit<dim::Energy> joules{1.0, 1.0, "J"};       ///< SI energy.
inline constexpr Unit<dim::Energy> newtonMetres{1.0, 1.0, "N m"}; ///< Torque; same dimension as energy.

inline constexpr Unit<dim::Power> watts{1.0, 1.0, "W"}; ///< SI power.

inline constexpr Unit<dim::Momentum> newtonSeconds{1.0, 1.0, "N s"}; ///< Impulse or momentum.

inline constexpr Unit<dim::Pressure> pascals{1.0, 1.0, "Pa"};    ///< SI pressure.
inline constexpr Unit<dim::Pressure> bar{100000.0, 1.0, "bar"};  ///< 1e5 Pa.
inline constexpr Unit<dim::Pressure> psi{6894.757293168361, 1.0, "psi"}; ///< Pound-force per square inch.

inline constexpr Unit<dim::Density> kilogramsPerCubicMetre{1.0, 1.0, "kg/m^3"}; ///< SI density.

inline constexpr Unit<dim::Stiffness> newtonsPerMetre{1.0, 1.0, "N/m"};         ///< Spring rate.
inline constexpr Unit<dim::Stiffness> newtonsPerMillimetre{1000.0, 1.0, "N/mm"}; ///< Spring rate as quoted for coils.

inline constexpr Unit<dim::Damping> newtonSecondsPerMetre{1.0, 1.0, "N s/m"}; ///< Damping coefficient.

inline constexpr Unit<dim::AngularVelocity> radiansPerSecond{1.0, 1.0, "rad/s"};                        ///< SI angular velocity.
inline constexpr Unit<dim::AngularVelocity> revolutionsPerMinute{2.0 * std::numbers::pi, 60.0, "rpm"}; ///< 2 pi/60 rad/s.

inline constexpr Unit<dim::AngularAcceleration> radiansPerSecondSquared{1.0, 1.0, "rad/s^2"}; ///< SI angular acceleration.

inline constexpr Unit<dim::MomentOfInertia> kilogramMetresSquared{1.0, 1.0, "kg m^2"}; ///< SI moment of inertia.

} // namespace sub0math
