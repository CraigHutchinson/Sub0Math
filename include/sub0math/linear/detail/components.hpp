#pragma once

#include <array>
#include <cstddef>

namespace sub0math::detail
{

// Storage shared by Vec and Point: named members for 2-4 components (v.x, v.y) so values read naturally
// and layouts match graphics APIs; an array beyond that. `at` gives uniform indexed access, which
// compilers resolve statically in unrolled loops.

template<typename T, std::size_t N>
struct Components
{
    std::array<T, N> e{};

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
