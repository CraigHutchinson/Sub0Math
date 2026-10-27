#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <doctest.h>
#include <limits>
#include <numbers>
#include <random>

#include "sub0math/geometry.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;

namespace
{

using Quad = ConvexPolygon<double, 4>;
using LD = long double;

static_assert(sizeof(Witness<double, 2>) == 2 * 2 * sizeof(double) + 2 * sizeof(std::size_t));
static_assert(std::is_trivially_copyable_v<CapsuleContact<double, 2>>);
static_assert(std::is_trivially_copyable_v<Quad>);

// ---- Independent references in long double: no closed form, only search over parameters and directions ----

LD pointSegmentSquared(LD px, LD py, LD ax, LD ay, LD bx, LD by)
{
    const LD dx = bx - ax;
    const LD dy = by - ay;
    const LD l2 = dx * dx + dy * dy;
    LD t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = std::clamp<LD>(t, 0, 1);
    const LD ex = ax + t * dx - px;
    const LD ey = ay + t * dy - py;
    return ex * ex + ey * ey;
}

// Minimises a unimodal function on [0, 1] by golden-section search after a coarse scan.
template<typename F>
LD minimise(const F& f, int scan = 400)
{
    int best = 0;
    LD bestValue = f(LD{0});
    for (int i = 1; i <= scan; ++i)
    {
        const LD v = f(static_cast<LD>(i) / scan);
        if (v < bestValue)
        {
            bestValue = v;
            best = i;
        }
    }
    LD lo = std::max<LD>(0, static_cast<LD>(best - 1) / scan);
    LD hi = std::min<LD>(1, static_cast<LD>(best + 1) / scan);
    const LD g = (std::sqrt(LD{5}) - 1) / 2;
    for (int it = 0; it < 200; ++it)
    {
        const LD m1 = hi - g * (hi - lo);
        const LD m2 = lo + g * (hi - lo);
        if (f(m1) < f(m2))
        {
            hi = m2;
        }
        else
        {
            lo = m1;
        }
    }
    return std::min({bestValue, f(lo), f(hi), f((lo + hi) / 2)});
}

// Distance between two segments: minimise over the first parameter of the exact point-segment distance (convex).
LD segmentDistanceReference(const Segment<double, 2>& s1, const Segment<double, 2>& s2)
{
    return std::sqrt(minimise([&](LD s) {
        const LD px = s1.a.x + s * (static_cast<LD>(s1.b.x) - s1.a.x);
        const LD py = s1.a.y + s * (static_cast<LD>(s1.b.y) - s1.a.y);
        return pointSegmentSquared(px, py, s2.a.x, s2.a.y, s2.b.x, s2.b.y);
    }));
}

// Signed distance between a segment and a convex polygon as the largest separation over all directions:
// max over unit n of [min over the segment of x.n - max over the polygon of x.n]. Exact for convex bodies,
// in or out of contact; found by a dense angle scan and golden refinement.
LD signedDistanceReference(const Segment<double, 2>& s, const Quad& p)
{
    const auto separation = [&](LD angle) {
        const LD nx = std::cos(angle);
        const LD ny = std::sin(angle);
        const LD sa = s.a.x * nx + s.a.y * ny;
        const LD sb = s.b.x * nx + s.b.y * ny;
        LD hp = -std::numeric_limits<LD>::infinity();
        for (const auto& v : p.vertices)
        {
            hp = std::max(hp, v.x * nx + v.y * ny);
        }
        return std::min(sa, sb) - hp;
    };
    constexpr int Scan = 20000;
    const LD twoPi = 2 * std::numbers::pi_v<LD>;
    int best = 0;
    LD bestValue = separation(0);
    for (int i = 1; i < Scan; ++i)
    {
        const LD v = separation(twoPi * i / Scan);
        if (v > bestValue)
        {
            bestValue = v;
            best = i;
        }
    }
    LD lo = twoPi * (best - 1) / Scan;
    LD hi = twoPi * (best + 1) / Scan;
    const LD g = (std::sqrt(LD{5}) - 1) / 2;
    for (int it = 0; it < 200; ++it)
    {
        const LD m1 = hi - g * (hi - lo);
        const LD m2 = lo + g * (hi - lo);
        if (separation(m1) > separation(m2))
        {
            hi = m2;
        }
        else
        {
            lo = m1;
        }
    }
    return std::max(bestValue, separation((lo + hi) / 2));
}

Quad randomQuad(std::mt19937_64& rng)
{
    // A random convex quadrilateral: four sorted angles on a jittered ellipse, so it is strictly convex.
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::array<double, 4> angles{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        angles[i] = (static_cast<double>(i) + 0.15 + 0.7 * u(rng)) * std::numbers::pi / 2.0;
    }
    const double rx = 0.2 + u(rng);
    const double ry = 0.2 + u(rng);
    const double cx = u(rng) - 0.5;
    const double cy = u(rng) - 0.5;
    std::array<Point2d, 4> v{};
    for (std::size_t i = 0; i < 4; ++i)
    {
        v[i] = {cx + rx * std::cos(angles[i]), cy + ry * std::sin(angles[i])};
    }
    return Quad::fromVertices(v);
}

// A terrain-like slab: a sloped top over a floor with vertical sides.
Quad slab(double ax, double az, double bx, double bz, double floor)
{
    return Quad::fromVertices({Point2d{ax, floor}, Point2d{bx, floor}, Point2d{bx, bz}, Point2d{ax, az}});
}

} // namespace

TEST_CASE("Segment pair: closed form against a search, in every configuration")
{
    std::mt19937_64 rng(7);
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int k = 0; k < 2000; ++k)
    {
        Segment<double, 2> s1{{u(rng), u(rng)}, {u(rng), u(rng)}};
        Segment<double, 2> s2{{u(rng), u(rng)}, {u(rng), u(rng)}};
        if (k % 5 == 0)
        {
            s2.b = s2.a + (s1.b - s1.a) * 0.7; // parallel
        }
        if (k % 7 == 0)
        {
            s2.b = s2.a; // a point
        }
        const auto pair = closestPoints(s1, s2);
        const double d = length(pair.first - pair.second);
        CHECK(std::abs(d - static_cast<double>(segmentDistanceReference(s1, s2))) < 1e-9);
        CHECK(pair.firstT >= 0.0);
        CHECK(pair.firstT <= 1.0);
        if (pair.secondT == 1.0)
        {
            CHECK(pair.second == s2.b);
        }
    }
}

TEST_CASE("Segment pair: crossing, 3D skew lines and quantities")
{
    const auto cross = closestPoints(Segment<double, 2>{{-1.0, 0.0}, {1.0, 0.0}}, Segment<double, 2>{{0.25, -1.0}, {0.25, 1.0}});
    CHECK(length(cross.first - cross.second) == 0.0);
    CHECK(cross.firstT == doctest::Approx(0.625));

    const auto skew = closestPoints(Segment<double, 3>{{-1.0, 0.0, 0.0}, {1.0, 0.0, 0.0}},
                                    Segment<double, 3>{{0.5, -1.0, 2.0}, {0.5, 1.0, 2.0}});
    CHECK(length(skew.first - skew.second) == doctest::Approx(2.0));
    CHECK(skew.first.x == doctest::Approx(0.5));

    using L = f64::Length;
    const Segment<L, 2> q1{{metres(0.0), metres(0.0)}, {metres(1.0), metres(0.0)}};
    const Segment<L, 2> q2{{metres(2.0), metres(1.0)}, {metres(3.0), metres(1.0)}};
    const auto qp = closestPoints(q1, q2);
    CHECK(length(qp.first - qp.second).si() == doctest::Approx(std::sqrt(2.0)));
    const auto qc = contact(Capsule<L, 2>{q1, metres(0.25)}, q2);
    CHECK(qc.gap.si() == doctest::Approx(std::sqrt(2.0) - 0.25));
    CHECK(qc.witness.kind == WitnessKind::Vertex);
    CHECK(qc.witness.index == 0U);
}

TEST_CASE("Capsule and segment: witness kinds and normals")
{
    const Segment<double, 2> ground{{0.0, 0.0}, {1.0, 0.0}};
    // Above the interior: an edge witness, normal straight up.
    const auto above = contact(Capsule<double, 2>{{{0.3, 0.5}, {0.6, 0.2}}, 0.1}, ground);
    CHECK(above.witness.kind == WitnessKind::Edge);
    CHECK(above.gap == doctest::Approx(0.1));
    CHECK(above.witness.normal.y == doctest::Approx(1.0));
    CHECK(above.axisT == 1.0);
    // Beyond the end: the vertex with the true direction.
    const auto beyond = contact(Capsule<double, 2>{{{1.3, 0.4}, {1.3, 0.8}}, 0.1}, ground);
    CHECK(beyond.witness.kind == WitnessKind::Vertex);
    CHECK(beyond.witness.index == 1U);
    CHECK(beyond.witness.normal.x == doctest::Approx(0.6));
    CHECK(beyond.witness.normal.y == doctest::Approx(0.8));
    CHECK(beyond.gap == doctest::Approx(0.4));
    // Crossing: distance zero, normal on the side of the far end.
    const auto crossing = contact(Capsule<double, 2>{{{0.5, -0.1}, {0.5, 0.3}}, 0.05}, ground);
    CHECK(crossing.axisCrosses);
    CHECK(crossing.gap == doctest::Approx(-0.05));
    CHECK(crossing.witness.normal.y == doctest::Approx(1.0));
}

TEST_CASE("Capsule and convex polygon: distance and penetration against the direction search")
{
    std::mt19937_64 rng(11);
    std::uniform_real_distribution<double> u(-1.5, 1.5);
    std::uniform_real_distribution<double> r(0.0, 0.1);
    int crossing = 0;
    int clear = 0;
    for (int k = 0; k < 3000; ++k)
    {
        const Quad p = randomQuad(rng);
        Segment<double, 2> axis{{u(rng), u(rng)}, {u(rng), u(rng)}};
        if (k % 9 == 0)
        {
            axis.b = axis.a; // a disc
        }
        if (k % 11 == 0)
        {
            // Parallel to an edge, just outside it: the tie between an end and a vertex.
            const Vec2d e = p.vertices[1] - p.vertices[0];
            axis.a = p.vertices[0] + e * (u(rng) * 0.6) + p.normals[0] * 0.05;
            axis.b = axis.a + e * 0.5;
        }
        const double radius = r(rng);
        const auto c = contact(Capsule<double, 2>{axis, radius}, p);
        const auto reference = static_cast<double>(signedDistanceReference(axis, p));
        CHECK(std::abs(c.axisDistance - reference) < 1e-9);
        CHECK(c.gap == c.axisDistance - radius);
        (c.axisCrosses ? crossing : clear)++;
        CHECK(lengthSquared(c.witness.normal) == doctest::Approx(1.0));
        if (!c.axisCrosses)
        {
            // The witness pair realises the distance, and the normal is the true direction (or the face normal).
            CHECK(length(c.axisPoint - c.witness.point) == doctest::Approx(c.axisDistance).epsilon(1e-12));
            const Vec2d direction = (c.axisPoint - c.witness.point) / c.axisDistance;
            CHECK(dot(direction, c.witness.normal) > 1.0 - 1e-12);
            if (c.witness.kind == WitnessKind::Vertex)
            {
                CHECK(c.witness.point == p.vertices[c.witness.index]);
                CHECK(vertexNormalCone(p, c.witness.index).contains(c.witness.normal, 1e-12));
            }
            else
            {
                CHECK(c.witness.normal == p.normals[c.witness.index]);
            }
        }
    }
    CHECK(crossing > 300);
    CHECK(clear > 1000);
}

TEST_CASE("Capsule and slab: square-edge corners give vertex witnesses inside the cone, never averaged")
{
    // A 40 mm square block's left top corner: the casing edge approaches diagonally.
    const Quad block = slab(0.6, 0.04, 0.9, 0.04, -1.0);
    const Capsule<double, 2> edge{{{0.58, 0.05}, {0.585, 0.065}}, 0.008};
    const auto c = contact(edge, block);
    CHECK(c.witness.kind == WitnessKind::Vertex);
    CHECK(c.witness.index == 3U);
    CHECK(c.witness.point == Point2d{0.6, 0.04});
    const NormalCone<double> cone = vertexNormalCone(block, 3);
    CHECK(cone.from.y == doctest::Approx(1.0)); // the top, then counter-clockwise to the left face
    CHECK(cone.to.x == doctest::Approx(-1.0));
    CHECK(cone.contains(c.witness.normal));
    CHECK(c.witness.normal.x < -0.1);
    CHECK(c.witness.normal.y > 0.1);
    // The face beside it: the face normal exactly.
    const auto face = contact(Capsule<double, 2>{{{0.55, 0.0}, {0.55, 0.03}}, 0.008}, block);
    CHECK(face.witness.kind == WitnessKind::Edge);
    CHECK(face.witness.index == 3U);
    CHECK(face.witness.normal == Vec2d{-1.0, 0.0});
    CHECK(face.gap == doctest::Approx(0.05 - 0.008));
    // A cone that excludes a direction says so.
    CHECK_FALSE(cone.contains(Vec2d{1.0, 0.0}));
    CHECK_FALSE(cone.contains(Vec2d{0.0, -1.0}));
}

TEST_CASE("Capsule and polygon: float coordinates compile and agree")
{
    const auto pf = ConvexPolygon<float, 4>::fromVertices(
        {Point2f{0.0F, -1.0F}, Point2f{1.0F, -1.0F}, Point2f{1.0F, 0.0F}, Point2f{0.0F, 0.0F}});
    const auto c = contact(Capsule<float, 2>{{{0.5F, 0.5F}, {0.7F, 0.25F}}, 0.05F}, pf);
    CHECK(c.gap == doctest::Approx(0.2).epsilon(1e-6));
    CHECK(c.witness.kind == WitnessKind::Edge);
    CHECK(c.witness.index == 2U);
}
