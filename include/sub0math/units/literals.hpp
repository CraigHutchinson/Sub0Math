#pragma once

#include "sub0math/units/dimension.hpp"
#include "sub0math/units/quantity.hpp"
#include "sub0math/units/si_units.hpp"

// Literal suffixes. Each translation unit picks one precision: `using namespace sub0math::literals;`
// (DefaultReal, float) or `using namespace sub0math::f64::literals;` (double). Using both is ambiguous by design.
// Suffixes are lower case: identifiers starting with an underscore and a capital letter are reserved.

namespace sub0math
{

/** Literals for DefaultReal (float) quantities: `1.5_m`, `250_ms`, `30_deg`. */
namespace literals
{

[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_m(long double v) noexcept { return metres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_m(unsigned long long v) noexcept { return metres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_km(long double v) noexcept { return kilometres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_km(unsigned long long v) noexcept { return kilometres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_cm(long double v) noexcept { return centimetres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_cm(unsigned long long v) noexcept { return centimetres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_mm(long double v) noexcept { return millimetres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, float> operator""_mm(unsigned long long v) noexcept { return millimetres(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, float> operator""_kg(long double v) noexcept { return kilograms(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, float> operator""_kg(unsigned long long v) noexcept { return kilograms(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, float> operator""_g(long double v) noexcept { return grams(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, float> operator""_g(unsigned long long v) noexcept { return grams(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, float> operator""_s(long double v) noexcept { return seconds(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, float> operator""_s(unsigned long long v) noexcept { return seconds(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, float> operator""_ms(long double v) noexcept { return milliseconds(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, float> operator""_ms(unsigned long long v) noexcept { return milliseconds(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, float> operator""_us(long double v) noexcept { return microseconds(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, float> operator""_us(unsigned long long v) noexcept { return microseconds(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, float> operator""_rad(long double v) noexcept { return radians(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, float> operator""_rad(unsigned long long v) noexcept { return radians(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, float> operator""_deg(long double v) noexcept { return degrees(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, float> operator""_deg(unsigned long long v) noexcept { return degrees(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, float> operator""_mps(long double v) noexcept { return metresPerSecond(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, float> operator""_mps(unsigned long long v) noexcept { return metresPerSecond(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, float> operator""_kmh(long double v) noexcept { return kilometresPerHour(static_cast<float>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, float> operator""_kmh(unsigned long long v) noexcept { return kilometresPerHour(static_cast<float>(v)); }

} // namespace literals

/** Literals for double quantities. */
namespace f64::literals
{

[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_m(long double v) noexcept { return metres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_m(unsigned long long v) noexcept { return metres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_km(long double v) noexcept { return kilometres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_km(unsigned long long v) noexcept { return kilometres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_cm(long double v) noexcept { return centimetres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_cm(unsigned long long v) noexcept { return centimetres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_mm(long double v) noexcept { return millimetres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Length, double> operator""_mm(unsigned long long v) noexcept { return millimetres(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, double> operator""_kg(long double v) noexcept { return kilograms(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, double> operator""_kg(unsigned long long v) noexcept { return kilograms(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, double> operator""_g(long double v) noexcept { return grams(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Mass, double> operator""_g(unsigned long long v) noexcept { return grams(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, double> operator""_s(long double v) noexcept { return seconds(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, double> operator""_s(unsigned long long v) noexcept { return seconds(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, double> operator""_ms(long double v) noexcept { return milliseconds(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, double> operator""_ms(unsigned long long v) noexcept { return milliseconds(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, double> operator""_us(long double v) noexcept { return microseconds(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Time, double> operator""_us(unsigned long long v) noexcept { return microseconds(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, double> operator""_rad(long double v) noexcept { return radians(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, double> operator""_rad(unsigned long long v) noexcept { return radians(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, double> operator""_deg(long double v) noexcept { return degrees(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Angle, double> operator""_deg(unsigned long long v) noexcept { return degrees(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, double> operator""_mps(long double v) noexcept { return metresPerSecond(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, double> operator""_mps(unsigned long long v) noexcept { return metresPerSecond(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, double> operator""_kmh(long double v) noexcept { return kilometresPerHour(static_cast<double>(v)); }
[[nodiscard]] constexpr Quantity<dim::Velocity, double> operator""_kmh(unsigned long long v) noexcept { return kilometresPerHour(static_cast<double>(v)); }

} // namespace f64::literals

} // namespace sub0math
