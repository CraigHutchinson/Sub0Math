#pragma once

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace sub0math::detail
{

// Storage shared by Vec and Point: named members for 2-4 components (v.x, v.y) so values read naturally
// and layouts match graphics APIs; an array beyond that. `get<I>()` is compile-time access and
// `forEachComponent` expands an operation over it with no loop and no runtime index: measured, a loop
// over the runtime `at(i)` cost about 45% in a 2D distance kernel. `at(i)` remains for runtime indices.

// Calls f(std::integral_constant<std::size_t, I>{}) for I = 0..N-1, fully expanded.
template<std::size_t N, typename F>
constexpr void forEachComponent(F&& f)
{
    [&]<std::size_t... I>(std::index_sequence<I...>) { (f(std::integral_constant<std::size_t, I>{}), ...); }(
        std::make_index_sequence<N>{});
}

template<typename T, std::size_t N>
struct Components
{
    std::array<T, N> e{};

    template<std::size_t I>
    constexpr T& get() noexcept
    {
        return e[I];
    }

    template<std::size_t I>
    constexpr const T& get() const noexcept
    {
        return e[I];
    }

    constexpr T& at(std::size_t i) noexcept
    {
        return e[i];
    }

    constexpr const T& at(std::size_t i) const noexcept
    {
        return e[i];
    }

    friend constexpr bool operator==(const Components&, const Components&) = default;
};

template<typename T>
struct Components<T, 2>
{
    T x{};
    T y{};

    template<std::size_t I>
    constexpr T& get() noexcept
    {
        if constexpr (I == 0)
        {
            return x;
        }
        else
        {
            return y;
        }
    }

    template<std::size_t I>
    constexpr const T& get() const noexcept
    {
        if constexpr (I == 0)
        {
            return x;
        }
        else
        {
            return y;
        }
    }

    constexpr T& at(std::size_t i) noexcept
    {
        return i == 0 ? x : y;
    }

    constexpr const T& at(std::size_t i) const noexcept
    {
        return i == 0 ? x : y;
    }

    friend constexpr bool operator==(const Components&, const Components&) = default;
};

template<typename T>
struct Components<T, 3>
{
    T x{};
    T y{};
    T z{};

    template<std::size_t I>
    constexpr T& get() noexcept
    {
        if constexpr (I == 0)
        {
            return x;
        }
        else if constexpr (I == 1)
        {
            return y;
        }
        else
        {
            return z;
        }
    }

    template<std::size_t I>
    constexpr const T& get() const noexcept
    {
        if constexpr (I == 0)
        {
            return x;
        }
        else if constexpr (I == 1)
        {
            return y;
        }
        else
        {
            return z;
        }
    }

    constexpr T& at(std::size_t i) noexcept
    {
        return i == 0 ? x : (i == 1 ? y : z);
    }

    constexpr const T& at(std::size_t i) const noexcept
    {
        return i == 0 ? x : (i == 1 ? y : z);
    }

    friend constexpr bool operator==(const Components&, const Components&) = default;
};

template<typename T>
struct Components<T, 4>
{
    T x{};
    T y{};
    T z{};
    T w{};

    template<std::size_t I>
    constexpr T& get() noexcept
    {
        if constexpr (I == 0)
        {
            return x;
        }
        else if constexpr (I == 1)
        {
            return y;
        }
        else if constexpr (I == 2)
        {
            return z;
        }
        else
        {
            return w;
        }
    }

    template<std::size_t I>
    constexpr const T& get() const noexcept
    {
        if constexpr (I == 0)
        {
            return x;
        }
        else if constexpr (I == 1)
        {
            return y;
        }
        else if constexpr (I == 2)
        {
            return z;
        }
        else
        {
            return w;
        }
    }

    constexpr T& at(std::size_t i) noexcept
    {
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }

    constexpr const T& at(std::size_t i) const noexcept
    {
        return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w));
    }

    friend constexpr bool operator==(const Components&, const Components&) = default;
};

} // namespace sub0math::detail
