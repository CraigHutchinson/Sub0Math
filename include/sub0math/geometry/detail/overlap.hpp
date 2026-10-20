#pragma once

#include <cmath>
#include <limits>

#include "sub0math/config.hpp"
#include "sub0math/number/real.hpp"
#include "sub0math/units/functions.hpp"

namespace sub0math::detail
{

// Absolute value of a number or a quantity: `using std::abs` finds the number overloads and argument
// lookup finds sub0math::abs for quantities.
template<typename T>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr T absolute(T x) noexcept
{
    using std::abs;
    return abs(x);
}

// Added to every |rotation term| of the separating-axis tests, so near-parallel box edges (a cross
// product close to zero) cannot produce a false separation from rounding.
template<typename S>
[[nodiscard]] SUB0MATH_FORCE_INLINE constexpr S satTolerance() noexcept
{
    return std::numeric_limits<S>::epsilon() * S{16};
}

} // namespace sub0math::detail
