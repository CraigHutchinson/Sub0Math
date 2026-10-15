#define ANKERL_NANOBENCH_IMPLEMENT
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
    return runner.finish();
}
