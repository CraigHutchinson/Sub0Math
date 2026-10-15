#include <concepts>
#include <doctest.h>
#include <type_traits>

#include "sub0math/linear.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;

// Layout matches graphics vectors; aggregates with named components.
static_assert(sizeof(Vec3f) == 3 * sizeof(float));
static_assert(sizeof(Vec4d) == 4 * sizeof(double));
static_assert(sizeof(Vec3<f64::Length>) == 3 * sizeof(double));
static_assert(std::is_trivially_copyable_v<Vec3<f64::Velocity>>);
static_assert(std::is_aggregate_v<Vec3f>);
static_assert(sizeof(Mat4f) == 16 * sizeof(float));

// Dimensions flow through vector maths.
static_assert(std::same_as<decltype(Vec3<f64::Velocity>{} * f64::Time{}), Vec3<f64::Length>>);
static_assert(std::same_as<decltype(dot(Vec3<f64::Length>{}, Vec3<f64::Length>{})), f64::Area>);
static_assert(std::same_as<decltype(cross(Vec3<f64::Length>{}, Vec3<f64::Force>{})), Vec3<f64::Energy>>);
static_assert(std::same_as<decltype(Vec3f{} * 2.0), Vec3f>); // a float vector stays float

TEST_CASE("Vector arithmetic")
{
    constexpr Vec3d a{1.0, 2.0, 3.0};
    constexpr Vec3d b{4.0, 5.0, 6.0};
    static_assert(a + b == Vec3d{5.0, 7.0, 9.0});
    static_assert(b - a == Vec3d{3.0, 3.0, 3.0});
    static_assert(-a == Vec3d{-1.0, -2.0, -3.0});
    static_assert(a * 2 == Vec3d{2.0, 4.0, 6.0});
    static_assert(dot(a, b) == 32.0);
    static_assert(cross(Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0}) == Vec3d{0.0, 0.0, 1.0});
    CHECK(length(Vec2d{3.0, 4.0}) == 5.0);
    const Vec2d n = normalized(Vec2d{3.0, 4.0});
    CHECK(n.x == doctest::Approx(0.6));
    CHECK(a[2] == 3.0);
    CHECK((a / 2.0).z == 1.5);

    Vec<double, 5> wide{};
    wide[4] = 2.0;
    CHECK(length(wide) == 2.0);
}

TEST_CASE("Vectors of quantities")
{
    using namespace sub0math::f64::literals;
    const Vec3<f64::Velocity> v{metresPerSecond(3.0), metresPerSecond(4.0), metresPerSecond(0.0)};
    const Vec3<f64::Length> d = v * 2_s;
    CHECK(d.y == 8_m);
    CHECK(length(d) == 10_m);
    const Vec3d direction = normalized(d);
    CHECK(direction.x == doctest::Approx(0.6));
    const Vec3<f64::Velocity> back = d / 2_s;
    CHECK(back == v);
}

TEST_CASE("Matrices are column-major")
{
    Mat3d m = Mat3d::identity();
    m(0, 2) = 5.0; // row 0, column 2
    CHECK(m.columns[2].x == 5.0);
    const Vec3d moved = m * Vec3d{0.0, 0.0, 1.0};
    CHECK(moved == Vec3d{5.0, 0.0, 1.0});
    CHECK(transpose(m)(2, 0) == 5.0);
    CHECK(m * Mat3d::identity() == m);
}
