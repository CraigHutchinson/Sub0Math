#include <concepts>
#include <doctest.h>

#include "sub0math/geometry.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;

namespace
{

template<typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };

} // namespace

// Affine rules: positions and displacements are different things.
static_assert(!Addable<Point2d, Point2d>);
static_assert(Addable<Point2d, Vec2d>);
static_assert(std::same_as<decltype(Point2d{} - Point2d{}), Vec2d>);
static_assert(sizeof(Point2d) == 2 * sizeof(double));

TEST_CASE("Points and displacements")
{
    constexpr Point2d a{1.0, 2.0};
    constexpr Point2d b{4.0, 6.0};
    static_assert(b - a == Vec2d{3.0, 4.0});
    static_assert(a + Vec2d{3.0, 4.0} == b);
    static_assert(Point2d::origin() == Point2d{0.0, 0.0});
    CHECK(a.x == 1.0);
    CHECK(a[1] == 2.0);
}

TEST_CASE("Axis-aligned boxes")
{
    using namespace sub0math::f64::literals;
    const auto box = Aabb<f64::Length, 2>::fromCorners({2_m, 0_m}, {0_m, 1_m});
    CHECK(box.lo == Point2<f64::Length>{0_m, 0_m});
    CHECK(box.size() == Vec2<f64::Length>{2_m, 1_m});
    CHECK(box.contains({1_m, 500_mm}));
    CHECK_FALSE(box.contains({3_m, 500_mm}));
    CHECK(box.intersects(Aabb<f64::Length, 2>::fromCorners({2_m, 1_m}, {3_m, 3_m})));
    CHECK_FALSE(box.intersects(Aabb<f64::Length, 2>::fromCorners({2.1_m, 0_m}, {3_m, 3_m})));
    CHECK(box.including({-1_m, 0_m}).lo.x == -1_m);
}

TEST_CASE("Spheres and circles")
{
    using namespace sub0math::f64::literals;
    constexpr Circle<double> unit{{0.0, 0.0}, 1.0};
    static_assert(contains(unit, Point2d{0.6, 0.8}));
    static_assert(!contains(unit, Point2d{0.8, 0.8}));
    static_assert(boundingBox(unit) == Aabb<double, 2>{{-1.0, -1.0}, {1.0, 1.0}});

    const Sphere<f64::Length, 3> wheel{{0_m, 0_m, 360_mm}, 360_mm};
    CHECK(contains(wheel, Point3<f64::Length>{0_m, 0_m, 0_m}));
    CHECK(boundingBox(wheel).hi.z == 720_mm);
}

TEST_CASE("Closest point on a segment, with exact end parameters")
{
    constexpr Segment<double, 2> s{{0.0, 0.0}, {2.0, 0.0}};
    constexpr auto mid = closestPoint(s, Point2d{0.5, 1.0});
    static_assert(mid.point == Point2d{0.5, 0.0} && mid.t == 0.25);
    static_assert(closestPoint(s, Point2d{-3.0, 1.0}).t == 0.0); // exactly the start vertex
    static_assert(closestPoint(s, Point2d{9.0, -1.0}).t == 1.0); // exactly the end vertex
    static_assert(closestPoint(s, Point2d{9.0, -1.0}).point == s.b);
    constexpr Segment<double, 2> degenerate{{1.0, 1.0}, {1.0, 1.0}}; // zero length: a point
    static_assert(closestPoint(degenerate, Point2d{5.0, 5.0}).point == degenerate.a && closestPoint(degenerate, Point2d{5.0, 5.0}).t == 0.0);

    using namespace sub0math::f64::literals;
    const Segment<f64::Length, 3> lever{{0_m, 0_m, 0_m}, {0_m, 0_m, 2_m}};
    const auto near = closestPoint(lever, Point3<f64::Length>{1_m, 0_m, 1_m});
    CHECK(near.point.z == 1_m);
    CHECK(boundingBox(lever).hi.z == 2_m);
}
