#pragma once

#include <cmath>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "sub0math/linear/detail/components.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A fixed-size vector of plain numbers or quantities: a displacement, velocity, force or direction.
 *
 * Components are named for sizes 2-4 (`v.x`, `v.y`, `v.z`, `v.w`) and laid out contiguously like the
 * matching graphics types; `v[i]` works for every size. An aggregate: `Vec3f{1.0f, 2.0f, 3.0f}`. With
 * quantity components the dimension rules carry through: a velocity vector times a time is a
 * displacement vector, and a dot product of two lengths is an area.
 *
 * A position is a Point, not a Vec: subtracting two points gives a Vec.
 *
 * @tparam T Component type: a floating-point number or a Quantity.
 * @tparam N Number of components.
 */
template<typename T, std::size_t N>
struct Vec : detail::Components<T, N>
{
    using ValueType = T;                   ///< Component type.
    static constexpr std::size_t Size = N; ///< Number of components.

    /**
     * Accesses a component.
     *
     * @param i Index, less than N.
     * @return The component.
     */
    [[nodiscard]] constexpr T& operator[](std::size_t i) noexcept
    {
        return this->at(i);
    }

    /**
     * Reads a component.
     *
     * @param i Index, less than N.
     * @return The component.
     */
    [[nodiscard]] constexpr const T& operator[](std::size_t i) const noexcept
    {
        return this->at(i);
    }

    constexpr Vec& operator+=(const Vec& other) noexcept
    {
        detail::forEachComponent<N>([&](auto i) {
            this->template get<i>() += other.template get<i>();
        });
        return *this;
    }

    constexpr Vec& operator-=(const Vec& other) noexcept
    {
        detail::forEachComponent<N>([&](auto i) {
            this->template get<i>() -= other.template get<i>();
        });
        return *this;
    }

    friend constexpr Vec operator+(Vec a, const Vec& b) noexcept
    {
        a += b;
        return a;
    }

    friend constexpr Vec operator-(Vec a, const Vec& b) noexcept
    {
        a -= b;
        return a;
    }

    friend constexpr Vec operator-(Vec v) noexcept
    {
        detail::forEachComponent<N>([&](auto i) {
            v.template get<i>() = -v.template get<i>();
        });
        return v;
    }

    friend constexpr bool operator==(const Vec&, const Vec&) = default;
};

/**
 * True for any Vec.
 *
 * @tparam T Candidate type.
 */
template<typename T>
concept IsVec = requires {
    typename T::ValueType;
    T::Size;
} && std::same_as<T, Vec<typename T::ValueType, T::Size>>;

/**
 * The type of a product of two values (number or quantity).
 *
 * @tparam A First factor type.
 * @tparam B Second factor type.
 */
template<typename A, typename B>
using ProductType = decltype(std::declval<A>() * std::declval<B>());

/**
 * Scales a vector by a plain number; the components keep their type (a float vector stays float).
 *
 * @param v The vector.
 * @param factor The factor, converted to the vector's scalar type.
 * @return The scaled vector.
 */
template<typename T, std::size_t N, typename S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr Vec<T, N> operator*(Vec<T, N> v, S factor) noexcept
{
    const auto f = static_cast<ScalarOf<T>>(factor);
    detail::forEachComponent<N>([&](auto i) {
        v.template get<i>() = v.template get<i>() * f;
    });
    return v;
}

/**
 * Scales a vector by a plain number.
 *
 * @param factor The factor.
 * @param v The vector.
 * @return The scaled vector.
 */
template<typename T, std::size_t N, typename S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr Vec<T, N> operator*(S factor, const Vec<T, N>& v) noexcept
{
    return v * factor;
}

/**
 * Divides a vector by a plain number.
 *
 * @param v The vector.
 * @param divisor The divisor.
 * @return The divided vector.
 */
template<typename T, std::size_t N, typename S>
    requires std::is_arithmetic_v<S>
[[nodiscard]] constexpr Vec<T, N> operator/(Vec<T, N> v, S divisor) noexcept
{
    const auto d = static_cast<ScalarOf<T>>(divisor);
    detail::forEachComponent<N>([&](auto i) {
        v.template get<i>() = v.template get<i>() / d;
    });
    return v;
}

/**
 * Multiplies a vector by a quantity: velocity times time is a displacement.
 *
 * @param v The vector.
 * @param q The quantity.
 * @return The vector of products.
 */
template<typename T, std::size_t N, typename Q>
    requires(!std::is_arithmetic_v<Q> && !IsVec<Q>)
[[nodiscard]] constexpr Vec<ProductType<T, Q>, N> operator*(const Vec<T, N>& v, const Q& q) noexcept
{
    Vec<ProductType<T, Q>, N> out{};
    detail::forEachComponent<N>([&](auto i) {
        out.template get<i>() = v.template get<i>() * q;
    });
    return out;
}

/**
 * Multiplies a quantity by a vector.
 *
 * @param q The quantity.
 * @param v The vector.
 * @return The vector of products.
 */
template<typename T, std::size_t N, typename Q>
    requires(!std::is_arithmetic_v<Q> && !IsVec<Q>)
[[nodiscard]] constexpr Vec<ProductType<Q, T>, N> operator*(const Q& q, const Vec<T, N>& v) noexcept
{
    Vec<ProductType<Q, T>, N> out{};
    detail::forEachComponent<N>([&](auto i) {
        out.template get<i>() = q * v.template get<i>();
    });
    return out;
}

/**
 * Divides a vector by a quantity: a displacement over a time is a velocity.
 *
 * @param v The vector.
 * @param q The quantity.
 * @return The vector of quotients.
 */
template<typename T, std::size_t N, typename Q>
    requires(!std::is_arithmetic_v<Q> && !IsVec<Q>)
[[nodiscard]] constexpr Vec<decltype(std::declval<T>() / std::declval<Q>()), N> operator/(const Vec<T, N>& v,
                                                                                        const Q& q) noexcept
{
    Vec<decltype(std::declval<T>() / std::declval<Q>()), N> out{};
    detail::forEachComponent<N>([&](auto i) {
        out.template get<i>() = v.template get<i>() / q;
    });
    return out;
}

/**
 * Computes the dot product.
 *
 * @param a First vector.
 * @param b Second vector.
 * @return Sum of component products, e.g. an area for two lengths.
 */
template<typename A, typename B, std::size_t N>
[[nodiscard]] constexpr ProductType<A, B> dot(const Vec<A, N>& a, const Vec<B, N>& b) noexcept
{
    // PERF: first-product start faster than zero-initialised sum (IEEE).
    ProductType<A, B> sum = a.template get<0>() * b.template get<0>();
    detail::forEachComponent<N - 1>([&](auto i) {
        sum += a.template get<i + 1>() * b.template get<i + 1>();
    });
    return sum;
}

/**
 * Computes the cross product of 3-vectors.
 *
 * @param a First vector.
 * @param b Second vector.
 * @return a x b, e.g. a torque from a lever arm and a force.
 */
template<typename A, typename B>
[[nodiscard]] constexpr Vec<ProductType<A, B>, 3> cross(const Vec<A, 3>& a, const Vec<B, 3>& b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/**
 * Computes the squared length; cheaper than `length` for comparisons.
 *
 * @param v The vector.
 * @return dot(v, v).
 */
template<typename T, std::size_t N>
[[nodiscard]] constexpr ProductType<T, T> lengthSquared(const Vec<T, N>& v) noexcept
{
    return dot(v, v);
}

/**
 * Computes the Euclidean length.
 *
 * @param v The vector.
 * @return |v|, in the component type.
 */
template<typename T, std::size_t N>
[[nodiscard]] T length(const Vec<T, N>& v) noexcept
{
    using std::sqrt;
    return sqrt(dot(v, v));
}

/**
 * Computes the unit vector in the same direction; a plain-number vector even for quantity components.
 *
 * @param v The vector; a zero vector gives non-finite components.
 * @return v / |v|.
 */
template<typename T, std::size_t N>
[[nodiscard]] Vec<ScalarOf<T>, N> normalized(const Vec<T, N>& v) noexcept
{
    const T n = length(v);
    Vec<ScalarOf<T>, N> out{};
    detail::forEachComponent<N>([&](auto i) {
        out.template get<i>() = static_cast<ScalarOf<T>>(v.template get<i>() / n);
    });
    return out;
}

template<typename T>
using Vec2 = Vec<T, 2>; ///< 2-vector of T.
template<typename T>
using Vec3 = Vec<T, 3>; ///< 3-vector of T.
template<typename T>
using Vec4 = Vec<T, 4>; ///< 4-vector of T.

using Vec2f = Vec2<float>;  ///< Float 2-vector.
using Vec3f = Vec3<float>;  ///< Float 3-vector.
using Vec4f = Vec4<float>;  ///< Float 4-vector.
using Vec2d = Vec2<double>; ///< Double 2-vector.
using Vec3d = Vec3<double>; ///< Double 3-vector.
using Vec4d = Vec4<double>; ///< Double 4-vector.

} // namespace sub0math
