#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "sub0math/geometry/aabb.hpp"
#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

template<std::size_t N>
using GridIndex = std::array<std::int32_t, N>; ///< Integer coordinates of a node or cell.

/**
 * An inclusive block of cells or nodes.
 *
 * @tparam N Number of dimensions.
 */
template<std::size_t N>
struct GridRange
{
    GridIndex<N> lo; ///< First index on each axis.
    GridIndex<N> hi; ///< Last index on each axis (inclusive).

    /** @return True when the range contains no index. */
    [[nodiscard]] constexpr bool empty() const noexcept
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            if (hi[i] < lo[i]) return true;
        }
        return false;
    }

    /** @return How many indices the range contains. */
    [[nodiscard]] constexpr std::int64_t count() const noexcept
    {
        if (empty()) return 0;
        std::int64_t n = 1;
        for (std::size_t i = 0; i < N; ++i) n *= static_cast<std::int64_t>(hi[i] - lo[i] + 1);
        return n;
    }

    friend constexpr bool operator==(const GridRange&, const GridRange&) = default;
};

/**
 * A regular grid of nodes `origin + index * spacing`. Cell i on an axis lies between nodes i and i + 1.
 *
 * Nodes are samples at exact positions: how a source image's pixels map onto them (pixel-is-point or
 * pixel-is-area) is decided when the data is imported, not here.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Number of dimensions.
 */
template<typename T, std::size_t N>
struct RegularGrid
{
    Point<T, N> origin;  ///< Position of node 0.
    T spacing;           ///< Distance between neighbouring nodes; positive.
    GridIndex<N> nodes;  ///< Nodes along each axis; at least 2 for there to be cells.

    /**
     * Finds a node's position.
     *
     * @param index The node; need not be inside the grid.
     * @return origin + index * spacing.
     */
    [[nodiscard]] constexpr Point<T, N> nodeAt(const GridIndex<N>& index) const noexcept
    {
        Point<T, N> p = origin;
        detail::forEachComponent<N>([&](auto i) {
            p.template get<i>() += spacing * static_cast<ScalarOf<T>>(index[i]);
        });
        return p;
    }

    /**
     * Finds the cell containing a point. A point on the far boundary belongs to the last cell.
     *
     * @param p The point.
     * @return The cell, or nothing when the point is outside the grid.
     */
    [[nodiscard]] std::optional<GridIndex<N>> cellOf(const Point<T, N>& p) const noexcept
    {
        GridIndex<N> cell{};
        for (std::size_t i = 0; i < N; ++i)
        {
            const auto u = static_cast<ScalarOf<T>>((p[i] - origin[i]) / spacing);
            const auto last = nodes[i] - 1;
            if (!(u >= ScalarOf<T>{0}) || u > static_cast<ScalarOf<T>>(last)) return std::nullopt;
            cell[i] = std::min(static_cast<std::int32_t>(std::floor(u)), last - 1);
        }
        return cell;
    }

    /**
     * Finds the cells that a box touches, clipped to the grid.
     *
     * @param box The box.
     * @return The cells; empty when the box misses the grid.
     */
    [[nodiscard]] GridRange<N> cellsOverlapping(const Aabb<T, N>& box) const noexcept
    {
        GridRange<N> range{};
        for (std::size_t i = 0; i < N; ++i)
        {
            const auto lo = static_cast<ScalarOf<T>>((box.lo[i] - origin[i]) / spacing);
            const auto hi = static_cast<ScalarOf<T>>((box.hi[i] - origin[i]) / spacing);
            const auto lastCell = static_cast<ScalarOf<T>>(nodes[i] - 2);
            range.lo[i] = static_cast<std::int32_t>(std::floor(std::clamp(lo, ScalarOf<T>{0}, lastCell + 1)));
            range.hi[i] = static_cast<std::int32_t>(std::floor(std::clamp(hi, ScalarOf<T>{-1}, lastCell)));
            if (hi < ScalarOf<T>{0} || lo > lastCell + 1) range.hi[i] = range.lo[i] - 1; // misses on this axis
        }
        return range;
    }

    friend constexpr bool operator==(const RegularGrid&, const RegularGrid&) = default;
};

} // namespace sub0math
