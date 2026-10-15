#pragma once

#include <concepts>
#include <string_view>
#include <type_traits>

#include "sub0math/number/real.hpp"
#include "sub0math/units/quantity.hpp"

namespace sub0math
{

/**
 * A named unit: its size in coherent SI as an exact ratio, and a symbol. Units are only used to enter
 * and leave quantities; they are never part of a quantity's type.
 *
 * The ratio keeps conversions correctly rounded where a decimal factor cannot: 350 mm is 350 / 1000 m,
 * exactly 0.35 in double, whereas 350 * 0.001 is not.
 *
 * Call a unit to make a quantity: `millimetres(350.0)` (double), `millimetres(350.0f)` or
 * `millimetres(350)` (DefaultReal).
 *
 * @tparam D The dimension the unit measures.
 */
template<typename D>
struct Unit
{
    using DimensionType = D; ///< The dimension.

    double numerator;        ///< One of this unit is numerator / denominator SI units.
    double denominator;      ///< See numerator; exact powers of ten and small integers keep rounding to one step.
    std::string_view symbol; ///< Conventional symbol, e.g. "mm".

    /**
     * Makes a DefaultReal quantity; also accepts integers.
     *
     * @param value Amount in this unit.
     * @return The quantity.
     */
    [[nodiscard]] constexpr Quantity<D, DefaultReal> operator()(DefaultReal value) const noexcept
    {
        return make<DefaultReal>(value);
    }

    /**
     * Makes a quantity in the argument's floating-point type.
     *
     * @tparam R The representation, deduced.
     * @param value Amount in this unit.
     * @return The quantity.
     */
    template<Real R>
        requires(!std::same_as<R, DefaultReal>)
    [[nodiscard]] constexpr Quantity<D, R> operator()(R value) const noexcept
    {
        return make<R>(value);
    }

private:
    template<Real R>
    [[nodiscard]] constexpr Quantity<D, R> make(R value) const noexcept
    {
        // Scale in at least double so a float result is rounded once.
        using Wide = std::common_type_t<R, double>;
        return Quantity<D, R>::fromSi(static_cast<R>(static_cast<Wide>(value) * static_cast<Wide>(numerator)
                                                     / static_cast<Wide>(denominator)));
    }
};

} // namespace sub0math
