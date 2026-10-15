#pragma once

#include <compare>
#include <concepts>
#include <type_traits>
#include <utility>

namespace sub0math
{

/**
 * Gives a value a role, so arguments of the same type cannot be swapped or left unnamed.
 *
 * ```cpp
 * using Height = sub0math::Role<struct HeightTag, Length>;
 * using Width = sub0math::Role<struct WidthTag, Length>;
 * Box makeBox(Height h, Width w);
 * makeBox(Height{1_m}, Width{5_m});  // compiles
 * makeBox(Width{5_m}, Height{1_m});  // does not
 * makeBox(1_m, 5_m);                 // does not
 * Height{1};                         // does not: a length needs its unit
 * ```
 *
 * Construction is explicit and roles never convert into each other. A role adds no arithmetic: do the
 * maths on `value()`. Same size as the value and trivially copyable when the value is.
 *
 * @tparam Tag A unique tag, usually declared inline: `struct HeightTag`.
 * @tparam T The value type: a quantity, a plain number, an id.
 */
template<typename Tag, typename T>
class Role
{
public:
    using ValueType = T; ///< The wrapped value type.

    /**
     * Assigns the role.
     *
     * @param value The value.
     */
    constexpr explicit Role(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : value_(std::move(value))
    {
    }

    /** @return The value. */
    [[nodiscard]] constexpr const T& value() const noexcept
    {
        return value_;
    }

    friend constexpr bool operator==(const Role&, const Role&) = default;
    friend constexpr auto operator<=>(const Role&, const Role&)
        requires std::three_way_comparable<T>
    = default;

private:
    T value_;
};

} // namespace sub0math
