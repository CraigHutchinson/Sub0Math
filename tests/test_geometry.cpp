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
