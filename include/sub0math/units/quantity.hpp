#pragma once

#include <compare>
#include <concepts>
#include <limits>

#include "sub0math/number/real.hpp"
#include "sub0math/units/dimension.hpp"

namespace sub0math
{

/**
 * A number with a physical dimension, stored in coherent SI units (m, kg, s, rad, K).
 *
 * Adding, subtracting and comparing need equal dimensions; multiplying and dividing combine them; a
 * dimension mismatch does not compile. Values enter and leave through named units
 * (`millimetres(350.0)`, `length.in(millimetres)`) or SI explicitly (`fromSi`, `si`); there is no
 * implicit conversion to or from a plain number except for dimensionless results. Same size as the
 * representation, trivially copyable and usable in constant expressions.
 *
 * A float quantity converts implicitly to a double one; double to float is explicit, so precision is
 * never lost silently.
 *
 * @tparam D The Dimension.
 * @tparam Rep Floating-point representation.
 */
template<typename D, Real Rep = DefaultReal>
class Quantity
{
public:
    using DimensionType = D;    ///< The dimension.
    using Representation = Rep; ///< The stored number type.

    /** Zero. */
    constexpr Quantity() noexcept = default;

    /**
     * Converts between representations of the same dimension; explicit when it can lose precision.
     *
     * @tparam Other Source representation.
     * @param other The quantity to convert.
     */
    template<Real Other>
        requires(!std::same_as<Other, Rep>)
    constexpr explicit(std::numeric_limits<Other>::digits > std::numeric_limits<Rep>::digits)
        Quantity(const Quantity<D, Other>& other) noexcept
        : value_(static_cast<Rep>(other.si()))
    {
    }

    /**
     * Makes a quantity from a value already in coherent SI units.
     *
     * @param si Value in m, kg, s, rad or K combinations.
     * @return The quantity.
     */
    [[nodiscard]] static constexpr Quantity fromSi(Rep si) noexcept
    {
        Quantity q;
        q.value_ = si;
        return q;
    }

    /** @return The value in coherent SI units. */
    [[nodiscard]] constexpr Rep si() const noexcept
    {
        return value_;
    }

    /**
     * Reads the value in a named unit of the same dimension.
     *
     * @tparam U A unit type; a unit of another dimension does not compile.
     * @param unit The unit, e.g. `millimetres`.
     * @return The value expressed in that unit.
     */
    template<typename U>
        requires std::same_as<typename U::DimensionType, D>
    [[nodiscard]] constexpr Rep in(const U& unit) const noexcept
    {
        using Wide = decltype(Rep{} * 1.0);
        return static_cast<Rep>(static_cast<Wide>(value_) * static_cast<Wide>(unit.denominator)
                                / static_cast<Wide>(unit.numerator));
    }

    /** @return The plain number, for a dimensionless quantity only. */
    constexpr operator Rep() const noexcept // NOLINT(google-explicit-constructor): ratios are numbers
        requires std::same_as<D, dim::Dimensionless>
    {
        return value_;
    }

    constexpr Quantity& operator+=(const Quantity& other) noexcept
    {
        value_ += other.value_;
        return *this;
    }

    constexpr Quantity& operator-=(const Quantity& other) noexcept
    {
        value_ -= other.value_;
        return *this;
    }

    constexpr Quantity& operator*=(Rep factor) noexcept
    {
        value_ *= factor;
        return *this;
    }

    constexpr Quantity& operator/=(Rep divisor) noexcept
    {
        value_ /= divisor;
        return *this;
    }

    friend constexpr Quantity operator+(Quantity a, const Quantity& b) noexcept
    {
        a += b;
        return a;
    }

    friend constexpr Quantity operator-(Quantity a, const Quantity& b) noexcept
    {
        a -= b;
        return a;
    }

    friend constexpr Quantity operator-(const Quantity& q) noexcept
    {
        return fromSi(-q.value_);
    }

    friend constexpr Quantity operator+(const Quantity& q) noexcept
    {
        return q;
    }

    friend constexpr Quantity operator*(Quantity q, Rep factor) noexcept
    {
        q *= factor;
        return q;
    }

    friend constexpr Quantity operator*(Rep factor, Quantity q) noexcept
    {
        q *= factor;
        return q;
    }

    friend constexpr Quantity operator/(Quantity q, Rep divisor) noexcept
    {
        q /= divisor;
        return q;
    }

    friend constexpr Quantity<DimensionQuotient<dim::Dimensionless, D>, Rep> operator/(Rep dividend,
                                                                                      const Quantity& q) noexcept
    {
        return Quantity<DimensionQuotient<dim::Dimensionless, D>, Rep>::fromSi(dividend / q.value_);
    }

    friend constexpr bool operator==(const Quantity&, const Quantity&) noexcept = default;
    friend constexpr auto operator<=>(const Quantity&, const Quantity&) noexcept = default;

private:
    Rep value_{};
};

/**
 * Multiplies quantities, combining their dimensions.
 *
 * @tparam D1 First dimension.
 * @tparam D2 Second dimension.
 * @tparam Rep Shared representation.
 * @param a First factor.
 * @param b Second factor.
 * @return The product.
 */
template<typename D1, typename D2, Real Rep>
[[nodiscard]] constexpr Quantity<DimensionProduct<D1, D2>, Rep> operator*(const Quantity<D1, Rep>& a,
                                                                          const Quantity<D2, Rep>& b) noexcept
{
    return Quantity<DimensionProduct<D1, D2>, Rep>::fromSi(a.si() * b.si());
}

/**
 * Divides quantities, combining their dimensions; equal dimensions give a dimensionless ratio.
 *
 * @tparam D1 Dividend dimension.
 * @tparam D2 Divisor dimension.
 * @tparam Rep Shared representation.
 * @param a Dividend.
 * @param b Divisor.
 * @return The quotient.
 */
template<typename D1, typename D2, Real Rep>
[[nodiscard]] constexpr Quantity<DimensionQuotient<D1, D2>, Rep> operator/(const Quantity<D1, Rep>& a,
                                                                           const Quantity<D2, Rep>& b) noexcept
{
    return Quantity<DimensionQuotient<D1, D2>, Rep>::fromSi(a.si() / b.si());
}

/** A quantity's plain number type is its representation. */
template<typename D, Real Rep>
struct ScalarTraits<Quantity<D, Rep>>
{
    using Scalar = Rep; ///< The representation.
};

/**
 * True for any Quantity.
 *
 * @tparam T Candidate type.
 */
template<typename T>
concept IsQuantity = requires {
    typename T::DimensionType;
    typename T::Representation;
} && std::same_as<T, Quantity<typename T::DimensionType, typename T::Representation>>;

} // namespace sub0math
