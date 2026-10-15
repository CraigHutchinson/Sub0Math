#pragma once

#include <array>
#include <cstddef>

#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/**
 * A fixed-size matrix of plain numbers, stored column-major like GPU and Filament matrices.
 *
 * Transforms (rotations, scales) are dimensionless, so matrices hold plain numbers; multiplying a
 * matrix by a vector of quantities keeps the quantities (`rotation * displacement` is a displacement).
 * `m(row, column)` reads an element; `m.columns[c]` is a column.
 *
 * @tparam T Floating-point element type.
 * @tparam R Rows.
 * @tparam C Columns.
 */
template<Real T, std::size_t R, std::size_t C>
struct Mat
{
    std::array<Vec<T, R>, C> columns{}; ///< Columns, contiguous.

    /** @return The identity matrix (square only). */
    [[nodiscard]] static constexpr Mat identity() noexcept
        requires(R == C)
    {
        Mat m{};
        for (std::size_t i = 0; i < R; ++i)
        {
            m.columns[i][i] = T{1};
        }
        return m;
    }

    /**
     * Reads an element.
     *
     * @param row Row index.
     * @param column Column index.
     * @return The element.
     */
    [[nodiscard]] constexpr const T& operator()(std::size_t row, std::size_t column) const noexcept
    {
        return columns[column][row];
    }

    /**
     * Accesses an element.
     *
     * @param row Row index.
     * @param column Column index.
     * @return The element.
     */
    [[nodiscard]] constexpr T& operator()(std::size_t row, std::size_t column) noexcept
    {
        return columns[column][row];
    }

    friend constexpr bool operator==(const Mat&, const Mat&) = default;
};

/**
 * Transposes a matrix.
 *
 * @param m The matrix.
 * @return Its transpose.
 */
template<Real T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr Mat<T, C, R> transpose(const Mat<T, R, C>& m) noexcept
{
    Mat<T, C, R> t{};
    for (std::size_t r = 0; r < R; ++r)
    {
        for (std::size_t c = 0; c < C; ++c)
        {
            t(c, r) = m(r, c);
        }
    }
    return t;
}

/**
 * Multiplies a matrix by a vector.
 *
 * @param m The matrix.
 * @param v A vector of numbers or quantities.
 * @return m v, with v's component type.
 */
template<Real T, std::size_t R, std::size_t C, typename U>
[[nodiscard]] constexpr Vec<ProductType<T, U>, R> operator*(const Mat<T, R, C>& m, const Vec<U, C>& v) noexcept
{
    // Sum of columns scaled by v's components: contiguous, no runtime component index, and starting from
    // the first column rather than zero (0.0 + x is not foldable under IEEE rules).
    Vec<ProductType<T, U>, R> out = m.columns[0] * v.template get<0>();
    detail::forEachComponent<C - 1>([&](auto c) { out += m.columns[c + 1] * v.template get<c + 1>(); });
    return out;
}

/**
 * Multiplies matrices.
 *
 * @param a Left matrix.
 * @param b Right matrix.
 * @return a b.
 */
template<Real T, std::size_t R, std::size_t K, std::size_t C>
[[nodiscard]] constexpr Mat<T, R, C> operator*(const Mat<T, R, K>& a, const Mat<T, K, C>& b) noexcept
{
    Mat<T, R, C> out{};
    for (std::size_t c = 0; c < C; ++c)
    {
        out.columns[c] = a * b.columns[c];
    }
    return out;
}

template<Real T>
using Mat3 = Mat<T, 3, 3>; ///< 3x3 matrix.
template<Real T>
using Mat4 = Mat<T, 4, 4>; ///< 4x4 matrix.

using Mat3f = Mat3<float>;  ///< Float 3x3.
using Mat4f = Mat4<float>;  ///< Float 4x4.
using Mat3d = Mat3<double>; ///< Double 3x3.
using Mat4d = Mat4<double>; ///< Double 4x4.

} // namespace sub0math
