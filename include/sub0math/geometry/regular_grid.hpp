#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

namespace detail
{

// The canonical empty range: lo 0 and hi -1 on every axis.
template<std::size_t N>
[[nodiscard]] constexpr GridRange<N> emptyGridRange() noexcept
{
    GridRange<N> range{};
    range.lo.fill(0);
    range.hi.fill(-1);
    return range;
}

} // namespace detail

/**
 * The answer to "which cell owns this point": an index and whether there is one. A plain aggregate with a
 * status rather than `std::optional`, so a hot loop keeps it in registers.
 *
 * @tparam N Number of dimensions.
 */
template<std::size_t N>
struct GridCell
{
    GridIndex<N> index{}; ///< The cell; zeros when not known.
    bool known = false;   ///< False when the point is outside the grid or not finite.

    /** @return True when a cell was found. */
    [[nodiscard]] explicit constexpr operator bool() const noexcept
    {
        return known;
    }

    friend constexpr bool operator==(const GridCell&, const GridCell&) = default;
};

/**
 * A regular grid of nodes `origin + index * spacing`. Cell i on an axis lies between nodes i and i + 1.
 *
 * Nodes are samples at exact positions: how a source image's pixels map onto them (pixel-is-point or
 * pixel-is-area) is decided when the data is imported, not here.
 *
 * Two operations map space to cells, with different boundary rules on purpose:
 * - **Ownership** (`cellOf`): each point belongs to exactly one cell. Cells are half-open, `[lo, hi)`, and
 *   the grid's far edge belongs to the last cell.
 * - **Coverage** (`cellsTouching`): the cells a box touches. A box that touches a cell's boundary
 *   includes that cell, so a closed box on a node includes every cell incident to it.
 *
 * Neither converts a non-finite or out-of-range number to an integer: such input reports "not known" or
 * an empty range.
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
     * Finds the cell that owns a point. Cells are half-open: a point on an interior node belongs to the
     * cell above it, and a point on the far boundary belongs to the last cell.
     *
     * @param p The point.
     * @return The cell, or `known == false` when the point is outside the grid or not finite.
     */
    [[nodiscard]] GridCell<N> cellOf(const Point<T, N>& p) const noexcept
    {
        using S = ScalarOf<T>;
        GridCell<N> cell{};
        for (std::size_t i = 0; i < N; ++i)
        {
            const auto u = static_cast<S>((p[i] - origin[i]) / spacing);
            const auto last = nodes[i] - 1;
            // NaN fails the first test; infinity and huge values fail the second, before any conversion.
            // PERF: bitwise-or of the tests faster than short-circuit branches.
            if (!(u >= S{0}) | (u > static_cast<S>(last)) | (last < 1)) return {};
            cell.index[i] = std::min(static_cast<std::int32_t>(std::floor(u)), last - 1);
        }
        cell.known = true;
        return cell;
    }

    /**
     * Finds the cells that a closed box touches, clipped to the grid. A box edge exactly on a cell
     * boundary includes the cells on both sides, and a box that only touches the grid's edge includes
     * the cell it touches. On each axis the cells run from `ceil(lo / spacing) - 1` to
     * `floor(hi / spacing)` (grid units), limited to the grid.
     *
     * @param box The box; both corners must be finite.
     * @return The cells; empty when the box misses the grid or a corner is not finite.
     */
    [[nodiscard]] GridRange<N> cellsTouching(const Aabb<T, N>& box) const noexcept
    {
        using S = ScalarOf<T>;
        GridRange<N> range{};
        bool finite = true;
        for (std::size_t i = 0; i < N; ++i)
        {
            const auto lo = static_cast<S>((box.lo[i] - origin[i]) / spacing);
            const auto hi = static_cast<S>((box.hi[i] - origin[i]) / spacing);
            const bool loFinite = std::isfinite(lo); // named, so Clang's bitwise-on-calls warning stays quiet
            const bool hiFinite = std::isfinite(hi);
            finite &= loFinite & hiFinite;
            const auto cells = static_cast<S>(nodes[i] - 1);
            // Limit while still floating point, so the conversion is always in range. A lower index above
            // the last cell, or an upper one below the first, makes the axis empty. With the number as the
            // first argument std::min and std::max also turn NaN into a limit, so even a non-finite corner
            // converts safely before it is discarded below.
            range.lo[i] = static_cast<std::int32_t>(std::max(S{0}, std::min(std::ceil(lo) - S{1}, cells)));
            range.hi[i] = static_cast<std::int32_t>(std::max(S{-1}, std::min(std::floor(hi), cells - S{1})));
        }
        // PERF: one deferred finite test faster than a branch per axis.
        if (!finite) return detail::emptyGridRange<N>();
        return range;
    }

    friend constexpr bool operator==(const RegularGrid&, const RegularGrid&) = default;
};

} // namespace sub0math
