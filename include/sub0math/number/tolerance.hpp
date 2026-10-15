#pragma once

namespace sub0math
{

/**
 * Compares two values within an absolute tolerance of the same type.
 *
 * Works for plain numbers and for quantities, where the tolerance carries the unit too
 * (`isNear(a, b, 1_mm)`). NaN is never near anything.
 *
 * @tparam T A type with subtraction and ordering.
 * @param a First value.
 * @param b Second value.
 * @param tolerance Largest accepted absolute difference.
 * @return True when |a - b| <= tolerance.
 */
template<typename T>
[[nodiscard]] constexpr bool isNear(const T& a, const T& b, const T& tolerance) noexcept
{
    const T difference = a > b ? a - b : b - a;
    return difference <= tolerance;
}

} // namespace sub0math
