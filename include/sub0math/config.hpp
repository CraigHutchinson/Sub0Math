#pragma once

// Forces inlining of small vocabulary functions on hot paths. Layered abstractions (a consumer's helper
// calling closestPoint calling dot) can exhaust a compiler's inlining budget even when every layer is
// tiny; measured, Sub0Physics' clearance query lost 60% when GCC stopped inlining one such helper.
// Use it only on short functions that sit inside callers' inner loops.
#if defined(_MSC_VER) && !defined(__clang__)
#define SUB0MATH_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define SUB0MATH_FORCE_INLINE inline __attribute__((always_inline))
#else
#define SUB0MATH_FORCE_INLINE inline
#endif
