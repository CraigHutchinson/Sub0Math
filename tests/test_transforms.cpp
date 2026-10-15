#include <doctest.h>

#include "sub0math/geometry.hpp"
#include "sub0math/linear.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;
using namespace sub0math::f64::literals;

namespace
{

void checkNear(const Vec3d& a, const Vec3d& b)
{
    CHECK(a.x == doctest::Approx(b.x).scale(1.0));
    CHECK(a.y == doctest::Approx(b.y).scale(1.0));
    CHECK(a.z == doctest::Approx(b.z).scale(1.0));
}

} // namespace

TEST_CASE("Quaternion rotation agrees with its matrix")
{
    const Quatd q = Quatd::fromAxisAngle({0.0, 0.0, 1.0}, 90_deg);
    checkNear(rotate(q, Vec3d{1.0, 0.0, 0.0}), {0.0, 1.0, 0.0});
    checkNear(toMat3(q) * Vec3d{1.0, 2.0, 3.0}, rotate(q, Vec3d{1.0, 2.0, 3.0}));

    const Quatd r = Quatd::fromAxisAngle(normalized(Vec3d{1.0, 1.0, 1.0}), 120_deg);
    checkNear(rotate(r, Vec3d{1.0, 0.0, 0.0}), {0.0, 1.0, 0.0});
    checkNear(rotate(r * q, Vec3d{0.3, -2.0, 1.0}), rotate(r, rotate(q, Vec3d{0.3, -2.0, 1.0})));
    checkNear(rotate(conjugate(r), rotate(r, Vec3d{0.3, -2.0, 1.0})), {0.3, -2.0, 1.0});
    CHECK(normalized(Quatd{2.0, 0.0, 0.0, 0.0}) == Quatd{});
}

TEST_CASE("Rotations keep quantity components")
{
    const Quatd q = Quatd::fromAxisAngle({0.0, 0.0, 1.0}, 90_deg);
    const Vec3<f64::Force> f{newtons(10.0), newtons(0.0), newtons(0.0)};
    const Vec3<f64::Force> turned = rotate(q, f);
    CHECK(turned.y.in(newtons) == doctest::Approx(10.0));
}

TEST_CASE("Rigid transforms move points, rotate vectors, compose and invert")
{
    using Pose = RigidTransform<double, f64::Length>;
    const Pose pose{Quatd::fromAxisAngle({0.0, 0.0, 1.0}, 90_deg), {1_m, 0_m, 0_m}};
    const Point3<f64::Length> p{1_m, 0_m, 0_m};
    const Point3<f64::Length> moved = pose.apply(p);
    CHECK(moved.x.si() == doctest::Approx(1.0));
    CHECK(moved.y.si() == doctest::Approx(1.0));

    const Vec3<f64::Length> v{1_m, 0_m, 0_m};
    CHECK(pose.applyToVector(v).y.si() == doctest::Approx(1.0)); // no translation for vectors

    const Point3<f64::Length> back = inverse(pose).apply(moved);
    CHECK(back.x.si() == doctest::Approx(1.0));
    CHECK(back.y.si() == doctest::Approx(0.0).scale(1.0));

    const Point3<f64::Length> twice = (pose * pose).apply(p);
    const Point3<f64::Length> stepwise = pose.apply(pose.apply(p));
    CHECK(twice.x.si() == doctest::Approx(stepwise.x.si()).scale(1.0));
    CHECK(twice.y.si() == doctest::Approx(stepwise.y.si()).scale(1.0));
}
