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

// ---- exp, log, pow ----

namespace
{

double expInput(std::uint64_t i)
{
    const std::uint64_t u = splitmix64(i + 7000000);
    const double signedUnit = static_cast<double>(u >> 11) * 0x1p-53 * 2 - 1;
    switch (i % 4)
    {
    case 0:
        return signedUnit * 745;
    case 1:
        return signedUnit * 2;
    case 2:
        return signedUnit * 1e-3;
    default:
    {
        const std::uint64_t e = 1023 - 40 + splitmix64(i + 9000000) % 52;
        return std::bit_cast<double>((u & 0x800FFFFFFFFFFFFFULL) | (e << 52));
    }
    }
}

double logInput(std::uint64_t i)
{
    const std::uint64_t u = splitmix64(i + 11000000);
    const double unit = static_cast<double>(u >> 11) * 0x1p-53;
    switch (i % 4)
    {
    case 0:
        return std::bit_cast<double>(u & 0x7FFFFFFFFFFFFFFFULL);
    case 1:
        return unit * 100;
    case 2:
        return 1 + (unit * 2 - 1) * 0x1p-18;
    default:
        return unit * 2 + 0.5;
    }
}

std::uint64_t canonical(double v)
{
    return std::isnan(v) ? 0x7ff8000000000000ULL : bitsOf(v);
}

constexpr std::uint64_t ExpLogCount = 40000;
constexpr std::uint64_t V8ExpDigest = 0x8a4511426ccf76c1ULL;
constexpr std::uint64_t V8LogDigest = 0x52d33eaff88f3165ULL;

// Node 24 Math.exp and Math.log: {x, exp(x), log(x)}.
constexpr Expected V8ExpLog[] = {
    {0x0000000000000000, 0x3ff0000000000000, 0xfff0000000000000},
    {0x8000000000000000, 0x3ff0000000000000, 0xfff0000000000000},
    {0x3ff0000000000000, 0x4005bf0a8b145769, 0x0000000000000000},
    {0xbff0000000000000, 0x3fd78b56362cef38, 0x7ff8000000000000},
    {0x3fe0000000000000, 0x3ffa61298e1e069c, 0xbfe62e42fefa39ef},
    {0x4000000000000000, 0x401d8e64b8d4ddae, 0x3fe62e42fefa39ef},
    {0x4024000000000000, 0x40d5829dcf950560, 0x40026bb1bbb55516},
    {0x4059000000000000, 0x48f3494a9b171bf5, 0x40126bb1bbb55516},
    {0x4086280000000000, 0x7fdd422d2be5dc9b, 0x401a416357d87f55},
    {0x40862e3d70a3d70a, 0x7fefe9ce5c4c52b4, 0x401a4283947ad848},
    {0x4086300000000000, 0x7ff0000000000000, 0x401a42d4d1ff8154},
    {0xc087480000000000, 0x0000000000000001, 0x7ff8000000000000},
    {0xc08749999999999a, 0x0000000000000000, 0x7ff8000000000000},
    {0xc087500000000000, 0x0000000000000000, 0x7ff8000000000000},
    {0x3ddb7cdfd9d7bdbb, 0x3ff000000006df38, 0xc037069e2aa2aa5b},
    {0x0000000000000001, 0x3ff0000000000000, 0xc0874385446d71c3},
    {0x7ff0000000000000, 0x7ff0000000000000, 0x7ff0000000000000},
    {0xfff0000000000000, 0x0000000000000000, 0x7ff8000000000000},
    {0x7ff8000000000000, 0x7ff8000000000000, 0x7ff8000000000000},
};

} // namespace

static_assert(bitsOf(det::exp(1.0)) == 0x4005bf0a8b145769ULL);
static_assert(bitsOf(det::log(2.0)) == 0x3fe62e42fefa39efULL);
static_assert(det::pow(3.0, 2.0) == 9.0);

TEST_CASE("det exp/log equal V8 on pinned values and over 40000 generated inputs each")
{
    for (const Expected& e : V8ExpLog)
    {
        const double x = std::bit_cast<double>(e.x);
        CAPTURE(x);
        CHECK(canonical(det::exp(x)) == e.sin);
        CHECK(canonical(det::log(x)) == e.cos);
    }
    std::uint64_t he = 0xCBF29CE484222325ULL;
    std::uint64_t hl = 0xCBF29CE484222325ULL;
    for (std::uint64_t i = 0; i < ExpLogCount; ++i)
    {
        he = fnv(he, canonical(det::exp(expInput(i))));
        hl = fnv(hl, canonical(det::log(logInput(i))));
    }
    CHECK(he == V8ExpDigest);
    CHECK(hl == V8LogDigest);
}

TEST_CASE("det pow: exact for whole exponents, close to the C library otherwise")
{
    CHECK(det::pow(2.0, 10.0) == 1024.0);
    CHECK(det::pow(7.0, 0.0) == 1.0);
    CHECK(det::pow(0.5, 2.0) == 0.25);
    CHECK(det::pow(2.0, -3.0) == 0.125);
    CHECK(det::pow(-2.0, 3.0) == -8.0);
    CHECK(det::pow(0.0, 2.5) == 0.0);
    CHECK(std::isinf(det::pow(0.0, -2.5)));
    CHECK(std::isnan(det::pow(-2.0, 0.5)));
    CHECK(std::isnan(det::pow(std::numeric_limits<double>::quiet_NaN(), 2.0)));
    double worst = 0;
    for (std::uint64_t i = 0; i < 4000; ++i)
    {
        const double x = 0.001 + 100.0 * static_cast<double>(splitmix64(i) >> 11) * 0x1p-53;
        const double y = -3.0 + 6.0 * static_cast<double>(splitmix64(i + 5) >> 11) * 0x1p-53;
        const double reference = static_cast<double>(std::pow(static_cast<long double>(x), static_cast<long double>(y)));
        worst = std::max(worst, std::fabs(det::pow(x, y) - reference) / std::fabs(reference));
    }
    MESSAGE("pow worst relative error vs long double: ", worst);
    CHECK(worst < 5e-15);
}

// ---- tan, atan, atan2 ----

namespace
{

double tanInput(std::uint64_t i)
{
    const std::uint64_t u = splitmix64(i + 13000000);
    const double unit = static_cast<double>(u >> 11) * 0x1p-53;
    switch (i % 4)
    {
    case 0:
        return (unit * 2 - 1) * 4;
    case 1:
        return (unit * 2 - 1) * 1000;
    case 2: // within an ulp of an odd multiple of pi/2, where tan is huge
    {
        const double x = (std::trunc((unit * 2 - 1) * 1000) * 2 + 1) * HalfPi;
        return std::bit_cast<double>(bitsOf(x) + ((u & 3) - 1));
    }
    default:
        return referenceInput(i);
    }
}

double atanInput(std::uint64_t i)
{
    const std::uint64_t u = splitmix64(i + 15000000);
    const double unit = static_cast<double>(u >> 11) * 0x1p-53;
    switch (i % 5)
    {
    case 0:
        return (unit * 2 - 1) * 3;
    case 1:
        return (unit * 2 - 1) * 1e6;
    case 2:
        return std::bit_cast<double>(u & 0x7FFFFFFFFFFFFFFFULL);
    case 3:
        return std::bit_cast<double>(u);
    default:
        return (unit * 2 - 1) * std::ldexp(1.0, static_cast<int>(splitmix64(i + 17000000) % 140) - 70);
    }
}

struct Atan2Input
{
    double y;
    double x;
};

Atan2Input atan2Input(std::uint64_t i)
{
    const std::uint64_t u = splitmix64(i + 19000000);
    const std::uint64_t v = splitmix64(i + 21000000);
    const double ux = static_cast<double>(u >> 11) * 0x1p-53;
    const double uy = static_cast<double>(v >> 11) * 0x1p-53;
    switch (i % 5)
    {
    case 0:
        return {(uy * 2 - 1) * 10, (ux * 2 - 1) * 10};
    case 1:
        return {(uy * 2 - 1) * std::ldexp(1.0, static_cast<int>(splitmix64(i + 23000000) % 200) - 100),
                (ux * 2 - 1) * std::ldexp(1.0, static_cast<int>(splitmix64(i + 25000000) % 200) - 100)};
    case 2:
        return {std::bit_cast<double>(v), std::bit_cast<double>(u)};
    case 3:
        return {std::bit_cast<double>((v & 0x800FFFFFFFFFFFFFULL) | ((u % 40 + 1003) << 52)),
                std::bit_cast<double>(u & 0x800FFFFFFFFFFFFFULL)};
    default:
        return {(uy * 2 - 1) * 4, (ux < 0.5 ? -1.0 : 1.0) * std::ldexp(1.0, -static_cast<int>(u % 30))};
    }
}

constexpr std::uint64_t TanCount = 40000;
constexpr std::uint64_t AtanCount = 40000;
constexpr std::uint64_t Atan2Count = 50000;
constexpr std::uint64_t V8TanDigest = 0x60f82ca28c826b12ULL;
constexpr std::uint64_t V8AtanDigest = 0x73ae1dfb71b85987ULL;
constexpr std::uint64_t V8Atan2Digest = 0x0222c7e45a061ba1ULL;

// Node 24 Math.atan2 over the edge values 0, -0, 5e-324, -5e-324, 1e-300, 1, -1, 2, 0.5, 1e300, +-inf, NaN:
// {y, x, atan2(y, x)}.
constexpr Expected V8Atan2[] = {
    {0x0000000000000000, 0x0000000000000000, 0x0000000000000000}, // 0, 0
    {0x0000000000000000, 0x8000000000000000, 0x400921fb54442d18}, // 0, -0
    {0x0000000000000000, 0x0000000000000001, 0x0000000000000000}, // 0, 5e-324
    {0x0000000000000000, 0x8000000000000001, 0x400921fb54442d18}, // 0, -5e-324
    {0x0000000000000000, 0x01a56e1fc2f8f359, 0x0000000000000000}, // 0, 1e-300
    {0x0000000000000000, 0x3ff0000000000000, 0x0000000000000000}, // 0, 1
    {0x0000000000000000, 0xbff0000000000000, 0x400921fb54442d18}, // 0, -1
    {0x0000000000000000, 0x4000000000000000, 0x0000000000000000}, // 0, 2
    {0x0000000000000000, 0x3fe0000000000000, 0x0000000000000000}, // 0, 0.5
    {0x0000000000000000, 0x7e37e43c8800759c, 0x0000000000000000}, // 0, 1e+300
    {0x0000000000000000, 0x7ff0000000000000, 0x0000000000000000}, // 0, Infinity
    {0x0000000000000000, 0xfff0000000000000, 0x400921fb54442d18}, // 0, -Infinity
    {0x0000000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // 0, NaN
    {0x8000000000000000, 0x0000000000000000, 0x8000000000000000}, // -0, 0
    {0x8000000000000000, 0x8000000000000000, 0xc00921fb54442d18}, // -0, -0
    {0x8000000000000000, 0x0000000000000001, 0x8000000000000000}, // -0, 5e-324
    {0x8000000000000000, 0x8000000000000001, 0xc00921fb54442d18}, // -0, -5e-324
    {0x8000000000000000, 0x01a56e1fc2f8f359, 0x8000000000000000}, // -0, 1e-300
    {0x8000000000000000, 0x3ff0000000000000, 0x8000000000000000}, // -0, 1
    {0x8000000000000000, 0xbff0000000000000, 0xc00921fb54442d18}, // -0, -1
    {0x8000000000000000, 0x4000000000000000, 0x8000000000000000}, // -0, 2
    {0x8000000000000000, 0x3fe0000000000000, 0x8000000000000000}, // -0, 0.5
    {0x8000000000000000, 0x7e37e43c8800759c, 0x8000000000000000}, // -0, 1e+300
    {0x8000000000000000, 0x7ff0000000000000, 0x8000000000000000}, // -0, Infinity
    {0x8000000000000000, 0xfff0000000000000, 0xc00921fb54442d18}, // -0, -Infinity
    {0x8000000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // -0, NaN
    {0x0000000000000001, 0x0000000000000000, 0x3ff921fb54442d18}, // 5e-324, 0
    {0x0000000000000001, 0x8000000000000000, 0x3ff921fb54442d18}, // 5e-324, -0
    {0x0000000000000001, 0x0000000000000001, 0x3fe921fb54442d18}, // 5e-324, 5e-324
    {0x0000000000000001, 0x8000000000000001, 0x4002d97c7f3321d2}, // 5e-324, -5e-324
    {0x0000000000000001, 0x01a56e1fc2f8f359, 0x3b17e43c8800759b}, // 5e-324, 1e-300
    {0x0000000000000001, 0x3ff0000000000000, 0x0000000000000001}, // 5e-324, 1
    {0x0000000000000001, 0xbff0000000000000, 0x400921fb54442d18}, // 5e-324, -1
    {0x0000000000000001, 0x4000000000000000, 0x0000000000000000}, // 5e-324, 2
    {0x0000000000000001, 0x3fe0000000000000, 0x0000000000000002}, // 5e-324, 0.5
    {0x0000000000000001, 0x7e37e43c8800759c, 0x0000000000000000}, // 5e-324, 1e+300
    {0x0000000000000001, 0x7ff0000000000000, 0x0000000000000000}, // 5e-324, Infinity
    {0x0000000000000001, 0xfff0000000000000, 0x400921fb54442d18}, // 5e-324, -Infinity
    {0x0000000000000001, 0x7ff8000000000000, 0x7ff8000000000000}, // 5e-324, NaN
    {0x8000000000000001, 0x0000000000000000, 0xbff921fb54442d18}, // -5e-324, 0
    {0x8000000000000001, 0x8000000000000000, 0xbff921fb54442d18}, // -5e-324, -0
    {0x8000000000000001, 0x0000000000000001, 0xbfe921fb54442d18}, // -5e-324, 5e-324
    {0x8000000000000001, 0x8000000000000001, 0xc002d97c7f3321d2}, // -5e-324, -5e-324
    {0x8000000000000001, 0x01a56e1fc2f8f359, 0xbb17e43c8800759b}, // -5e-324, 1e-300
    {0x8000000000000001, 0x3ff0000000000000, 0x8000000000000001}, // -5e-324, 1
    {0x8000000000000001, 0xbff0000000000000, 0xc00921fb54442d18}, // -5e-324, -1
    {0x8000000000000001, 0x4000000000000000, 0x8000000000000000}, // -5e-324, 2
    {0x8000000000000001, 0x3fe0000000000000, 0x8000000000000002}, // -5e-324, 0.5
    {0x8000000000000001, 0x7e37e43c8800759c, 0x8000000000000000}, // -5e-324, 1e+300
    {0x8000000000000001, 0x7ff0000000000000, 0x8000000000000000}, // -5e-324, Infinity
    {0x8000000000000001, 0xfff0000000000000, 0xc00921fb54442d18}, // -5e-324, -Infinity
    {0x8000000000000001, 0x7ff8000000000000, 0x7ff8000000000000}, // -5e-324, NaN
    {0x01a56e1fc2f8f359, 0x0000000000000000, 0x3ff921fb54442d18}, // 1e-300, 0
    {0x01a56e1fc2f8f359, 0x8000000000000000, 0x3ff921fb54442d18}, // 1e-300, -0
    {0x01a56e1fc2f8f359, 0x0000000000000001, 0x3ff921fb54442d18}, // 1e-300, 5e-324
    {0x01a56e1fc2f8f359, 0x8000000000000001, 0x3ff921fb54442d19}, // 1e-300, -5e-324
    {0x01a56e1fc2f8f359, 0x01a56e1fc2f8f359, 0x3fe921fb54442d18}, // 1e-300, 1e-300
    {0x01a56e1fc2f8f359, 0x3ff0000000000000, 0x01a56e1fc2f8f359}, // 1e-300, 1
    {0x01a56e1fc2f8f359, 0xbff0000000000000, 0x400921fb54442d18}, // 1e-300, -1
    {0x01a56e1fc2f8f359, 0x4000000000000000, 0x01956e1fc2f8f359}, // 1e-300, 2
    {0x01a56e1fc2f8f359, 0x3fe0000000000000, 0x01b56e1fc2f8f359}, // 1e-300, 0.5
    {0x01a56e1fc2f8f359, 0x7e37e43c8800759c, 0x0000000000000000}, // 1e-300, 1e+300
    {0x01a56e1fc2f8f359, 0x7ff0000000000000, 0x0000000000000000}, // 1e-300, Infinity
    {0x01a56e1fc2f8f359, 0xfff0000000000000, 0x400921fb54442d18}, // 1e-300, -Infinity
    {0x01a56e1fc2f8f359, 0x7ff8000000000000, 0x7ff8000000000000}, // 1e-300, NaN
    {0x3ff0000000000000, 0x0000000000000000, 0x3ff921fb54442d18}, // 1, 0
    {0x3ff0000000000000, 0x8000000000000000, 0x3ff921fb54442d18}, // 1, -0
    {0x3ff0000000000000, 0x0000000000000001, 0x3ff921fb54442d18}, // 1, 5e-324
    {0x3ff0000000000000, 0x8000000000000001, 0x3ff921fb54442d18}, // 1, -5e-324
    {0x3ff0000000000000, 0x01a56e1fc2f8f359, 0x3ff921fb54442d18}, // 1, 1e-300
    {0x3ff0000000000000, 0x3ff0000000000000, 0x3fe921fb54442d18}, // 1, 1
    {0x3ff0000000000000, 0xbff0000000000000, 0x4002d97c7f3321d2}, // 1, -1
    {0x3ff0000000000000, 0x4000000000000000, 0x3fddac670561bb4f}, // 1, 2
    {0x3ff0000000000000, 0x3fe0000000000000, 0x3ff1b6e192ebbe44}, // 1, 0.5
    {0x3ff0000000000000, 0x7e37e43c8800759c, 0x01a56e1fc2f8f359}, // 1, 1e+300
    {0x3ff0000000000000, 0x7ff0000000000000, 0x0000000000000000}, // 1, Infinity
    {0x3ff0000000000000, 0xfff0000000000000, 0x400921fb54442d18}, // 1, -Infinity
    {0x3ff0000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // 1, NaN
    {0xbff0000000000000, 0x0000000000000000, 0xbff921fb54442d18}, // -1, 0
    {0xbff0000000000000, 0x8000000000000000, 0xbff921fb54442d18}, // -1, -0
    {0xbff0000000000000, 0x0000000000000001, 0xbff921fb54442d18}, // -1, 5e-324
    {0xbff0000000000000, 0x8000000000000001, 0xbff921fb54442d18}, // -1, -5e-324
    {0xbff0000000000000, 0x01a56e1fc2f8f359, 0xbff921fb54442d18}, // -1, 1e-300
    {0xbff0000000000000, 0x3ff0000000000000, 0xbfe921fb54442d18}, // -1, 1
    {0xbff0000000000000, 0xbff0000000000000, 0xc002d97c7f3321d2}, // -1, -1
    {0xbff0000000000000, 0x4000000000000000, 0xbfddac670561bb4f}, // -1, 2
    {0xbff0000000000000, 0x3fe0000000000000, 0xbff1b6e192ebbe44}, // -1, 0.5
    {0xbff0000000000000, 0x7e37e43c8800759c, 0x81a56e1fc2f8f359}, // -1, 1e+300
    {0xbff0000000000000, 0x7ff0000000000000, 0x8000000000000000}, // -1, Infinity
    {0xbff0000000000000, 0xfff0000000000000, 0xc00921fb54442d18}, // -1, -Infinity
    {0xbff0000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // -1, NaN
    {0x4000000000000000, 0x0000000000000000, 0x3ff921fb54442d18}, // 2, 0
    {0x4000000000000000, 0x8000000000000000, 0x3ff921fb54442d18}, // 2, -0
    {0x4000000000000000, 0x0000000000000001, 0x3ff921fb54442d18}, // 2, 5e-324
    {0x4000000000000000, 0x8000000000000001, 0x3ff921fb54442d18}, // 2, -5e-324
    {0x4000000000000000, 0x01a56e1fc2f8f359, 0x3ff921fb54442d18}, // 2, 1e-300
    {0x4000000000000000, 0x3ff0000000000000, 0x3ff1b6e192ebbe44}, // 2, 1
    {0x4000000000000000, 0xbff0000000000000, 0x4000468a8ace4df6}, // 2, -1
    {0x4000000000000000, 0x4000000000000000, 0x3fe921fb54442d18}, // 2, 2
    {0x4000000000000000, 0x3fe0000000000000, 0x3ff5368c951e9cfd}, // 2, 0.5
    {0x4000000000000000, 0x7e37e43c8800759c, 0x01b56e1fc2f8f359}, // 2, 1e+300
    {0x4000000000000000, 0x7ff0000000000000, 0x0000000000000000}, // 2, Infinity
    {0x4000000000000000, 0xfff0000000000000, 0x400921fb54442d18}, // 2, -Infinity
    {0x4000000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // 2, NaN
    {0x3fe0000000000000, 0x0000000000000000, 0x3ff921fb54442d18}, // 0.5, 0
    {0x3fe0000000000000, 0x8000000000000000, 0x3ff921fb54442d18}, // 0.5, -0
    {0x3fe0000000000000, 0x0000000000000001, 0x3ff921fb54442d18}, // 0.5, 5e-324
    {0x3fe0000000000000, 0x8000000000000001, 0x3ff921fb54442d18}, // 0.5, -5e-324
    {0x3fe0000000000000, 0x01a56e1fc2f8f359, 0x3ff921fb54442d18}, // 0.5, 1e-300
    {0x3fe0000000000000, 0x3ff0000000000000, 0x3fddac670561bb4f}, // 0.5, 1
    {0x3fe0000000000000, 0xbff0000000000000, 0x40056c6e7397f5ae}, // 0.5, -1
    {0x3fe0000000000000, 0x4000000000000000, 0x3fcf5b75f92c80dd}, // 0.5, 2
    {0x3fe0000000000000, 0x3fe0000000000000, 0x3fe921fb54442d18}, // 0.5, 0.5
    {0x3fe0000000000000, 0x7e37e43c8800759c, 0x01956e1fc2f8f359}, // 0.5, 1e+300
    {0x3fe0000000000000, 0x7ff0000000000000, 0x0000000000000000}, // 0.5, Infinity
    {0x3fe0000000000000, 0xfff0000000000000, 0x400921fb54442d18}, // 0.5, -Infinity
    {0x3fe0000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // 0.5, NaN
    {0x7e37e43c8800759c, 0x0000000000000000, 0x3ff921fb54442d18}, // 1e+300, 0
    {0x7e37e43c8800759c, 0x8000000000000000, 0x3ff921fb54442d18}, // 1e+300, -0
    {0x7e37e43c8800759c, 0x0000000000000001, 0x3ff921fb54442d18}, // 1e+300, 5e-324
    {0x7e37e43c8800759c, 0x8000000000000001, 0x3ff921fb54442d18}, // 1e+300, -5e-324
    {0x7e37e43c8800759c, 0x01a56e1fc2f8f359, 0x3ff921fb54442d18}, // 1e+300, 1e-300
    {0x7e37e43c8800759c, 0x3ff0000000000000, 0x3ff921fb54442d18}, // 1e+300, 1
    {0x7e37e43c8800759c, 0xbff0000000000000, 0x3ff921fb54442d18}, // 1e+300, -1
    {0x7e37e43c8800759c, 0x4000000000000000, 0x3ff921fb54442d18}, // 1e+300, 2
    {0x7e37e43c8800759c, 0x3fe0000000000000, 0x3ff921fb54442d18}, // 1e+300, 0.5
    {0x7e37e43c8800759c, 0x7e37e43c8800759c, 0x3fe921fb54442d18}, // 1e+300, 1e+300
    {0x7e37e43c8800759c, 0x7ff0000000000000, 0x0000000000000000}, // 1e+300, Infinity
    {0x7e37e43c8800759c, 0xfff0000000000000, 0x400921fb54442d18}, // 1e+300, -Infinity
    {0x7e37e43c8800759c, 0x7ff8000000000000, 0x7ff8000000000000}, // 1e+300, NaN
    {0x7ff0000000000000, 0x0000000000000000, 0x3ff921fb54442d18}, // Infinity, 0
    {0x7ff0000000000000, 0x8000000000000000, 0x3ff921fb54442d18}, // Infinity, -0
    {0x7ff0000000000000, 0x0000000000000001, 0x3ff921fb54442d18}, // Infinity, 5e-324
    {0x7ff0000000000000, 0x8000000000000001, 0x3ff921fb54442d18}, // Infinity, -5e-324
    {0x7ff0000000000000, 0x01a56e1fc2f8f359, 0x3ff921fb54442d18}, // Infinity, 1e-300
    {0x7ff0000000000000, 0x3ff0000000000000, 0x3ff921fb54442d18}, // Infinity, 1
    {0x7ff0000000000000, 0xbff0000000000000, 0x3ff921fb54442d18}, // Infinity, -1
    {0x7ff0000000000000, 0x4000000000000000, 0x3ff921fb54442d18}, // Infinity, 2
    {0x7ff0000000000000, 0x3fe0000000000000, 0x3ff921fb54442d18}, // Infinity, 0.5
    {0x7ff0000000000000, 0x7e37e43c8800759c, 0x3ff921fb54442d18}, // Infinity, 1e+300
    {0x7ff0000000000000, 0x7ff0000000000000, 0x3fe921fb54442d18}, // Infinity, Infinity
    {0x7ff0000000000000, 0xfff0000000000000, 0x4002d97c7f3321d2}, // Infinity, -Infinity
    {0x7ff0000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // Infinity, NaN
    {0xfff0000000000000, 0x0000000000000000, 0xbff921fb54442d18}, // -Infinity, 0
    {0xfff0000000000000, 0x8000000000000000, 0xbff921fb54442d18}, // -Infinity, -0
    {0xfff0000000000000, 0x0000000000000001, 0xbff921fb54442d18}, // -Infinity, 5e-324
    {0xfff0000000000000, 0x8000000000000001, 0xbff921fb54442d18}, // -Infinity, -5e-324
    {0xfff0000000000000, 0x01a56e1fc2f8f359, 0xbff921fb54442d18}, // -Infinity, 1e-300
    {0xfff0000000000000, 0x3ff0000000000000, 0xbff921fb54442d18}, // -Infinity, 1
    {0xfff0000000000000, 0xbff0000000000000, 0xbff921fb54442d18}, // -Infinity, -1
    {0xfff0000000000000, 0x4000000000000000, 0xbff921fb54442d18}, // -Infinity, 2
    {0xfff0000000000000, 0x3fe0000000000000, 0xbff921fb54442d18}, // -Infinity, 0.5
    {0xfff0000000000000, 0x7e37e43c8800759c, 0xbff921fb54442d18}, // -Infinity, 1e+300
    {0xfff0000000000000, 0x7ff0000000000000, 0xbfe921fb54442d18}, // -Infinity, Infinity
    {0xfff0000000000000, 0xfff0000000000000, 0xc002d97c7f3321d2}, // -Infinity, -Infinity
    {0xfff0000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // -Infinity, NaN
    {0x7ff8000000000000, 0x0000000000000000, 0x7ff8000000000000}, // NaN, 0
    {0x7ff8000000000000, 0x8000000000000000, 0x7ff8000000000000}, // NaN, -0
    {0x7ff8000000000000, 0x0000000000000001, 0x7ff8000000000000}, // NaN, 5e-324
    {0x7ff8000000000000, 0x8000000000000001, 0x7ff8000000000000}, // NaN, -5e-324
    {0x7ff8000000000000, 0x01a56e1fc2f8f359, 0x7ff8000000000000}, // NaN, 1e-300
    {0x7ff8000000000000, 0x3ff0000000000000, 0x7ff8000000000000}, // NaN, 1
    {0x7ff8000000000000, 0xbff0000000000000, 0x7ff8000000000000}, // NaN, -1
    {0x7ff8000000000000, 0x4000000000000000, 0x7ff8000000000000}, // NaN, 2
    {0x7ff8000000000000, 0x3fe0000000000000, 0x7ff8000000000000}, // NaN, 0.5
    {0x7ff8000000000000, 0x7e37e43c8800759c, 0x7ff8000000000000}, // NaN, 1e+300
    {0x7ff8000000000000, 0x7ff0000000000000, 0x7ff8000000000000}, // NaN, Infinity
    {0x7ff8000000000000, 0xfff0000000000000, 0x7ff8000000000000}, // NaN, -Infinity
    {0x7ff8000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // NaN, NaN
};

// Node 24 Math.atan and Math.tan: {x, atan(x), tan(x)}.
constexpr Expected V8AtanTan[] = {
    {0x0000000000000000, 0x0000000000000000, 0x0000000000000000}, // 0
    {0x8000000000000000, 0x8000000000000000, 0x8000000000000000}, // -0
    {0x0000000000000001, 0x0000000000000001, 0x0000000000000001}, // 5e-324
    {0x3e20000000000000, 0x3e20000000000000, 0x3e20000000000000}, // 1.862645149230957e-9
    {0x3e30000000000000, 0x3e30000000000000, 0x3e30000000000000}, // 3.725290298461914e-9
    {0x3fdc000000000000, 0x3fda64eec3cc23fd, 0x3fddef49eaab37a1}, // 0.4375
    {0x3fe6000000000000, 0x3fe345f01cce37bb, 0x3fea46cb2be6a0b2}, // 0.6875
    {0x3fe594af4f0d844d, 0x3fe2fc9ea07b6fb2, 0x3fe995054ea00c37}, // 0.6744
    {0x3ff0000000000000, 0x3fe921fb54442d18, 0x3ff8eb245cbee3a6}, // 1
    {0x3ff3000000000000, 0x3febde70ed439fe7, 0x4003d6dc956eac7d}, // 1.1875
    {0x4003800000000000, 0x3ff2e75728833a54, 0xbfeb2d89a938294c}, // 2.4375
    {0x4008000000000000, 0x3ff3fc176b7a8560, 0xbfc23ef71254b86f}, // 3
    {0x4202a05f20000000, 0x3ff921fb543d4de0, 0xbfe1de000f443f50}, // 10000000000
    {0x4410000000000000, 0x3ff921fb54442d18, 0xbfb83d39fb22bdd2}, // 73786976294838210000
    {0x7e37e43c8800759c, 0x3ff921fb54442d18, 0x3ff6be411f37ac77}, // 1e+300
    {0xbff0000000000000, 0xbfe921fb54442d18, 0xbff8eb245cbee3a6}, // -1
    {0xbfe0000000000000, 0xbfddac670561bb4f, 0xbfe17b4f5bf3474a}, // -0.5
    {0x3ff921fb54442d18, 0x3ff00fe987ed02ff, 0x434d02967c31cdb5}, // 1.5707963267948966
    {0x400921fb54442d18, 0x3ff433b8a322ddd2, 0xbca1a62633145c07}, // 3.141592653589793
    {0x4480f0cf064dd592, 0x3ff921fb54442d18, 0xbffa0f79c1b6b258}, // 1e+22
    {0x7ff0000000000000, 0x3ff921fb54442d18, 0x7ff8000000000000}, // Infinity
    {0xfff0000000000000, 0xbff921fb54442d18, 0x7ff8000000000000}, // -Infinity
    {0x7ff8000000000000, 0x7ff8000000000000, 0x7ff8000000000000}, // NaN
};

} // namespace

static_assert(bitsOf(det::atan(1.0)) == 0x3fe921fb54442d18ULL);
static_assert(bitsOf(det::atan2(1.0, -1.0)) == 0x4002d97c7f3321d2ULL);
static_assert(noexcept(det::atan2(1.0, 1.0)) && noexcept(det::atan(1.0f)) && noexcept(det::tan(1.0f)));

TEST_CASE("det atan2 equals V8 on every pair of edge values (zeros, subnormals, infinities, NaN)")
{
    for (const Expected& e : V8Atan2)
    {
        const double y = std::bit_cast<double>(e.x);
        const double x = std::bit_cast<double>(e.sin);
        CAPTURE(y);
        CAPTURE(x);
        CHECK(canonical(det::atan2(y, x)) == e.cos);
    }
}

TEST_CASE("det atan and tan equal V8 on pinned values")
{
    for (const Expected& e : V8AtanTan)
    {
        const double x = std::bit_cast<double>(e.x);
        CAPTURE(x);
        CHECK(canonical(det::atan(x)) == e.sin);
        CHECK(canonical(det::tan(x)) == e.cos);
    }
}

TEST_CASE("det atan, atan2 and tan are bit-identical to V8 over generated inputs")
{
    std::uint64_t ht = 0xCBF29CE484222325ULL;
    std::uint64_t ha = 0xCBF29CE484222325ULL;
    std::uint64_t h2 = 0xCBF29CE484222325ULL;
    for (std::uint64_t i = 0; i < TanCount; ++i)
    {
        ht = fnv(ht, canonical(det::tan(tanInput(i))));
    }
    for (std::uint64_t i = 0; i < AtanCount; ++i)
    {
        ha = fnv(ha, canonical(det::atan(atanInput(i))));
    }
    for (std::uint64_t i = 0; i < Atan2Count; ++i)
    {
        const Atan2Input in = atan2Input(i);
        h2 = fnv(h2, canonical(det::atan2(in.y, in.x)));
    }
    CHECK(ht == V8TanDigest);
    CHECK(ha == V8AtanDigest);
    CHECK(h2 == V8Atan2Digest);
}

TEST_CASE("det atan2 quadrants, symmetry and float overloads")
{
    CHECK(det::atan2(1.0, 1.0) == doctest::Approx(0.78539816339744830962).epsilon(1e-15));
    CHECK(det::atan2(1.0, -1.0) == doctest::Approx(2.35619449019234492885).epsilon(1e-15));
    CHECK(det::atan2(-1.0, -1.0) == doctest::Approx(-2.35619449019234492885).epsilon(1e-15));
    CHECK(det::atan2(-1.0, 1.0) == doctest::Approx(-0.78539816339744830962).epsilon(1e-15));
    CHECK(std::signbit(det::atan2(-0.0, 1.0)));
    CHECK(det::atan2(0.0, -0.0) > 3.14);
    CHECK(det::atan2(-0.0, -0.0) < -3.14);
    CHECK(std::isnan(det::atan2(std::numeric_limits<double>::quiet_NaN(), 1.0)));
    for (std::uint64_t i = 0; i < 2000; ++i)
    {
        const Atan2Input in = atan2Input(i);
        if (std::isnan(in.x) || std::isnan(in.y))
        {
            continue;
        }
        CHECK(bitsOf(det::atan2(-in.y, in.x)) == (bitsOf(det::atan2(in.y, in.x)) ^ (1ULL << 63)));
        CHECK(bitsOf(det::atan(-in.x)) == (bitsOf(det::atan(in.x)) ^ (1ULL << 63)));
    }
    CHECK(det::atan(1.0f) == static_cast<float>(det::atan(1.0)));
    CHECK(det::atan2(1.0f, -2.0f) == static_cast<float>(det::atan2(1.0, -2.0)));
    CHECK(det::tan(0.5f) == static_cast<float>(det::tan(0.5)));
}

TEST_CASE("det atan, atan2 and tan stay under one ulp of a long-double reference")
{
    if constexpr (std::numeric_limits<long double>::digits > 53)
    {
        double worstAtan = 0;
        double worstAtan2 = 0;
        double worstTan = 0;
        for (std::uint64_t i = 0; i < AtanCount; ++i)
        {
            const double x = atanInput(i);
            if (std::isfinite(x) && std::fabs(x) > 1e-300)
            {
                worstAtan = std::max(worstAtan, ulpError(det::atan(x), std::atan(static_cast<long double>(x))));
            }
        }
        for (std::uint64_t i = 0; i < Atan2Count; ++i)
        {
            const Atan2Input in = atan2Input(i);
            const long double ref = std::atan2(static_cast<long double>(in.y), static_cast<long double>(in.x));
            if (std::isfinite(in.x) && std::isfinite(in.y) && std::fabs(ref) > 1e-300L)
            {
                worstAtan2 = std::max(worstAtan2, ulpError(det::atan2(in.y, in.x), ref));
            }
        }
        for (std::uint64_t i = 0; i < TanCount; i += 4)
        {
            const double x = tanInput(i);
            if (std::fabs(x) <= 4.0 && std::fabs(x) > 1e-300) // the reference reduction is trustworthy only here
            {
                worstTan = std::max(worstTan, ulpError(det::tan(x), std::tan(static_cast<long double>(x))));
            }
        }
        MESSAGE("max error (ulp): atan ", worstAtan, ", atan2 ", worstAtan2, ", tan ", worstTan);
        CHECK(worstAtan < 1.0);
        CHECK(worstAtan2 < 2.0); // fdlibm atan2 adds two roundings near the axes
        CHECK(worstTan < 1.0);
    }
}
