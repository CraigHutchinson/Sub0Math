#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <doctest.h>
#include <random>
#include <type_traits>

#include "sub0math/linear.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;
using namespace sub0math::f64::literals;

namespace
{

template<typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };
template<typename A, typename B>
concept Dottable = requires(A a, B b) { dot(a, b); };
template<typename A, typename B>
concept Crossable = requires(A a, B b) { cross(a, b); };
template<typename A, typename B>
concept Multipliable = requires(A a, B b) { a * b; };

} // namespace

// Motion and force are distinct: they do not mix, and the only product between them is the power.
static_assert(!Addable<SpatialMotiond, SpatialForced>);
static_assert(!Dottable<SpatialMotiond, SpatialMotiond>);
static_assert(!Dottable<SpatialForced, SpatialForced>);
static_assert(Dottable<SpatialMotiond, SpatialForced> && Dottable<SpatialForced, SpatialMotiond>);
static_assert(!Crossable<SpatialForced, SpatialForced>);
static_assert(!Crossable<SpatialForced, SpatialMotiond>);
static_assert(!Multipliable<SpatialInertiad, SpatialForced>);
static_assert(std::same_as<decltype(dot(SpatialMotiond{}, SpatialForced{})), double>);
static_assert(std::same_as<decltype(SpatialInertiad{} * SpatialMotiond{}), SpatialForced>);
static_assert(std::same_as<decltype(PluckerTransformd{}.apply(SpatialForced{})), SpatialForced>);
static_assert(std::is_aggregate_v<SpatialMotiond>);
static_assert(std::is_aggregate_v<PluckerTransformd>);

namespace
{

// Independent reference: Featherstone's 6x6 matrices, written out element by element.
using Mat6 = std::array<std::array<double, 6>, 6>;
using Vec6 = std::array<double, 6>;

Mat3d skew(const Vec3d& a)
{
    Mat3d s{};
    s(0, 1) = -a.z;
    s(0, 2) = a.y;
    s(1, 0) = a.z;
    s(1, 2) = -a.x;
    s(2, 0) = -a.y;
    s(2, 1) = a.x;
    return s;
}

// [[a, b], [c, d]] from 3x3 blocks.
Mat6 blocks(const Mat3d& a, const Mat3d& b, const Mat3d& c, const Mat3d& d)
{
    Mat6 m{};
    for (std::size_t r = 0; r < 3; ++r)
    {
        for (std::size_t k = 0; k < 3; ++k)
        {
            m[r][k] = a(r, k);
            m[r][k + 3] = b(r, k);
            m[r + 3][k] = c(r, k);
            m[r + 3][k + 3] = d(r, k);
        }
    }
    return m;
}

Mat6 multiply(const Mat6& a, const Mat6& b)
{
    Mat6 m{};
    for (std::size_t r = 0; r < 6; ++r)
    {
        for (std::size_t c = 0; c < 6; ++c)
        {
            for (std::size_t k = 0; k < 6; ++k)
            {
                m[r][c] += a[r][k] * b[k][c];
            }
        }
    }
    return m;
}

Mat6 transposed(const Mat6& a)
{
    Mat6 m{};
    for (std::size_t r = 0; r < 6; ++r)
    {
        for (std::size_t c = 0; c < 6; ++c)
        {
            m[r][c] = a[c][r];
        }
    }
    return m;
}

Vec6 multiply(const Mat6& a, const Vec6& v)
{
    Vec6 out{};
    for (std::size_t r = 0; r < 6; ++r)
    {
        for (std::size_t k = 0; k < 6; ++k)
        {
            out[r] += a[r][k] * v[k];
        }
    }
    return out;
}

Mat3d scaled(const Mat3d& m, double s)
{
    Mat3d out = m;
    for (auto& column : out.columns)
    {
        column = column * s;
    }
    return out;
}

Vec6 flat(const SpatialMotiond& m)
{
    return {m.angular.x, m.angular.y, m.angular.z, m.linear.x, m.linear.y, m.linear.z};
}

Vec6 flat(const SpatialForced& f)
{
    return {f.angular.x, f.angular.y, f.angular.z, f.linear.x, f.linear.y, f.linear.z};
}

Mat6 matrixOf(const PluckerTransformd& x) // motion transform [[E, 0], [-E r x, E]]
{
    return blocks(x.rotation, Mat3d{}, scaled(x.rotation * skew(x.translation), -1.0), x.rotation);
}

Mat6 forceMatrixOf(const PluckerTransformd& x) // [[E, -E r x], [0, E]]
{
    return blocks(x.rotation, scaled(x.rotation * skew(x.translation), -1.0), Mat3d{}, x.rotation);
}

Mat6 matrixOf(const SpatialInertiad& i) // [[I_o, h x], [-h x, m 1]]
{
    return blocks(i.rotational, skew(i.firstMoment), scaled(skew(i.firstMoment), -1.0),
                  scaled(Mat3d::identity(), i.mass));
}

void checkNear(const Vec6& a, const Vec6& b)
{
    for (std::size_t k = 0; k < 6; ++k)
    {
        CHECK(a[k] == doctest::Approx(b[k]).epsilon(1e-12).scale(1.0));
    }
}

void checkNear(const Mat6& a, const Mat6& b)
{
    for (std::size_t r = 0; r < 6; ++r)
    {
        checkNear(a[r], b[r]);
    }
}

const SpatialMotiond motion{{0.3, -1.2, 0.7}, {2.0, 0.5, -0.4}};
const SpatialMotiond velocity{{-0.8, 0.4, 1.1}, {0.2, -1.5, 0.9}};
const SpatialForced force{{1.5, 0.25, -2.0}, {-0.6, 3.0, 1.2}};

PluckerTransformd sampleTransform()
{
    const RigidTransformd pose{Quatd::fromAxisAngle(normalized(Vec3d{1.0, -2.0, 0.5}), 0.9_rad), {0.4, -0.3, 1.7}};
    return PluckerTransformd::fromPose(pose);
}

SpatialInertiad sampleInertia()
{
    Mat3d inertiaAboutCentre{};
    inertiaAboutCentre(0, 0) = 0.30;
    inertiaAboutCentre(1, 1) = 0.25;
    inertiaAboutCentre(2, 2) = 0.40;
    inertiaAboutCentre(0, 1) = inertiaAboutCentre(1, 0) = 0.02;
    inertiaAboutCentre(1, 2) = inertiaAboutCentre(2, 1) = -0.03;
    return SpatialInertiad::fromCentreOfMass(2.5, {0.1, -0.4, 0.25}, inertiaAboutCentre);
}

} // namespace

TEST_CASE("Spatial cross products match Featherstone's crm and crf")
{
    const Mat3d w = skew(velocity.angular);
    const Mat6 crm = blocks(w, Mat3d{}, skew(velocity.linear), w);
    checkNear(flat(cross(velocity, motion)), multiply(crm, flat(motion)));

    Mat6 crf = transposed(crm);
    for (auto& row : crf)
    {
        for (double& e : row)
        {
            e = -e;
        }
    }
    checkNear(flat(cross(velocity, force)), multiply(crf, flat(force)));
    CHECK(dot(motion, cross(velocity, force)) == doctest::Approx(-dot(cross(velocity, motion), force)));
}

TEST_CASE("Plücker transforms act on motion as X and on force as X*, in both directions")
{
    const PluckerTransformd x = sampleTransform();
    const Mat6 xm = matrixOf(x);
    const Mat6 xf = forceMatrixOf(x);
    checkNear(flat(x.apply(motion)), multiply(xm, flat(motion)));
    checkNear(flat(x.apply(force)), multiply(xf, flat(force)));

    // X* = X^-T, so the transpose of X is the inverse force transform.
    checkNear(flat(x.applyInverse(force)), multiply(transposed(xm), flat(force)));
    checkNear(flat(x.applyInverse(x.apply(motion))), flat(motion));
    checkNear(flat(inverse(x).apply(motion)), flat(x.applyInverse(motion)));
    checkNear(flat(inverse(x).apply(force)), flat(x.applyInverse(force)));

    // Power does not depend on the frame it is computed in.
    CHECK(dot(x.apply(motion), x.apply(force)) == doctest::Approx(dot(motion, force)));
}

TEST_CASE("Plücker transforms from poses describe the moving frame")
{
    // A body moving with `motion` in A; frame B sits at `pose` in A. In B, the linear part is the
    // velocity of the body point at B's origin, in B's axes.
    const RigidTransformd pose{Quatd::fromAxisAngle({0.0, 0.0, 1.0}, 90_deg), {1.0, 2.0, 0.0}};
    const PluckerTransformd x = PluckerTransformd::fromPose(pose);
    const Vec3d pointVelocityInA = motion.linear + cross(motion.angular, pose.translation);
    const Vec3d expected = rotate(conjugate(pose.rotation), pointVelocityInA);
    const SpatialMotiond inB = x.apply(motion);
    CHECK(inB.linear.x == doctest::Approx(expected.x));
    CHECK(inB.linear.y == doctest::Approx(expected.y));
    CHECK(inB.linear.z == doctest::Approx(expected.z));

    const RigidTransform<double, f64::Length> typed{pose.rotation, {1_m, 2_m, 0_m}};
    CHECK(PluckerTransformd::fromPose(typed) == x);
}

TEST_CASE("Plücker transforms compose and invert like the frames they describe")
{
    const PluckerTransformd ab = sampleTransform();
    const PluckerTransformd bc = PluckerTransformd::fromPose(
        RigidTransformd{Quatd::fromAxisAngle(normalized(Vec3d{0.0, 1.0, 1.0}), -0.4_rad), {-0.2, 0.9, 0.3}});
    checkNear(multiply(matrixOf(bc * ab), flat(motion)), multiply(multiply(matrixOf(bc), matrixOf(ab)), flat(motion)));
    checkNear(flat((bc * ab).apply(force)), flat(bc.apply(ab.apply(force))));
    checkNear(flat((inverse(ab) * ab).apply(motion)), flat(motion));
}

TEST_CASE("Spatial inertia: parallel axis, action on motion and change of frame")
{
    const SpatialInertiad inertia = sampleInertia();
    checkNear(flat(inertia * motion), multiply(matrixOf(inertia), flat(motion)));
    const Vec3d c = inertia.centreOfMass();
    CHECK(c.y == doctest::Approx(-0.4));

    // Kinetic energy 1/2 v.Iv is positive and the matrix is symmetric.
    CHECK(dot(velocity, inertia * velocity) > 0.0);
    const Mat6 im = matrixOf(inertia);
    checkNear(im, transposed(im));

    // X^T I X, compared with the 6x6 product.
    const PluckerTransformd x = sampleTransform();
    const SpatialInertiad inA = x.applyInverse(inertia);
    checkNear(matrixOf(inA), multiply(transposed(matrixOf(x)), multiply(im, matrixOf(x))));
    CHECK(inA.mass == inertia.mass);

    // Sums are component-wise: two halves make the whole.
    const SpatialInertiad half = SpatialInertiad::fromCentreOfMass(1.25, c, Mat3d{});
    const SpatialInertiad whole = SpatialInertiad::fromCentreOfMass(2.5, c, Mat3d{});
    checkNear(matrixOf(half + half), matrixOf(whole));
}

namespace
{

double rotationDifference(const Mat3d& a, const Mat3d& b)
{
    double worst = 0.0;
    for (std::size_t r = 0; r < 3; ++r)
    {
        for (std::size_t c = 0; c < 3; ++c) worst = std::max(worst, std::abs(a(r, c) - b(r, c)));
    }
    return worst;
}

double norm(const Quatd& q)
{
    return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
}

} // namespace

TEST_CASE("Quaternion from rotation matrix: random rotations and every branch of Shepperd's method")
{
    std::mt19937_64 rng(20261020);
    std::normal_distribution<double> gauss;
    std::size_t branches[4] = {};
    for (int n = 0; n < 20000; ++n)
    {
        Quatd q{gauss(rng), gauss(rng), gauss(rng), gauss(rng)};
        const double len = norm(q);
        q = {q.w / len, q.x / len, q.y / len, q.z / len};
        const Mat3d m = toMat3(q);
        const Quatd back = Quatd::fromRotationMatrix(m);
        CHECK(rotationDifference(toMat3(back), m) < 1e-14);
        CHECK(std::abs(norm(back) - 1.0) < 1e-14);
        CHECK(back.w >= 0.0);
        // The same rotation as q: equal up to sign.
        const double sign = q.w < 0.0 ? -1.0 : 1.0;
        CHECK(std::abs(back.w - sign * q.w) < 1e-12);
        CHECK(std::abs(back.x - sign * q.x) < 1e-12);
        CHECK(std::abs(back.y - sign * q.y) < 1e-12);
        CHECK(std::abs(back.z - sign * q.z) < 1e-12);
        const double tr = m(0, 0) + m(1, 1) + m(2, 2);
        if (tr >= m(0, 0) && tr >= m(1, 1) && tr >= m(2, 2)) ++branches[0];
        else if (m(0, 0) >= m(1, 1) && m(0, 0) >= m(2, 2)) ++branches[1];
        else if (m(1, 1) >= m(2, 2)) ++branches[2];
        else ++branches[3];
    }
    for (const std::size_t count : branches) CHECK(count > 100); // all four branches were exercised
}

TEST_CASE("Quaternion from rotation matrix: identity, half turns and tiny angles")
{
    const Quatd identity = Quatd::fromRotationMatrix(Mat3d::identity());
    CHECK(identity == Quatd{1.0, 0.0, 0.0, 0.0});

    for (const Vec3d& axis : {Vec3d{1.0, 0.0, 0.0}, Vec3d{0.0, 1.0, 0.0}, Vec3d{0.0, 0.0, 1.0},
                              normalized(Vec3d{1.0, 1.0, 0.0}), normalized(Vec3d{-1.0, 2.0, 3.0})})
    {
        const Quatd half = Quatd::fromAxisAngle(axis, f64::Angle::fromSi(3.14159265358979323846));
        const Quatd back = Quatd::fromRotationMatrix(toMat3(half));
        CHECK(rotationDifference(toMat3(back), toMat3(half)) < 1e-14);
        CHECK(std::abs(norm(back) - 1.0) < 1e-14);
        for (const double angle : {1e-9, -1e-9, 1e-4, 3.14159265, 3.14159265358979 - 1e-9})
        {
            const Quatd q = Quatd::fromAxisAngle(axis, f64::Angle::fromSi(angle));
            CHECK(rotationDifference(toMat3(Quatd::fromRotationMatrix(toMat3(q))), toMat3(q)) < 1e-14);
        }
    }

    // Float works the same way, to float precision.
    const Quatf qf = Quatf::fromAxisAngle({0.0F, 0.0F, 1.0F}, f32::Angle::fromSi(2.0F));
    const Quatf backf = Quatf::fromRotationMatrix(toMat3(qf));
    CHECK(backf.w == doctest::Approx(qf.w).epsilon(1e-5));
    CHECK(backf.z == doctest::Approx(qf.z).epsilon(1e-5));
}

TEST_CASE("toRigidTransform undoes PluckerTransform::fromPose")
{
    std::mt19937_64 rng(7);
    std::normal_distribution<double> gauss;
    for (int n = 0; n < 5000; ++n)
    {
        Quatd q{gauss(rng), gauss(rng), gauss(rng), gauss(rng)};
        const double len = norm(q);
        q = {q.w / len, q.x / len, q.y / len, q.z / len};
        const Vec3d t{gauss(rng), gauss(rng), gauss(rng)};
        const PluckerTransformd x = PluckerTransformd::fromPose(RigidTransformd{q, t});

        const RigidTransformd pose = toRigidTransform(x);
        CHECK(pose.translation == t);
        CHECK(rotationDifference(toMat3(pose.rotation), toMat3(q)) < 1e-14);
        const PluckerTransformd again = PluckerTransformd::fromPose(pose);
        CHECK(rotationDifference(again.rotation, x.rotation) < 1e-14);
        CHECK(again.translation == x.translation);

        // With typed positions the translation comes back as lengths.
        const RigidTransform<double, f64::Length> typed = toRigidTransform<f64::Length>(x);
        CHECK(toSi(typed.translation) == t);
        CHECK(PluckerTransformd::fromPose(typed).translation == x.translation);
    }
    static_assert(std::same_as<decltype(toRigidTransform(PluckerTransformd{})), RigidTransformd>);
    static_assert(std::same_as<decltype(toRigidTransform<f64::Length>(PluckerTransformd{})),
                               RigidTransform<double, f64::Length>>);
}
