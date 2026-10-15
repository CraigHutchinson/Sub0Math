#define ANKERL_NANOBENCH_IMPLEMENT
#include <algorithm>
#include <bench_runner.hpp>
#include <cstddef>
#include <vector>

#include "sub0math/sub0math.hpp"

using namespace sub0math;

namespace
{

constexpr std::size_t Count = 1024;

// Each pair runs the same arithmetic on plain numbers and on quantities: the quantity version must be
// as fast, because the dimension checks happen at compile time.

template<typename T>
std::vector<Vec3<T>> makeVectors(T scale)
{
    std::vector<Vec3<T>> v(Count);
    for (std::size_t i = 0; i < Count; ++i)
    {
        const auto f = static_cast<ScalarOf<T>>(i % 17) * static_cast<ScalarOf<T>>(0.25);
        v[i] = Vec3<T>{scale * f, scale * (f + ScalarOf<T>{1}), scale * (f - ScalarOf<T>{2})};
    }
    return v;
}

template<typename T>
void benchDot(sub0bench::BenchRunner& runner, const char* name, T scale)
{
    const auto a = makeVectors(scale);
    const auto b = makeVectors(scale);
    runner.run(name, [&] {
        decltype(dot(a[0], b[0])) sum{};
        for (std::size_t i = 0; i < Count; ++i)
        {
            sum += dot(a[i], b[i]);
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

template<typename T>
void benchAxpy(sub0bench::BenchRunner& runner, const char* name, T scale)
{
    auto x = makeVectors(scale);
    const auto v = makeVectors(scale);
    runner.run(name, [&] {
        for (std::size_t i = 0; i < Count; ++i)
        {
            x[i] += v[i] * static_cast<ScalarOf<T>>(0.0005);
        }
        ankerl::nanobench::doNotOptimizeAway(x.data());
    });
}

template<Real R>
void benchRotate(sub0bench::BenchRunner& runner, const char* name)
{
    const Quat<R> q = Quat<R>::fromAxisAngle(normalized(Vec3<R>{R{1}, R{2}, R{3}}),
                                             Quantity<dim::Angle, R>::fromSi(R{0.7}));
    auto v = makeVectors(R{1});
    runner.run(name, [&] {
        for (auto& p : v)
        {
            p = rotate(q, p);
        }
        ankerl::nanobench::doNotOptimizeAway(v.data());
    });
}

template<Real R>
void benchMat4(sub0bench::BenchRunner& runner, const char* name)
{
    Mat4<R> m = Mat4<R>::identity();
    m(0, 3) = R{2};
    m(1, 0) = R{0.5};
    std::vector<Vec4<R>> v(Count, Vec4<R>{R{1}, R{2}, R{3}, R{1}});
    runner.run(name, [&] {
        for (auto& p : v)
        {
            p = m * p;
        }
        ankerl::nanobench::doNotOptimizeAway(v.data());
    });
}

// Nearest-point distance from points to segments, the kernel of a terrain clearance query: once on
// plain scalars written by hand, once through Point/Vec with quantity components. Their gap is the whole
// cost of the vocabulary.
void benchSegmentDistance(sub0bench::BenchRunner& runner)
{
    std::vector<double> xs(Count + 1);
    std::vector<double> ys(Count + 1);
    for (std::size_t i = 0; i <= Count; ++i)
    {
        xs[i] = static_cast<double>(i) * 0.01;
        ys[i] = 0.05 * static_cast<double>(i % 7);
    }
    const double cx = 3.0;
    const double cy = 0.4;
    runner.run("segment distance^2, hand-written scalars", [&] {
        double best = 1e300;
        for (std::size_t i = 0; i < Count; ++i)
        {
            const double abx = xs[i + 1] - xs[i];
            const double aby = ys[i + 1] - ys[i];
            const double t = std::clamp(((cx - xs[i]) * abx + (cy - ys[i]) * aby) / (abx * abx + aby * aby), 0.0, 1.0);
            const double dx = cx - (xs[i] + abx * t);
            const double dy = cy - (ys[i] + aby * t);
            best = std::min(best, dx * dx + dy * dy);
        }
        ankerl::nanobench::doNotOptimizeAway(best);
    });

    std::vector<Point2<f64::Length>> points(Count + 1);
    for (std::size_t i = 0; i <= Count; ++i)
    {
        points[i] = {f64::Length::fromSi(xs[i]), f64::Length::fromSi(ys[i])};
    }
    const Point2<f64::Length> c{f64::Length::fromSi(cx), f64::Length::fromSi(cy)};
    runner.run("segment distance^2, closestPoint(Segment<f64::Length>)", [&] {
        f64::Area best = f64::Area::fromSi(1e300);
        for (std::size_t i = 0; i < Count; ++i)
        {
            const auto nearest = closestPoint(Segment<f64::Length, 2>{points[i], points[i + 1]}, c);
            best = std::min(best, lengthSquared(c - nearest.point));
        }
        ankerl::nanobench::doNotOptimizeAway(best);
    });
}

} // namespace

int main(int argc, char** argv)
{
    sub0bench::BenchRunner runner(argc, argv, "Sub0Math, 1024 elements per op");
    runner.bench().unit("1024 elements").minEpochIterations(30000);

    benchDot(runner, "dot, double", 1.0);
    benchDot(runner, "dot, f64::Length", metres(1.0));
    benchDot(runner, "dot, float", 1.0f);
    benchDot(runner, "dot, f32::Length", metres(1.0f));

    benchAxpy(runner, "x += v*dt, double", 1.0);
    benchAxpy(runner, "x += v*dt, f64::Length", metres(1.0));
    benchAxpy(runner, "x += v*dt, float", 1.0f);

    benchRotate<double>(runner, "quaternion rotate, double");
    benchRotate<float>(runner, "quaternion rotate, float");

    benchMat4<double>(runner, "Mat4 * Vec4, double");
    benchMat4<float>(runner, "Mat4 * Vec4, float");

    benchSegmentDistance(runner);
    return runner.finish();
}
