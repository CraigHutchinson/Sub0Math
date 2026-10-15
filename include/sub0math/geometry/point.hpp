#pragma once

#include <cstddef>

#include "sub0math/linear/detail/components.hpp"
#include "sub0math/linear/vec.hpp"

namespace sub0math
{

/**
 * A position, as opposed to a displacement (Vec).
 *
 * Positions follow affine rules: point - point is a Vec, point + Vec is a point, and point + point does
 * not compile. Same storage and named components as Vec (`p.x`, `p.y`); an aggregate:
 * `Point2d{1.0, 2.0}`. Components may be quantities (`Point<f64::Length, 3>`).
 *
 * @tparam T Coordinate type: a floating-point number or a Quantity.
 * @tparam N Number of coordinates.
 */
template<typename T, std::size_t N>
struct Point : detail::Components<T, N>
{
    using ValueType = T;                   ///< Coordinate type.
    static constexpr std::size_t Size = N; ///< Number of coordinates.

    /** @return The origin. */
    [[nodiscard]] static constexpr Point origin() noexcept
    {
        return Point{};
    }

    /**
     * Accesses a coordinate.
     *
     * @param i Index, less than N.
     * @return The coordinate.
     */
    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
    {
        return this->at(i);
    }

    /**
     * Reads a coordinate.
     *
     * @param i Index, less than N.
     * @return The coordinate.
     */
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
    {
        return this->at(i);
    }

    constexpr Point& operator+=(const Vec<T, N>& offset) noexcept
    {
        detail::forEachComponent<N>([&](auto i) {
            this->template get<i>() += offset.template get<i>();
        });
        return *this;
    }

    constexpr Point& operator-=(const Vec<T, N>& offset) noexcept
    {
        detail::forEachComponent<N>([&](auto i) {
            this->template get<i>() -= offset.template get<i>();
        });
        return *this;
    }

    friend constexpr Point operator+(Point p, const Vec<T, N>& offset) noexcept
    {
        p += offset;
        return p;
    }

    friend constexpr Point operator-(Point p, const Vec<T, N>& offset) noexcept
    {
        p -= offset;
        return p;
    }

    friend constexpr Vec<T, N> operator-(const Point& a, const Point& b) noexcept
    {
        Vec<T, N> d{};
        detail::forEachComponent<N>([&](auto i) {
            d.template get<i>() = a.template get<i>() - b.template get<i>();
        });
        return d;
    }

    friend constexpr bool operator==(const Point&, const Point&) = default;
};

template<typename T>
using Point2 = Point<T, 2>; ///< 2D position of T.
template<typename T>
using Point3 = Point<T, 3>; ///< 3D position of T.

using Point2f = Point2<float>;  ///< Float 2D position.
using Point3f = Point3<float>;  ///< Float 3D position.
using Point2d = Point2<double>; ///< Double 2D position.
using Point3d = Point3<double>; ///< Double 3D position.

} // namespace sub0math
