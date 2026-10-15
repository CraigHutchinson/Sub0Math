#include <concepts>
#include <doctest.h>
#include <type_traits>

#include "sub0math/units.hpp"

using namespace sub0math;

namespace
{

template<typename A, typename B>
concept Addable = requires(A a, B b) { a + b; };

template<typename A, typename B>
concept Comparable = requires(A a, B b) { a < b; };

template<typename To, typename From>
concept ImplicitlyConvertible = std::is_convertible_v<From, To>;

template<typename Q, typename U>
concept ReadableIn = requires(Q q, U u) { q.in(u); };

} // namespace

// Dimension errors do not compile.
static_assert(Addable<Length, Length>);
static_assert(!Addable<Length, Time>);
static_assert(!Addable<Length, float>);
static_assert(!Comparable<Mass, Length>);
static_assert(!ImplicitlyConvertible<Length, float>);
static_assert(!ImplicitlyConvertible<float, Length>);
static_assert(!std::constructible_from<Length, float>);
static_assert(!ReadableIn<Length, decltype(seconds)>);
static_assert(ReadableIn<Length, decltype(millimetres)>);

// Dimensions combine.
static_assert(std::same_as<decltype(Length{} / Time{}), Velocity>);
static_assert(std::same_as<decltype(Mass{} * Acceleration{}), Force>);
static_assert(std::same_as<decltype(Force{} * Length{}), Energy>);
static_assert(std::same_as<decltype(Energy{} / Time{}), Power>);
static_assert(std::same_as<decltype(Force{} / Length{}), Stiffness>);
static_assert(std::same_as<decltype(Length{} / Length{}), Dimensionless>);
static_assert(std::same_as<decltype(1.0f / Time{}), Frequency>);

// Precision: float widens implicitly to double; double narrows only explicitly.
static_assert(ImplicitlyConvertible<f64::Length, f32::Length>);
static_assert(!ImplicitlyConvertible<f32::Length, f64::Length>);
static_assert(std::constructible_from<f32::Length, f64::Length>);
static_assert(!Addable<f64::Length, f32::Time>);

// Zero overhead.
static_assert(sizeof(Length) == sizeof(float));
static_assert(sizeof(f64::Energy) == sizeof(double));
static_assert(std::is_trivially_copyable_v<f64::Length>);
static_assert(std::is_standard_layout_v<Length>);

// Usable at compile time.
static_assert(metres(2.0f) + millimetres(500.0f) == metres(2.5f));
static_assert((kilometres(3.6) / hours(1.0)).si() == 1.0);

TEST_CASE("Quantities store SI and convert at the edges")
{
    const f64::Length l = millimetres(350.0);
    CHECK(l.si() == doctest::Approx(0.35));
    CHECK(l.in(millimetres) == doctest::Approx(350.0));
    CHECK(l.in(centimetres) == doctest::Approx(35.0));
    CHECK(f64::Length::fromSi(0.35) == l);

    const f64::Velocity v = kilometresPerHour(36.0);
    CHECK(v.in(metresPerSecond) == doctest::Approx(10.0));
    CHECK(newtonsPerMillimetre(9.0).si() == doctest::Approx(9000.0));
    CHECK(revolutionsPerMinute(60.0).in(radiansPerSecond) == doctest::Approx(2.0 * 3.141592653589793));
}

TEST_CASE("Arithmetic follows the dimensions")
{
    const f64::Mass m = kilograms(45.0);
    const f64::Acceleration g = standardGravities(1.0);
    const f64::Force weight = m * g;
    CHECK(weight.in(newtons) == doctest::Approx(441.29925));

    const f64::Stiffness k = newtonsPerMetre(9000.0);
    const f64::Length sag = weight / k;
    CHECK(sag.in(millimetres) == doctest::Approx(49.033).epsilon(1e-4));

    f64::Energy e = 0.5 * k * sag * sag;
    e += joules(1.0);
    CHECK(e.si() == doctest::Approx(0.5 * 9000.0 * sag.si() * sag.si() + 1.0));

    const double ratio = metres(3.0) / metres(1.5);
    CHECK(ratio == 2.0);
    CHECK(-metres(1.0) < metres(0.0));
}

TEST_CASE("Mixed precision widens, never narrows silently")
{
    const f32::Length coarse = metres(0.1f);
    const f64::Length fine = metres(0.1);
    const f64::Length sum = fine + coarse; // float operand widens to double
    CHECK(sum.si() == doctest::Approx(0.2).epsilon(1e-7));
    const f32::Length narrowed{fine}; // explicit only
    CHECK(narrowed.si() == 0.1f);
}

TEST_CASE("Dimensionless quantities read as plain numbers; percent is a unit")
{
    const Dimensionless slip = percent(12.5f);
    const float asNumber = slip;
    CHECK(asNumber == doctest::Approx(0.125f));
}
