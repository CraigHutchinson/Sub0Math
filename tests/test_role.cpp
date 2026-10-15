#include <concepts>
#include <cstdint>
#include <doctest.h>
#include <type_traits>

#include "sub0math/role.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;
using namespace sub0math::f64::literals;

namespace
{

using Height = Role<struct HeightTag, f64::Length>;
using Width = Role<struct WidthTag, f64::Length>;
using Depth = Role<struct DepthTag, f64::Length>;
using RetryCount = Role<struct RetryCountTag, std::uint32_t>;

struct Box
{
    f64::Length height;
    f64::Length width;
    f64::Length depth;
};

constexpr Box makeBox(Height h, Width w, Depth d) noexcept
{
    return {h.value(), w.value(), d.value()};
}

template<typename... Args>
concept CanMakeBox = requires(Args... args) { makeBox(args...); };

} // namespace

// The point of roles: these mistakes do not compile.
static_assert(CanMakeBox<Height, Width, Depth>);
static_assert(!CanMakeBox<Width, Height, Depth>);             // swapped
static_assert(!CanMakeBox<f64::Length, f64::Length, f64::Length>); // unnamed
static_assert(!std::constructible_from<Height, int>);          // a length needs its unit
static_assert(!std::constructible_from<Height, Width>);         // roles never convert
static_assert(!std::is_convertible_v<f64::Length, Height>);    // explicit only
static_assert(std::constructible_from<RetryCount, int>);        // plain numbers are fine

// Zero overhead.
static_assert(sizeof(Height) == sizeof(double));
static_assert(std::is_trivially_copyable_v<Height>);

TEST_CASE("Named arguments read clearly and carry their values")
{
    constexpr Box box = makeBox(Height{1_m}, Width{5_m}, Depth{600_mm});
    CHECK(box.height == 1_m);
    CHECK(box.width == 5_m);
    CHECK(box.depth.in(millimetres) == doctest::Approx(600.0));
    CHECK(RetryCount{3}.value() == 3U);
    CHECK(Height{1_m} < Height{2_m});
    CHECK(Height{1_m} == Height{1000_mm});
}
