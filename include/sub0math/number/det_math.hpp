#pragma once

/*
 * Deterministic sine, cosine, exp, log and pow.
 *
 * The algorithms are Sun Microsystems' fdlibm (as in FreeBSD msun and V8's src/base/ieee754.cc):
 * __kernel_sin, __kernel_cos, __ieee754_rem_pio2 and __kernel_rem_pio2 (Payne-Hanek for large arguments),
 * __ieee754_exp and __ieee754_log.
 * Using only +, -, * and exact scaling, they give the same bits on every IEEE-754 binary64 platform when
 * the compiler does not fuse multiply-add (build with -ffp-contract=off; see sub0math_target_deterministic_fp),
 * and the same bits as JavaScript's Math.sin, Math.cos, Math.exp and Math.log in V8. pow is our own.
 *
 * Original fdlibm notice, preserved as its terms require:
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * Changes from the original: C++23 constexpr and noexcept, std::bit_cast word access, own exact scaling
 * and floor, tables regenerated from the digits of pi (verified against the originals), no errno.
 */

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "sub0math/number/real.hpp"

namespace sub0math::det
{

namespace detail
{

constexpr std::int32_t highWord(double x) noexcept
{
    return static_cast<std::int32_t>(std::bit_cast<std::uint64_t>(x) >> 32);
}

constexpr double fromHighWord(std::int32_t high) noexcept
{
    return std::bit_cast<double>(static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32);
}

constexpr double fromWords(std::int32_t high, std::uint32_t low) noexcept
{
    return std::bit_cast<double>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(high)) << 32) | low);
}

// Exact power of two for -1022 <= n <= 1023.
constexpr double powerOfTwo(int n) noexcept
{
    return std::bit_cast<double>(static_cast<std::uint64_t>(n + 1023) << 52);
}

// x * 2^n, exact except where the result is subnormal (which this file never reaches).
constexpr double scaleByPowerOfTwo(double x, int n) noexcept
{
    while (n > 1023)
    {
        x *= powerOfTwo(1023);
        n -= 1023;
    }
    while (n < -1022)
    {
        x *= powerOfTwo(-1022);
        n += 1022;
    }
    return x * powerOfTwo(n);
}

// floor for |x| < 2^52 (the only range used here), constexpr without relying on std::floor.
constexpr double floorSmall(double x) noexcept
{
    const double t = static_cast<double>(static_cast<std::int64_t>(x));
    return t > x ? t - 1.0 : t;
}

// 2/pi in 24-bit chunks (the first is the integer part of 2^24 * 2/pi).
inline constexpr std::array<std::int32_t, 66> TwoOverPi{
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62, 0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7,
    0x246E3A, 0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129, 0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C,
    0x7026B4, 0x5F7E41, 0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8, 0x97FFDE, 0x05980F, 0xEF2F11,
    0x8B5A0A, 0x6D1F6D, 0x367ECF, 0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5, 0xF17B3D, 0x0739F7,
    0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08, 0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3, 0x91615E,
    0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880, 0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B};

// pi/2 in 24-bit chunks.
inline constexpr std::array<double, 8> PiOver2Chunks{
    1.57079625129699707031e+00, 7.54978941586159635335e-08, 5.39030252995776476554e-15, 3.28200341580791294123e-22,
    1.27065575308067607349e-29, 1.22933308981111328932e-36, 2.73370053816464559624e-44, 2.16741683877804819444e-51};

// High words of n * pi/2, n = 1..32 (detects possible cancellation in the medium-range reduction).
inline constexpr std::array<std::int32_t, 32> NPiOver2HighWords{
    0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
    0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
    0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
    0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB};

constexpr std::size_t idx(int i) noexcept
{
    return static_cast<std::size_t>(i);
}

// Computes x mod pi/2 for huge |x| in several 24-bit pieces (Payne-Hanek); precision "double" (prec = 2).
// x holds the scaled argument in 24-bit chunks, e0 the exponent of its first chunk, nx the chunk count.
// Returns n mod 8; y receives the remainder as an unevaluated sum y[0] + y[1].
constexpr int kernelRemPio2(const double* x, double* y, int e0, int nx) noexcept
{
    constexpr int jk = 4;
    constexpr double two24 = 16777216.0;
    constexpr double twon24 = 5.96046447753906250000e-08;

    std::array<std::int32_t, 20> iq{};
    std::array<double, 20> f{};
    std::array<double, 20> fq{};
    std::array<double, 20> q{};

    const int jp = jk;
    const int jx = nx - 1;
    int jv = (e0 - 3) / 24;
    if (jv < 0)
    {
        jv = 0;
    }
    int q0 = e0 - 24 * (jv + 1);

    int j = jv - jx;
    const int m = jx + jk;
    for (int i = 0; i <= m; ++i, ++j)
    {
        f[idx(i)] = j < 0 ? 0.0 : static_cast<double>(TwoOverPi[idx(j)]);
    }
    for (int i = 0; i <= jk; ++i)
    {
        double fw = 0.0;
        for (j = 0; j <= jx; ++j)
        {
            fw += x[j] * f[idx(jx + i - j)];
        }
        q[idx(i)] = fw;
    }

    int jz = jk;
    int n = 0;
    int ih = 0;
    double z = 0.0;
    for (;;)
    {
        // Distil q[] into iq[] in reverse order.
        z = q[idx(jz)];
        for (int i = 0, jj = jz; jj > 0; ++i, --jj)
        {
            const double fw = static_cast<double>(static_cast<std::int32_t>(twon24 * z));
            iq[idx(i)] = static_cast<std::int32_t>(z - two24 * fw);
            z = q[idx(jj - 1)] + fw;
        }

        z = scaleByPowerOfTwo(z, q0);
        z -= 8.0 * floorSmall(z * 0.125); // trim off integer >= 8
        n = static_cast<std::int32_t>(z);
        z -= static_cast<double>(n);
        ih = 0;
        if (q0 > 0)
        {
            const std::int32_t i = iq[idx(jz - 1)] >> (24 - q0);
            n += i;
            iq[idx(jz - 1)] -= i << (24 - q0);
            ih = iq[idx(jz - 1)] >> (23 - q0);
        }
        else if (q0 == 0)
        {
            ih = iq[idx(jz - 1)] >> 23;
        }
        else if (z >= 0.5)
        {
            ih = 2;
        }

        if (ih > 0) // q > 0.5
        {
            n += 1;
            int carry = 0;
            for (int i = 0; i < jz; ++i) // compute 1 - q
            {
                const std::int32_t jq = iq[idx(i)];
                if (carry == 0)
                {
                    if (jq != 0)
                    {
                        carry = 1;
                        iq[idx(i)] = 0x1000000 - jq;
                    }
                }
                else
                {
                    iq[idx(i)] = 0xffffff - jq;
                }
            }
            if (q0 > 0) // rare case: chance is 1 in 12
            {
                if (q0 == 1)
                {
                    iq[idx(jz - 1)] &= 0x7fffff;
                }
                else if (q0 == 2)
                {
                    iq[idx(jz - 1)] &= 0x3fffff;
                }
            }
            if (ih == 2)
            {
                z = 1.0 - z;
                if (carry != 0)
                {
                    z -= scaleByPowerOfTwo(1.0, q0);
                }
            }
        }

        // Check whether recomputation is needed.
        if (z == 0.0)
        {
            std::int32_t jor = 0;
            for (int i = jz - 1; i >= jk; --i)
            {
                jor |= iq[idx(i)];
            }
            if (jor == 0) // need recomputation
            {
                int k = 1;
                while (iq[idx(jk - k)] == 0)
                {
                    ++k; // k = number of additional terms needed
                }
                for (int i = jz + 1; i <= jz + k; ++i) // add q[jz+1] to q[jz+k]
                {
                    f[idx(jx + i)] = static_cast<double>(TwoOverPi[idx(jv + i)]);
                    double fw = 0.0;
                    for (j = 0; j <= jx; ++j)
                    {
                        fw += x[j] * f[idx(jx + i - j)];
                    }
                    q[idx(i)] = fw;
                }
                jz += k;
                continue;
            }
        }
        break;
    }

    // Chop off zero terms.
    if (z == 0.0)
    {
        jz -= 1;
        q0 -= 24;
        while (iq[idx(jz)] == 0)
        {
            jz--;
            q0 -= 24;
        }
    }
    else // break z into 24-bit pieces if necessary
    {
        z = scaleByPowerOfTwo(z, -q0);
        if (z >= two24)
        {
            const double fw = static_cast<double>(static_cast<std::int32_t>(twon24 * z));
            iq[idx(jz)] = static_cast<std::int32_t>(z - two24 * fw);
            jz += 1;
            q0 += 24;
            iq[idx(jz)] = static_cast<std::int32_t>(fw);
        }
        else
        {
            iq[idx(jz)] = static_cast<std::int32_t>(z);
        }
    }

    // Convert integer "bit" chunks to floating-point value.
    double fw = scaleByPowerOfTwo(1.0, q0);
    for (int i = jz; i >= 0; --i)
    {
        q[idx(i)] = fw * static_cast<double>(iq[idx(i)]);
        fw *= twon24;
    }

    // Compute PIo2[0..jp] * q[jz..0].
    for (int i = jz; i >= 0; --i)
    {
        fw = 0.0;
        for (int k = 0; k <= jp && k <= jz - i; ++k)
        {
            fw += PiOver2Chunks[idx(k)] * q[idx(i + k)];
        }
        fq[idx(jz - i)] = fw;
    }

    // Compress fq[] into y[].
    fw = 0.0;
    for (int i = jz; i >= 0; --i)
    {
        fw += fq[idx(i)];
    }
    y[0] = ih == 0 ? fw : -fw;
    fw = fq[0] - fw;
    for (int i = 1; i <= jz; ++i)
    {
        fw += fq[idx(i)];
    }
    y[1] = ih == 0 ? fw : -fw;
    return n & 7;
}

// Result of reducing an argument modulo pi/2: x = n * pi/2 + (y0 + y1), |y0 + y1| <= pi/4.
struct Reduced
{
    int n;
    double y0;
    double y1;
};

// Reduces finite |x| > pi/4 modulo pi/2.
constexpr Reduced remPio2(double x) noexcept
{
    constexpr double half = 0.5;
    constexpr double invpio2 = 6.36619772367581382433e-01;
    constexpr double pio2_1 = 1.57079632673412561417e+00;
    constexpr double pio2_1t = 6.07710050650619224932e-11;
    constexpr double pio2_2 = 6.07710050630396597660e-11;
    constexpr double pio2_2t = 2.02226624879595063154e-21;
    constexpr double pio2_3 = 2.02226624871116645580e-21;
    constexpr double pio2_3t = 8.47842766036889956997e-32;

    const std::int32_t hx = highWord(x);
    const std::int32_t ix = hx & 0x7fffffff;

    if (ix < 0x4002d97c) // |x| < 3pi/4, special case with n = +-1
    {
        if (hx > 0)
        {
            double z = x - pio2_1;
            if (ix != 0x3ff921fb) // 33 + 53 bit pi is good enough
            {
                const double y0 = z - pio2_1t;
                return {1, y0, (z - y0) - pio2_1t};
            }
            z -= pio2_2; // near pi/2, use 33 + 33 + 53 bit pi
            const double y0 = z - pio2_2t;
            return {1, y0, (z - y0) - pio2_2t};
        }
        double z = x + pio2_1;
        if (ix != 0x3ff921fb)
        {
            const double y0 = z + pio2_1t;
            return {-1, y0, (z - y0) + pio2_1t};
        }
        z += pio2_2;
        const double y0 = z + pio2_2t;
        return {-1, y0, (z - y0) + pio2_2t};
    }

    if (ix <= 0x413921fb) // |x| ~<= 2^19 * (pi/2), medium size
    {
        const double t = hx < 0 ? -x : x;
        const int n = static_cast<std::int32_t>(t * invpio2 + half);
        const double fn = static_cast<double>(n);
        double r = t - fn * pio2_1;
        double w = fn * pio2_1t; // 1st round, good to 85 bit
        double y0 = 0.0;
        if (n < 32 && ix != NPiOver2HighWords[idx(n - 1)])
        {
            y0 = r - w; // quick check, no cancellation
        }
        else
        {
            const std::int32_t j = ix >> 20;
            y0 = r - w;
            std::int32_t i = j - ((highWord(y0) >> 20) & 0x7ff);
            if (i > 16) // 2nd iteration needed, good to 118
            {
                const double t2 = r;
                w = fn * pio2_2;
                r = t2 - w;
                w = fn * pio2_2t - ((t2 - r) - w);
                y0 = r - w;
                i = j - ((highWord(y0) >> 20) & 0x7ff);
                if (i > 49) // 3rd iteration needed, 151 bits accuracy
                {
                    const double t3 = r;
                    w = fn * pio2_3;
                    r = t3 - w;
                    w = fn * pio2_3t - ((t3 - r) - w);
                    y0 = r - w;
                }
            }
        }
        const double y1 = (r - y0) - w;
        if (hx < 0)
        {
            return {-n, -y0, -y1};
        }
        return {n, y0, y1};
    }

    // |x| is large (the caller has excluded Inf and NaN): Payne-Hanek.
    const std::uint32_t low = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x));
    const int e0 = (ix >> 20) - 1046; // e0 = ilogb(x) - 23
    double z = fromWords(ix - (e0 << 20), low);
    std::array<double, 3> tx{};
    for (std::size_t i = 0; i < 2; ++i)
    {
        tx[i] = static_cast<double>(static_cast<std::int32_t>(z));
        z = (z - tx[i]) * 16777216.0;
    }
    tx[2] = z;
    int nx = 3;
    while (tx[idx(nx - 1)] == 0.0)
    {
        --nx; // skip zero terms
    }
    std::array<double, 2> y{};
    const int n = kernelRemPio2(tx.data(), y.data(), e0, nx);
    if (hx < 0)
    {
        return {-n, -y[0], -y[1]};
    }
    return {n, y[0], y[1]};
}

// sin on |x| <= pi/4 with a tail y (x + y is the argument); iy == 0 means y is zero.
constexpr double kernelSin(double x, double y, int iy) noexcept
{
    constexpr double half = 5.00000000000000000000e-01;
    constexpr double S1 = -1.66666666666666324348e-01;
    constexpr double S2 = 8.33333333332248946124e-03;
    constexpr double S3 = -1.98412698298579493134e-04;
    constexpr double S4 = 2.75573137070700676789e-06;
    constexpr double S5 = -2.50507602534068634195e-08;
    constexpr double S6 = 1.58969099521155010221e-10;

    const std::int32_t ix = highWord(x) & 0x7fffffff;
    if (ix < 0x3e400000) // |x| < 2^-27
    {
        if (static_cast<int>(x) == 0)
        {
            return x;
        }
    }
    const double z = x * x;
    const double v = z * x;
    const double r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0)
    {
        return x + v * (S1 + z * r);
    }
    return x - ((z * (half * y - v * r) - y) - v * S1);
}

// cos on |x| <= pi/4 with a tail y.
constexpr double kernelCos(double x, double y) noexcept
{
    constexpr double one = 1.0;
    constexpr double C1 = 4.16666666666666019037e-02;
    constexpr double C2 = -1.38888888888741095749e-03;
    constexpr double C3 = 2.48015872894767294178e-05;
    constexpr double C4 = -2.75573143513906633035e-07;
    constexpr double C5 = 2.08757232129817482790e-09;
    constexpr double C6 = -1.13596475577881948265e-11;

    const std::int32_t ix = highWord(x) & 0x7fffffff;
    if (ix < 0x3e400000) // |x| < 2^-27
    {
        if (static_cast<int>(x) == 0)
        {
            return one;
        }
    }
    const double z = x * x;
    const double r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333) // |x| < 0.3
    {
        return one - (0.5 * z - (z * r - x * y));
    }
    double qx = 0.0;
    if (ix > 0x3fe90000) // |x| > 0.78125
    {
        qx = 0.28125;
    }
    else
    {
        qx = fromHighWord(ix - 0x00200000); // |x| / 4
    }
    const double hz = 0.5 * z - qx;
    const double a = one - qx;
    return a - (hz - (z * r - x * y));
}

constexpr double sinFromReduction(const Reduced& r) noexcept
{
    switch (r.n & 3)
    {
    case 0:
        return kernelSin(r.y0, r.y1, 1);
    case 1:
        return kernelCos(r.y0, r.y1);
    case 2:
        return -kernelSin(r.y0, r.y1, 1);
    default:
        return -kernelCos(r.y0, r.y1);
    }
}

constexpr double cosFromReduction(const Reduced& r) noexcept
{
    switch (r.n & 3)
    {
    case 0:
        return kernelCos(r.y0, r.y1);
    case 1:
        return -kernelSin(r.y0, r.y1, 1);
    case 2:
        return -kernelCos(r.y0, r.y1);
    default:
        return kernelSin(r.y0, r.y1, 1);
    }
}

} // namespace detail

/**
 * A sine and cosine of one argument.
 *
 * @tparam T Floating-point type.
 */
template<Real T>
struct SinCos
{
    T sin; ///< Sine.
    T cos; ///< Cosine.
};

/**
 * Computes the sine of an angle in radians, bit-identically on every platform (fdlibm; equals V8 Math.sin).
 *
 * Accurate to under 1 ulp for all finite inputs; NaN for infinities and NaN, as IEEE. The result depends
 * only on IEEE-754 +, -, * and conversions, never on the C library; build with `-ffp-contract=off`.
 *
 * @param x The angle in radians.
 * @return sin(x).
 */
[[nodiscard]] constexpr double sin(double x) noexcept
{
    const std::int32_t ix = detail::highWord(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) // |x| ~< pi/4
    {
        return detail::kernelSin(x, 0.0, 0);
    }
    if (ix >= 0x7ff00000) // sin(Inf or NaN) is NaN
    {
        return x - x;
    }
    return detail::sinFromReduction(detail::remPio2(x));
}

/**
 * Computes the cosine of an angle in radians, bit-identically on every platform (fdlibm; equals V8 Math.cos).
 *
 * @param x The angle in radians.
 * @return cos(x).
 * @see sin for accuracy and the determinism contract.
 */
[[nodiscard]] constexpr double cos(double x) noexcept
{
    const std::int32_t ix = detail::highWord(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb)
    {
        return detail::kernelCos(x, 0.0);
    }
    if (ix >= 0x7ff00000)
    {
        return x - x;
    }
    return detail::cosFromReduction(detail::remPio2(x));
}

/**
 * Computes sine and cosine together, sharing the argument reduction; each equals the separate call.
 *
 * @param x The angle in radians.
 * @return The pair {sin(x), cos(x)}.
 */
[[nodiscard]] constexpr SinCos<double> sincos(double x) noexcept
{
    const std::int32_t ix = detail::highWord(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb)
    {
        return {detail::kernelSin(x, 0.0, 0), detail::kernelCos(x, 0.0)};
    }
    if (ix >= 0x7ff00000)
    {
        const double nan = x - x;
        return {nan, nan};
    }
    const detail::Reduced r = detail::remPio2(x);
    return {detail::sinFromReduction(r), detail::cosFromReduction(r)};
}

/**
 * Computes e^x, bit-identically on every platform (fdlibm; equals V8 Math.exp).
 *
 * Accurate to under 1 ulp. Overflows to +infinity above 709.78 and underflows to zero below -745.13; NaN stays NaN.
 *
 * @param x The exponent.
 * @return e^x.
 */
[[nodiscard]] constexpr double exp(double x) noexcept
{
    constexpr double one = 1.0;
    constexpr double huge = 1.0e+300;
    constexpr double twom1000 = 9.33263618503218878990e-302; // 2^-1000
    constexpr double oThreshold = 7.09782712893383973096e+02;
    constexpr double uThreshold = -7.45133219101941108420e+02;
    constexpr double invln2 = 1.44269504088896338700e+00;
    constexpr double P1 = 1.66666666666666019037e-01;
    constexpr double P2 = -2.77777777770155933842e-03;
    constexpr double P3 = 6.61375632143793436117e-05;
    constexpr double P4 = -1.65339022054652515390e-06;
    constexpr double P5 = 4.13813679705723846039e-08;
    constexpr double ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01};
    constexpr double ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10};
    constexpr double halF[2] = {0.5, -0.5};

    const std::uint64_t bits = std::bit_cast<std::uint64_t>(x);
    std::int32_t hx = static_cast<std::int32_t>(bits >> 32);
    const std::uint32_t lx = static_cast<std::uint32_t>(bits);
    const int xsb = (hx >> 31) & 1; // sign bit of x
    hx &= 0x7fffffff;               // high word of |x|

    if (hx >= 0x40862E42) // |x| >= 709.78
    {
        if (hx >= 0x7ff00000)
        {
            if (((hx & 0xfffff) | static_cast<std::int32_t>(lx)) != 0)
            {
                return x + x; // NaN
            }
            return xsb == 0 ? x : 0.0; // exp(+-inf) = {inf, 0}
        }
        if (x > oThreshold)
        {
            return huge * huge; // overflow
        }
        if (x < uThreshold)
        {
            return twom1000 * twom1000; // underflow
        }
    }

    double hi = 0.0;
    double lo = 0.0;
    int k = 0;
    if (hx > 0x3fd62e42) // |x| > 0.5 ln2
    {
        if (hx < 0x3FF0A2B2) // and |x| < 1.5 ln2
        {
            if (x == 1.0)
            {
                return 2.718281828459045; // V8 special-cases e: the algorithm gets the last bit of exp(1) wrong
            }
            hi = x - ln2HI[xsb];
            lo = ln2LO[xsb];
            k = 1 - xsb - xsb;
        }
        else
        {
            k = static_cast<int>(invln2 * x + halF[xsb]);
            const double t = static_cast<double>(k);
            hi = x - t * ln2HI[0]; // t * ln2HI is exact here
            lo = t * ln2LO[0];
        }
        x = hi - lo;
    }
    else if (hx < 0x3e300000) // |x| < 2^-28
    {
        if (huge + x > one)
        {
            return one + x; // trigger inexact
        }
    }

    // x is now in the primary range.
    const double t = x * x;
    const double c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0)
    {
        return one - ((x * c) / (c - 2.0) - x);
    }
    const double y = one - ((lo - (x * c) / (2.0 - c)) - hi);
    if (k >= -1021)
    {
        if (k == 1024)
        {
            return y * 2.0 * 8.98846567431158e307;
        }
        return y * detail::fromHighWord(0x3ff00000 + (k << 20));
    }
    return y * detail::fromHighWord(0x3ff00000 + ((k + 1000) << 20)) * twom1000;
}

/**
 * Computes the natural logarithm, bit-identically on every platform (fdlibm; equals V8 Math.log).
 *
 * Accurate to under 1 ulp. log(0) is -infinity, negative arguments give NaN, +infinity and NaN pass through.
 *
 * @param x The argument.
 * @return ln(x).
 */
[[nodiscard]] constexpr double log(double x) noexcept
{
    constexpr double ln2Hi = 6.93147180369123816490e-01;
    constexpr double ln2Lo = 1.90821492927058770002e-10;
    constexpr double two54 = 1.80143985094819840000e+16;
    constexpr double Lg1 = 6.666666666666735130e-01;
    constexpr double Lg2 = 3.999999999940941908e-01;
    constexpr double Lg3 = 2.857142874366239149e-01;
    constexpr double Lg4 = 2.222219843214978396e-01;
    constexpr double Lg5 = 1.818357216161805012e-01;
    constexpr double Lg6 = 1.531383769920937332e-01;
    constexpr double Lg7 = 1.479819860511658591e-01;

    std::int32_t hx = detail::highWord(x);
    std::uint32_t lx = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x));

    int k = 0;
    if (hx < 0x00100000) // x < 2^-1022
    {
        if (((hx & 0x7fffffff) | static_cast<std::int32_t>(lx)) == 0)
        {
            return -std::numeric_limits<double>::infinity(); // log(+-0) = -inf
        }
        if (hx < 0)
        {
            return std::numeric_limits<double>::quiet_NaN(); // log(-#) = NaN
        }
        k -= 54;
        x *= two54; // subnormal number, scale up x
        hx = detail::highWord(x);
        lx = static_cast<std::uint32_t>(std::bit_cast<std::uint64_t>(x));
    }
    if (hx >= 0x7ff00000)
    {
        return x + x;
    }
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;
    std::int32_t i = (hx + 0x95f64) & 0x100000;
    x = detail::fromWords(hx | (i ^ 0x3ff00000), lx); // normalise x or x/2
    k += (i >> 20);
    const double f = x - 1.0;
    if ((0x000fffff & (2 + hx)) < 3) // -2^-20 <= f < 2^-20
    {
        if (f == 0.0)
        {
            if (k == 0)
            {
                return 0.0;
            }
            const double dk = static_cast<double>(k);
            return dk * ln2Hi + dk * ln2Lo;
        }
        const double R = f * f * (0.5 - 0.33333333333333333 * f);
        if (k == 0)
        {
            return f - R;
        }
        const double dk = static_cast<double>(k);
        return dk * ln2Hi - ((R - dk * ln2Lo) - f);
    }
    const double s = f / (2.0 + f);
    const double dk = static_cast<double>(k);
    const double z = s * s;
    i = hx - 0x6147a;
    const double w = z * z;
    const std::int32_t j = 0x6b851 - hx;
    const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    const double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    const double R = t2 + t1;
    if (i > 0)
    {
        const double hfsq = 0.5 * f * f;
        if (k == 0)
        {
            return f - (hfsq - s * (hfsq + R));
        }
        return dk * ln2Hi - ((hfsq - (s * (hfsq + R) + dk * ln2Lo)) - f);
    }
    if (k == 0)
    {
        return f - s * (f - R);
    }
    return dk * ln2Hi - ((s * (f - R) - dk * ln2Lo) - f);
}

/**
 * Computes x^y deterministically (our own definition, not fdlibm's pow, so not equal to V8's Math.pow).
 *
 * A whole-number y of magnitude up to 1024 uses repeated squaring: exact for x^2, a few ulp for larger powers.
 * Any other y with x > 0 uses exp(y log x), whose relative error is about |y log x| ulp; it is meant for step-size
 * and impedance exponents, not for ulp-accurate mathematics. Fractional y with x < 0 gives NaN; x = 0 gives 0 for
 * y > 0 and +infinity for y < 0; pow(x, 0) is 1 for every x.
 *
 * @param x The base.
 * @param y The exponent.
 * @return x^y.
 */
[[nodiscard]] constexpr double pow(double x, double y) noexcept
{
    if (y == 0.0)
    {
        return 1.0;
    }
    if (x != x || y != y)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double magnitude = y < 0.0 ? -y : y;
    if (magnitude <= 1024.0 && static_cast<double>(static_cast<std::int32_t>(magnitude)) == magnitude)
    {
        auto n = static_cast<std::uint32_t>(magnitude);
        double result = 1.0;
        double base = x;
        while (n != 0)
        {
            if ((n & 1U) != 0)
            {
                result *= base;
            }
            base *= base;
            n >>= 1U;
        }
        return y < 0.0 ? 1.0 / result : result;
    }
    if (x == 0.0)
    {
        return y > 0.0 ? 0.0 : std::numeric_limits<double>::infinity();
    }
    if (x < 0.0)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return exp(y * log(x));
}

/**
 * Computes the sine in single precision: the double result rounded to float (deterministic).
 *
 * @param x The angle in radians.
 * @return sin(x).
 */
[[nodiscard]] constexpr float sin(float x) noexcept
{
    return static_cast<float>(sin(static_cast<double>(x)));
}

/**
 * Computes the cosine in single precision.
 *
 * @param x The angle in radians.
 * @return cos(x).
 */
[[nodiscard]] constexpr float cos(float x) noexcept
{
    return static_cast<float>(cos(static_cast<double>(x)));
}

/**
 * Computes sine and cosine in single precision.
 *
 * @param x The angle in radians.
 * @return The pair {sin(x), cos(x)}.
 */
[[nodiscard]] constexpr SinCos<float> sincos(float x) noexcept
{
    const SinCos<double> r = sincos(static_cast<double>(x));
    return {static_cast<float>(r.sin), static_cast<float>(r.cos)};
}

} // namespace sub0math::det
