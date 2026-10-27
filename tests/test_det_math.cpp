#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <doctest.h>
#include <limits>

#include "sub0math/number.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;

namespace
{

constexpr std::uint64_t bitsOf(double x) noexcept
{
    return std::bit_cast<std::uint64_t>(x);
}

// Mirrors tools/det_math_reference.js exactly: SplitMix64 on the index and exactly-rounded arithmetic only.
constexpr std::uint64_t splitmix64(std::uint64_t i) noexcept
{
    std::uint64_t z = i * 0x9E3779B97F4A7C15ULL + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr double HalfPi = 1.5707963267948966;
constexpr std::uint64_t ReferenceCount = 60000;

double referenceInput(std::uint64_t i)
{
    const std::uint64_t u = splitmix64(i);
    const double unit = static_cast<double>(u >> 11) * 0x1p-53;
    const double signedUnit = unit * 2 - 1;
    switch (i % 6)
    {
    case 0:
        return signedUnit * 4;
    case 1:
        return signedUnit * 1000;
    case 2:
        return signedUnit * 1048576;
    case 3:
    {
        const std::uint64_t e = splitmix64(i + 1000003) % 2047;
        return std::bit_cast<double>((u & 0x800FFFFFFFFFFFFFULL) | (e << 52));
    }
    case 4:
        return std::trunc(signedUnit * 1048576) * HalfPi;
    default:
    {
        const double k = std::trunc(signedUnit * 1099511627776.0);
        const std::uint64_t step = (u & 3) - 1; // -1, 0, +1 ulp (modular)
        return std::bit_cast<double>(bitsOf(k * HalfPi) + step);
    }
    }
}

struct Expected
{
    std::uint64_t x;
    std::uint64_t sin;
    std::uint64_t cos;
};

// From V8 (Node 24) Math.sin and Math.cos, via tools/det_math_reference.js.
constexpr Expected V8Table[] = {
    {0x0000000000000000, 0x0000000000000000, 0x3ff0000000000000},
    {0x8000000000000000, 0x8000000000000000, 0x3ff0000000000000},
    {0x0000000000000001, 0x0000000000000001, 0x3ff0000000000000},
    {0x8000000000000001, 0x8000000000000001, 0x3ff0000000000000},
    {0x01a56e1fc2f8f359, 0x01a56e1fc2f8f359, 0x3ff0000000000000},
    {0x3e40000000000000, 0x3e40000000000000, 0x3ff0000000000000},
    {0x3e50000000000000, 0x3e50000000000000, 0x3fefffffffffffff},
    {0x3fb999999999999a, 0x3fb98eaecb8bcb2c, 0x3fefd712f9a817c0},
    {0x3fe0000000000000, 0x3fdeaee8744b05f0, 0x3fec1528065b7d50},
    {0x3fe921fb54442d18, 0x3fe6a09e667f3bcc, 0x3fe6a09e667f3bcd},
    {0x3fe921fb54442d19, 0x3fe6a09e667f3bcd, 0x3fe6a09e667f3bcc},
    {0x3ff0000000000000, 0x3feaed548f090cee, 0x3fe14a280fb5068c},
    {0x3ff921fb54442d18, 0x3ff0000000000000, 0x3c91a62633145c07},
    {0x3ff921fb54442d19, 0x3ff0000000000000, 0xbca72cece675d1fd},
    {0x4000000000000000, 0x3fed18f6ead1b446, 0xbfdaa22657537205},
    {0x4002d97c7f3321d2, 0x3fe6a09e667f3bcd, 0xbfe6a09e667f3bcc},
    {0x4008000000000000, 0x3fc210386db6d55b, 0xbfefae04be85e5d2},
    {0x400921fb54442d18, 0x3ca1a62633145c07, 0xbff0000000000000},
    {0x4010000000000000, 0xbfe837b9dddc1eae, 0xbfe4eaa606db24c1},
    {0x4012d97c7f3321d2, 0xbff0000000000000, 0xbcaa79394c9e8a0a},
    {0x401921fb54442d18, 0xbcb1a62633145c07, 0x3ff0000000000000},
    {0x4024000000000000, 0xbfe1689ef5f34f52, 0xbfead9ac890c6b1f},
    {0x4059000000000000, 0xbfe03425b78c4db8, 0x3feb981dbf665fdf},
    {0x40f86a0000000000, 0x3fa24daa9c527e96, 0xbfeffac3841b3da7},
    {0x412921fb33333333, 0xbfb0858be515492b, 0x3fefeeebfee47670},
    {0x412921fc00000000, 0x3fd51102efad6266, 0x3fee377c09c4cce7},
    {0x412e848000000000, 0xbfd6664b2568d867, 0x3fedf9df9906d32c},
    {0x4202a05f20000000, 0xbfdf334c7896a4e3, 0x3febf098901c931a},
    {0x430c6bf526340000, 0x3feb76f88136ceba, 0xbfe06c154609d33e},
    {0x4340000000000000, 0xbfeb2a66c8f35586, 0xbfe0e9918bb35aac},
    {0x4480f0cf064dd592, 0xbfeb453ab76bf397, 0x3fe0be2cef01c8f4},
    {0x54b249ad2594c37d, 0xbfd85c5e5b929359, 0x3fed9757496841f5},
    {0x7e37e43c8800759c, 0xbfea2c16b010e385, 0xbfe2699022adc4c1},
    {0x7fefffffffffffff, 0x3f7452fc98b34e97, 0xbfefffe62ecfab75},
    {0xffefffffffffffff, 0xbf7452fc98b34e97, 0xbfefffe62ecfab75},
    {0xbff0000000000000, 0xbfeaed548f090cee, 0x3fe14a280fb5068c},
    {0xc00921fb54442d18, 0xbca1a62633145c07, 0xbff0000000000000},
    {0xc480f0cf064dd592, 0x3feb453ab76bf397, 0x3fe0be2cef01c8f4},
    {0x4630000000000000, 0xbfebe8ed97ac1f59, 0x3fdf4eb3ff66e36d},
};

constexpr std::uint64_t V8Digest = 0x684e97226403f795ULL;

constexpr std::uint64_t fnv(std::uint64_t h, std::uint64_t b) noexcept
{
    return (h ^ b) * 0x100000001B3ULL;
}

// Distance in units in the last place between a result and a long-double reference.
[[maybe_unused]] double ulpError(double got, long double exact)
{
    const double mag = std::fabs(got);
    const long double ulp = static_cast<long double>(std::nextafter(mag, std::numeric_limits<double>::infinity())) -
                            static_cast<long double>(mag);
    const long double diff = static_cast<long double>(got) - exact;
    return static_cast<double>((diff < 0 ? -diff : diff) / ulp);
}

} // namespace

// Usable at compile time, so the value cannot come from the C library.
static_assert(det::sin(0.0) == 0.0);
static_assert(det::cos(0.0) == 1.0);
static_assert(bitsOf(det::sin(1.0)) == 0x3feaed548f090ceeULL);
static_assert(bitsOf(det::cos(1.0)) == 0x3fe14a280fb5068cULL);
static_assert(bitsOf(det::sin(1e22)) == 0xbfeb453ab76bf397ULL); // Payne-Hanek path
static_assert(bitsOf(det::sincos(100.0).cos) == 0x3feb981dbf665fdfULL);
static_assert(noexcept(det::sin(1.0)) && noexcept(det::sincos(1.0f)));

TEST_CASE("det sin/cos equal V8 on pinned edge values")
{
    for (const Expected& e : V8Table)
    {
        const double x = std::bit_cast<double>(e.x);
        CAPTURE(x);
        CHECK(bitsOf(det::sin(x)) == e.sin);
        CHECK(bitsOf(det::cos(x)) == e.cos);
        const auto sc = det::sincos(x);
        CHECK(bitsOf(sc.sin) == e.sin);
        CHECK(bitsOf(sc.cos) == e.cos);
    }
}

TEST_CASE("det sin/cos are bit-identical to V8 over 60000 generated inputs")
{
    std::uint64_t digest = 0xCBF29CE484222325ULL;
    std::uint64_t mismatches = 0;
    for (std::uint64_t i = 0; i < ReferenceCount; ++i)
    {
        const double x = referenceInput(i);
        const auto sc = det::sincos(x);
        if (bitsOf(sc.sin) != bitsOf(det::sin(x)) || bitsOf(sc.cos) != bitsOf(det::cos(x)))
        {
            ++mismatches;
        }
        digest = fnv(digest, bitsOf(sc.sin));
        digest = fnv(digest, bitsOf(sc.cos));
    }
    CHECK(mismatches == 0);
    CHECK(digest == V8Digest);
}

TEST_CASE("det sin/cos non-finite inputs and symmetry")
{
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(std::isnan(det::sin(inf)));
    CHECK(std::isnan(det::cos(-inf)));
    CHECK(std::isnan(det::sin(std::numeric_limits<double>::quiet_NaN())));
    CHECK(std::isnan(det::sincos(inf).sin));
    CHECK(std::signbit(det::sin(-0.0)));
    for (std::uint64_t i = 0; i < 2000; ++i)
    {
        const double x = referenceInput(i);
        CHECK(bitsOf(det::sin(-x)) == (bitsOf(det::sin(x)) ^ (1ULL << 63)));
        CHECK(bitsOf(det::cos(-x)) == bitsOf(det::cos(x)));
    }
}

TEST_CASE("det sin/cos stay under one ulp of a long-double reference")
{
    // The x87 reference reduces with a 66-bit pi, so only |x| <= 4 away from the zero crossings is trustworthy;
    // the full-range figures (0.79 ulp worst, 60000 inputs, mpmath at 200 bits) come from tools/det_math_ulp.py.
    if constexpr (std::numeric_limits<long double>::digits > 53)
    {
        double worstSin = 0;
        double worstCos = 0;
        for (std::uint64_t i = 0; i < ReferenceCount; i += 6)
        {
            const double x = referenceInput(i);
            const long double s = std::sin(static_cast<long double>(x));
            const long double c = std::cos(static_cast<long double>(x));
            if (std::fabs(s) > 0.01L)
            {
                worstSin = std::max(worstSin, ulpError(det::sin(x), s));
            }
            if (std::fabs(c) > 0.01L)
            {
                worstCos = std::max(worstCos, ulpError(det::cos(x), c));
            }
        }
        MESSAGE("max error (ulp): sin ", worstSin, ", cos ", worstCos);
        CHECK(worstSin < 1.0);
        CHECK(worstCos < 1.0);
    }
}

TEST_CASE("det float overloads and the Angle helpers use the deterministic functions")
{
    CHECK(det::sin(1.0f) == static_cast<float>(det::sin(1.0)));
    CHECK(det::cos(1.0f) == static_cast<float>(det::cos(1.0)));
    const auto sc = det::sincos(2.5f);
    CHECK(sc.sin == det::sin(2.5f));
    CHECK(sc.cos == det::cos(2.5f));
    const f64::Angle a = f64::Angle::fromSi(100.0);
    CHECK(bitsOf(sin(a)) == bitsOf(det::sin(100.0)));
    CHECK(bitsOf(cos(a)) == bitsOf(det::cos(100.0)));
    const auto both = sincos(a);
    CHECK(bitsOf(both.sin) == bitsOf(det::sin(100.0)));
    CHECK(bitsOf(both.cos) == bitsOf(det::cos(100.0)));
}
