#pragma once

#include <concepts>

namespace sub0math
{

/**
 * Floating-point type used when code does not choose one.
 *
 * Float is enough for rendering, firmware (single-precision FPUs) and most interfaces. Choose double
 * explicitly, through the `sub0math::f64` aliases, for simulation state, accumulated time and world
 * positions, where float's 24-bit significand loses small increments.
 */
using DefaultReal = float;

/**
 * A floating-point representation for quantities, vectors and transforms.
 *
 * @tparam T Candidate type.
 */
template<typename T>
concept Real = std::floating_point<T>;

/**
 * Names the plain number a value type is built on; wrapper types (quantities) specialise it.
 *
 * @tparam T The value type.
 */
template<typename T>
struct ScalarTraits
{
    using Scalar = T; ///< For a plain number, the number type itself.
};

/**
 * The plain number type underlying T.
 *
 * @tparam T The value type.
 */
template<typename T>
using ScalarOf = typename ScalarTraits<T>::Scalar;

} // namespace sub0math
