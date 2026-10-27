#pragma once

#include <cstddef>
#include <cstdint>

#include "sub0math/geometry/point.hpp"
#include "sub0math/linear/vec.hpp"
#include "sub0math/number/real.hpp"

namespace sub0math
{

/** Which kind of surface feature a witness point lies on. */
enum class WitnessKind : std::uint8_t
{
    Vertex, ///< A vertex; `index` is the vertex index.
    Edge,   ///< The interior of an edge; `index` is its first vertex.
    Face    ///< The interior of a face (a triangle or box face); `index` is the face index.
};

/**
 * The surface feature that answers a nearest-point or hit query: where it is, which way it faces and what
 * it is. Queries return the feature itself and never average the normals of several: at a corner the
 * witness is the vertex with the true approach direction, not a blend of its faces.
 *
 * @tparam T Coordinate type: a floating-point number or a length quantity.
 * @tparam N Dimensions of the space: 2 for a profile or polygon, 3 for a surface.
 */
template<typename T, std::size_t N>
struct Witness
{
    Point<T, N> point;           ///< The point on the surface.
    Vec<ScalarOf<T>, N> normal;  ///< Unit direction from the point towards the query.
    WitnessKind kind;            ///< Feature the point lies on.
    std::size_t index;           ///< Index of that feature in its owner.
};

} // namespace sub0math
