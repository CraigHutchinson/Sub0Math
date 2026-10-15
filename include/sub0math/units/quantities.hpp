#pragma once

#include "sub0math/units/dimension.hpp"
#include "sub0math/units/quantity.hpp"

namespace sub0math
{

/** Float quantities: rendering, firmware and interfaces. */
namespace f32
{

using Dimensionless = Quantity<dim::Dimensionless, float>; ///< Dimensionless, float.
using Length = Quantity<dim::Length, float>; ///< Length, float.
using Area = Quantity<dim::Area, float>; ///< Area, float.
using Volume = Quantity<dim::Volume, float>; ///< Volume, float.
using Mass = Quantity<dim::Mass, float>; ///< Mass, float.
using Time = Quantity<dim::Time, float>; ///< Time, float.
using Angle = Quantity<dim::Angle, float>; ///< Angle, float.
using Temperature = Quantity<dim::Temperature, float>; ///< Temperature, float.
using Frequency = Quantity<dim::Frequency, float>; ///< Frequency, float.
using Velocity = Quantity<dim::Velocity, float>; ///< Velocity, float.
using Acceleration = Quantity<dim::Acceleration, float>; ///< Acceleration, float.
using Force = Quantity<dim::Force, float>; ///< Force, float.
using Energy = Quantity<dim::Energy, float>; ///< Energy, float.
using Power = Quantity<dim::Power, float>; ///< Power, float.
using Momentum = Quantity<dim::Momentum, float>; ///< Momentum, float.
using Pressure = Quantity<dim::Pressure, float>; ///< Pressure, float.
using Density = Quantity<dim::Density, float>; ///< Density, float.
using Stiffness = Quantity<dim::Stiffness, float>; ///< Stiffness, float.
using Damping = Quantity<dim::Damping, float>; ///< Damping, float.
using AngularVelocity = Quantity<dim::AngularVelocity, float>; ///< AngularVelocity, float.
using AngularAcceleration = Quantity<dim::AngularAcceleration, float>; ///< AngularAcceleration, float.
using MomentOfInertia = Quantity<dim::MomentOfInertia, float>; ///< MomentOfInertia, float.
using Torque = Energy;   ///< N m; same dimension as energy.
using Impulse = Momentum; ///< N s; same dimension as momentum.

} // namespace f32

/** Double quantities: simulation state, accumulated time, world positions, energy budgets. */
namespace f64
{

using Dimensionless = Quantity<dim::Dimensionless, double>; ///< Dimensionless, double.
using Length = Quantity<dim::Length, double>; ///< Length, double.
using Area = Quantity<dim::Area, double>; ///< Area, double.
using Volume = Quantity<dim::Volume, double>; ///< Volume, double.
using Mass = Quantity<dim::Mass, double>; ///< Mass, double.
using Time = Quantity<dim::Time, double>; ///< Time, double.
using Angle = Quantity<dim::Angle, double>; ///< Angle, double.
using Temperature = Quantity<dim::Temperature, double>; ///< Temperature, double.
using Frequency = Quantity<dim::Frequency, double>; ///< Frequency, double.
using Velocity = Quantity<dim::Velocity, double>; ///< Velocity, double.
using Acceleration = Quantity<dim::Acceleration, double>; ///< Acceleration, double.
using Force = Quantity<dim::Force, double>; ///< Force, double.
using Energy = Quantity<dim::Energy, double>; ///< Energy, double.
using Power = Quantity<dim::Power, double>; ///< Power, double.
using Momentum = Quantity<dim::Momentum, double>; ///< Momentum, double.
using Pressure = Quantity<dim::Pressure, double>; ///< Pressure, double.
using Density = Quantity<dim::Density, double>; ///< Density, double.
using Stiffness = Quantity<dim::Stiffness, double>; ///< Stiffness, double.
using Damping = Quantity<dim::Damping, double>; ///< Damping, double.
using AngularVelocity = Quantity<dim::AngularVelocity, double>; ///< AngularVelocity, double.
using AngularAcceleration = Quantity<dim::AngularAcceleration, double>; ///< AngularAcceleration, double.
using MomentOfInertia = Quantity<dim::MomentOfInertia, double>; ///< MomentOfInertia, double.
using Torque = Energy;   ///< N m; same dimension as energy.
using Impulse = Momentum; ///< N s; same dimension as momentum.

} // namespace f64

// The unqualified names use DefaultReal (float). Code that needs double says so with sub0math::f64.

using f32::Dimensionless;
using f32::Length;
using f32::Area;
using f32::Volume;
using f32::Mass;
using f32::Time;
using f32::Angle;
using f32::Temperature;
using f32::Frequency;
using f32::Velocity;
using f32::Acceleration;
using f32::Force;
using f32::Energy;
using f32::Power;
using f32::Momentum;
using f32::Pressure;
using f32::Density;
using f32::Stiffness;
using f32::Damping;
using f32::AngularVelocity;
using f32::AngularAcceleration;
using f32::MomentOfInertia;
using f32::Torque;
using f32::Impulse;

} // namespace sub0math
