#include <cmath>
#include <concepts>
#include <doctest.h>
#include <numbers>

#include "sub0math/number.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;

namespace
{

template<typename T>
concept HasSin = requires(T t) { sin(t); };

template<typename T>
concept HasSqrt = requires(T t) { sqrt(t); };

} // namespace

static_assert(HasSin<Angle>);
static_assert(!HasSin<Length>); // trigonometry takes angles only: no degrees-as-number mistakes
static_assert(HasSqrt<Area>);
static_assert(!HasSqrt<Length>); // no dimension has half a metre
static_assert(std::same_as<decltype(sqrt(Area{})), Length>);
static_assert(std::same_as<decltype(dropAngle(Angle{} * Length{})), Length>);

TEST_CASE("Float literals by default")
{
    using namespace sub0math::literals;
    static_assert(std::same_as<decltype(1.5_m), f32::Length>);
    static_assert(std::same_as<decltype(250_ms), f32::Time>);
    CHECK((250_ms).si() == doctest::Approx(0.25f));
    CHECK((2_km).in(metres) == doctest::Approx(2000.0f));
    CHECK((36_kmh).in(metresPerSecond) == doctest::Approx(10.0f));
    CHECK((180_deg).si() == doctest::Approx(std::numbers::pi_v<float>));
}

TEST_CASE("Double literals when chosen")
{
    using namespace sub0math::f64::literals;
    static_assert(std::same_as<decltype(0.1_m), f64::Length>);
    CHECK((0.1_m).si() == 0.1);
    CHECK((500_us).si() == doctest::Approx(0.0005));
}

TEST_CASE("Angles: trigonometry, inverse functions and arc length")
{
    using namespace sub0math::f64::literals;
    CHECK(sin(30_deg) == doctest::Approx(0.5));
    CHECK(cos(60_deg) == doctest::Approx(0.5));
    CHECK(atan2(metres(1.0), metres(1.0)).in(degrees) == doctest::Approx(45.0));
    CHECK(angleFromSine(0.5).in(degrees) == doctest::Approx(30.0));
    CHECK(angleFromCosine(0.5).in(degrees) == doctest::Approx(60.0));
    const f64::Length arc = dropAngle(90_deg * 2_m);
    CHECK(arc.si() == doctest::Approx(std::numbers::pi));
}

TEST_CASE("Helpers: abs, sqrt, finiteness and tolerance")
{
    using namespace sub0math::f64::literals;
    CHECK(abs(-3_m) == 3_m);
    CHECK(sqrt(metres(3.0) * metres(3.0) + metres(4.0) * metres(4.0)) == 5_m);
    CHECK(isFinite(1_m));
    CHECK_FALSE(isFinite(f64::Length::fromSi(std::nan(""))));
    CHECK(isNear(1000_mm, 1_m, 1e-9_m));
    CHECK_FALSE(isNear(1001_mm, 1_m, 0.5_mm));
    CHECK(isNear(1.0, 1.0 + 1e-12, 1e-9));
}
