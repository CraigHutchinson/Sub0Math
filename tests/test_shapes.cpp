#include <algorithm>
#include <cmath>
#include <cstddef>
#include <doctest.h>
#include <limits>
#include <type_traits>
#include <vector>

#include "sub0math/geometry.hpp"
#include "sub0math/units.hpp"

using namespace sub0math;

namespace
{

using L = f64::Length;

constexpr double Root2 = 1.41421356237309504880;

Mat<double, 2, 2> rotation2(double angle)
{
    Mat<double, 2, 2> m{};
    m.columns[0] = {std::cos(angle), std::sin(angle)};
    m.columns[1] = {-std::sin(angle), std::cos(angle)};
    return m;
}

template<typename R, typename A, typename B>
concept BracedFrom = requires(A a, B b) { R{a, b}; };

template<typename A, typename B>
concept Overlappable = requires(A a, B b) { overlaps(a, b); };

} // namespace

// Shapes keep units and frames honest: a direction is a plain unit vector, never a length.
static_assert(BracedFrom<Ray<L, 3>, Point3<L>, Vec3d>);
static_assert(!BracedFrom<Ray<L, 3>, Point3<L>, Vec3<L>>);
static_assert(BracedFrom<Plane<L, 3>, Vec3d, L>);
static_assert(!BracedFrom<Plane<L, 3>, Vec3<L>, L>);
static_assert(Overlappable<OrientedBox<L, 2>, Aabb<L, 2>>);
static_assert(!Overlappable<OrientedBox<L, 2>, Aabb<double, 2>>);
static_assert(sizeof(Triangle<float, 3>) == 9 * sizeof(float));

TEST_CASE("Oriented box: nearest point, containment and bounds")
{
    // A 2 x 0.2 strip along the diagonal y = x.
    const OrientedBox<double, 2> strip{{0.0, 0.0}, {1.0, 0.1}, rotation2(std::atan(1.0))};
    CHECK(contains(strip, Point2d{0.5, 0.5}));
    CHECK_FALSE(contains(strip, Point2d{0.5, -0.5}));

    // (0, 1) is 0.707 along the strip and 0.707 across it; across is clamped to the 0.1 half width.
    const Point2d nearest = closestPoint(strip, Point2d{0.0, 1.0});
    CHECK(nearest.x == doctest::Approx(0.5 - 0.1 / Root2));
    CHECK(nearest.y == doctest::Approx(0.5 + 0.1 / Root2));
    const Point2d inside = closestPoint(strip, Point2d{0.2, 0.2}); // inside: itself
    CHECK(inside.x == doctest::Approx(0.2));
    CHECK(inside.y == doctest::Approx(0.2));

    const auto bounds = boundingBox(strip);
    CHECK(bounds.hi.x == doctest::Approx(1.1 / Root2));
    CHECK(bounds.lo.y == doctest::Approx(-1.1 / Root2));
}

TEST_CASE("Oriented box against axis-aligned blocks (2D separating axes)")
{
    const OrientedBox<double, 2> strip{{0.0, 0.0}, {1.0, 0.1}, rotation2(std::atan(1.0))};
    // Inside the strip's bounding box but off the diagonal: only an oriented test can reject it.
    const Aabb<double, 2> offDiagonal{{0.5, -0.9}, {0.9, -0.5}};
    CHECK(boundingBox(strip).intersects(offDiagonal));
    CHECK_FALSE(overlaps(strip, offDiagonal));
    CHECK(overlaps(strip, Aabb<double, 2>{{0.4, 0.4}, {0.6, 0.6}}));
    CHECK_FALSE(overlaps(strip, Aabb<double, 2>{{2.0, 2.0}, {3.0, 3.0}}));
}

TEST_CASE("Oriented boxes in 3D: separation found only on an edge-pair axis")
{
    // A is the unit cube [-1, 1]^3. B is a thin strip lying across A's edge x = y = 1, rolled 45 degrees
    // about its own axis so its corner faces that edge. Face axes cannot separate them; a_z x b_long can.
    const double h = 1.0 / Root2;
    Mat<double, 3, 3> axes{};
    axes.columns[0] = {h, -h, 0.0};                // long axis, perpendicular to the edge's diagonal
    axes.columns[1] = {0.5, 0.5, h};               // rolled 45 degrees
    axes.columns[2] = {-0.5, -0.5, h};
    const OrientedBox<double, 3> cube{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
    const auto strip = [&](double offset) {
        return OrientedBox<double, 3>{{1.0 + offset, 1.0 + offset, 0.0}, {1.0, 0.05, 0.05}, axes};
    };
    // The strip's corner reaches 0.05 * sqrt(2) toward the edge; the gap is offset * sqrt(2).
    CHECK_FALSE(overlaps(cube, strip(0.06)));
    CHECK(overlaps(cube, strip(0.04)));
    CHECK(overlaps(strip(0.04), cube) == overlaps(cube, strip(0.04)));
    CHECK(overlaps(cube, cube));
}

TEST_CASE("Capsule: signed distance, containment and bounds, with units")
{
    using namespace sub0math::f64::literals;
    const Capsule<L, 3> tyre{{{0_m, -30_mm, 360_mm}, {0_m, 30_mm, 360_mm}}, 360_mm};
    CHECK(signedDistance(tyre, Point3<L>{0_m, 0_m, -40_mm}).in(millimetres) == doctest::Approx(40.0));
    CHECK(signedDistance(tyre, Point3<L>{0_m, 0_m, 360_mm}).in(millimetres) == doctest::Approx(-360.0));
    CHECK(contains(tyre, Point3<L>{0_m, 390_mm, 360_mm}));
    CHECK_FALSE(contains(tyre, Point3<L>{0_m, 391_mm, 360_mm}));
    CHECK(boundingBox(tyre).hi.y == 390_mm);

    const Capsule<double, 2> point{{{1.0, 1.0}, {1.0, 1.0}}, 0.5}; // zero-length axis: a circle
    CHECK(signedDistance(point, Point2d{1.0, 2.0}) == doctest::Approx(0.5));
}

TEST_CASE("Plane: signed distance and projection")
{
    using namespace sub0math::f64::literals;
    const auto ground = Plane<L, 3>::through({0_m, 0_m, 20_mm}, {0.0, 0.0, 1.0});
    CHECK(signedDistance(ground, Point3<L>{5_m, -2_m, 50_mm}).in(millimetres) == doctest::Approx(30.0));
    CHECK(signedDistance(ground, Point3<L>{0_m, 0_m, 0_m}).in(millimetres) == doctest::Approx(-20.0));
    CHECK(closestPoint(ground, Point3<L>{1_m, 2_m, 3_m}).z.in(millimetres) == doctest::Approx(20.0));
}

TEST_CASE("Triangle: nearest point reports the feature it lies on")
{
    const Triangle<double, 3> tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const auto check = [&](Point3d p, Point3d expected, TriangleFeature feature) {
        const auto r = closestPoint(tri, p);
        CHECK(r.point.x == doctest::Approx(expected.x));
        CHECK(r.point.y == doctest::Approx(expected.y));
        CHECK(r.point.z == doctest::Approx(expected.z));
        CHECK(r.feature == feature);
    };
    check({0.2, 0.2, 0.5}, {0.2, 0.2, 0.0}, TriangleFeature::Face);
    check({-1, -1, 0}, {0, 0, 0}, TriangleFeature::VertexA);
    check({2, -1, 0}, {1, 0, 0}, TriangleFeature::VertexB);
    check({-1, 2, 0}, {0, 1, 0}, TriangleFeature::VertexC);
    check({0.5, -1, 0}, {0.5, 0, 0}, TriangleFeature::EdgeAB);
    check({1, 1, 0}, {0.5, 0.5, 0}, TriangleFeature::EdgeBC);
    check({-1, 0.5, 0}, {0, 0.5, 0}, TriangleFeature::EdgeCA);

    const Vec3d n = normal(tri);
    CHECK(n == Vec3d{0.0, 0.0, 1.0});

    using namespace sub0math::f64::literals;
    const Triangle<L, 3> metric{{0_m, 0_m, 0_m}, {10_mm, 0_m, 0_m}, {0_m, 10_mm, 2_mm}};
    CHECK(closestPoint(metric, Point3<L>{2_mm, 2_mm, 1_mm}).feature == TriangleFeature::Face);
    // 50 mm above, the projection onto the tilted plane lands near (2, 11.5) mm, past the hypotenuse BC.
    CHECK(closestPoint(metric, Point3<L>{2_mm, 2_mm, 50_mm}).feature == TriangleFeature::EdgeBC);
    CHECK(boundingBox(metric).hi.z == 2_mm);
}

TEST_CASE("Ray against triangle and plane")
{
    using namespace sub0math::f64::literals;
    const Triangle<L, 3> tri{{0_m, 0_m, 0_m}, {1_m, 0_m, 0_m}, {0_m, 1_m, 0_m}};
    const Ray<L, 3> down{{250_mm, 250_mm, 1_m}, {0.0, 0.0, -1.0}};
    const auto hit = intersect(down, tri, 2_m);
    REQUIRE(hit);
    CHECK(hit->distance.in(metres) == doctest::Approx(1.0));
    CHECK(hit->u == doctest::Approx(0.25));
    CHECK(hit->v == doctest::Approx(0.25));
    CHECK(down.at(hit->distance).z.in(metres) == doctest::Approx(0.0));

    CHECK_FALSE(intersect(down, tri, 500_mm));                                              // too far
    CHECK_FALSE(intersect(Ray<L, 3>{{2_m, 2_m, 1_m}, {0.0, 0.0, -1.0}}, tri, 2_m));          // outside
    CHECK_FALSE(intersect(Ray<L, 3>{{250_mm, 250_mm, 1_m}, {0.0, 0.0, 1.0}}, tri, 2_m));     // behind
    CHECK_FALSE(intersect(Ray<L, 3>{{250_mm, 250_mm, 1_m}, {1.0, 0.0, 0.0}}, tri, 2_m));     // parallel
    CHECK(intersect(Ray<L, 3>{{500_mm, 0_m, 1_m}, {0.0, 0.0, -1.0}}, tri, 2_m));             // on an edge
    CHECK(intersect(Ray<L, 3>{{250_mm, 250_mm, -1_m}, {0.0, 0.0, 1.0}}, tri, 2_m));          // from behind

    const auto slope = Plane<L, 3>::through({0_m, 0_m, 0_m}, {-1.0 / Root2, 0.0, 1.0 / Root2});
    const auto t = intersect(Ray<L, 3>{{1_m, 0_m, 2_m}, {0.0, 0.0, -1.0}}, slope, 10_m);
    REQUIRE(t);
    CHECK(t->in(metres) == doctest::Approx(1.0));
}

TEST_CASE("Regular grid: nodes and cell ownership")
{
    using namespace sub0math::f64::literals;
    const RegularGrid<L, 2> page{{0_m, 0_m}, 10_mm, {257, 257}}; // 256 x 256 cells, 2.56 m
    CHECK(page.nodeAt({256, 0}).x.in(metres) == doctest::Approx(2.56));

    const GridCell<2> cell = page.cellOf({15_mm, 2.555_m});
    REQUIRE(cell);
    CHECK(cell.index == GridIndex<2>{1, 255});
    const GridCell<2> corner = page.cellOf({2.56_m, 2.56_m});
    REQUIRE(corner.known); // far corner: last cell
    CHECK(corner.index == GridIndex<2>{255, 255});
    CHECK_FALSE(page.cellOf({-1_mm, 0_m}));
    CHECK_FALSE(page.cellOf({0_m, 2.57_m}));
    CHECK(page.cellOf({-1_mm, 0_m}).index == GridIndex<2>{0, 0});

    static_assert(!std::is_convertible_v<GridCell<2>, bool>); // explicit only
    static_assert(std::is_aggregate_v<GridCell<2>>);
    static_assert(std::is_trivially_copyable_v<GridCell<2>>);
}

TEST_CASE("Regular grid: ownership is half-open and the far edge clamps")
{
    const RegularGrid<double, 2> grid{{1.0, -2.0}, 0.5, {5, 3}}; // 4 x 2 cells: x in [1, 3], y in [-2, -1]
    const auto at = [&](double x, double y) { return grid.cellOf({x, y}); };

    CHECK(at(1.0, -2.0).index == GridIndex<2>{0, 0});  // lower corner: first cell
    CHECK(at(1.5, -1.5).index == GridIndex<2>{1, 1});  // interior node: the cell above it
    CHECK(at(1.4999999999999998, -1.5).index[0] == 0); // just below a node: the cell below
    CHECK(at(3.0, -1.0).index == GridIndex<2>{3, 1});  // far corner: last cell
    CHECK(at(3.0, -1.5).index == GridIndex<2>{3, 1});  // far x edge only
    CHECK_FALSE(at(0.9999999999999999, -2.0));         // just before the origin
    CHECK_FALSE(at(3.0000000000000004, -1.0));         // just past the far edge
    CHECK_FALSE(at(1.0, -0.999999999999));
}

TEST_CASE("Regular grid: non-finite and huge input is never converted to an integer")
{
    constexpr double Nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double Inf = std::numeric_limits<double>::infinity();
    const RegularGrid<double, 2> grid{{0.0, 0.0}, 1.0, {11, 11}};
    for (const double bad : {Nan, Inf, -Inf, 1e30, -1e30, 1e300, -1e300, std::numeric_limits<double>::max()})
    {
        INFO(bad);
        CHECK_FALSE(grid.cellOf({bad, 5.0}));
        CHECK_FALSE(grid.cellOf({5.0, bad}));
        CHECK(grid.cellOf({bad, bad}).index == GridIndex<2>{0, 0});
    }
    for (const double bad : {Nan, Inf, -Inf})
    {
        INFO(bad);
        CHECK(grid.cellsTouching(Aabb<double, 2>{{bad, 1.0}, {2.0, 2.0}}).empty());
        CHECK(grid.cellsTouching(Aabb<double, 2>{{1.0, 1.0}, {2.0, bad}}).empty());
        CHECK(grid.cellsTouching(Aabb<double, 2>{{1.0, bad}, {bad, 2.0}}).empty());
    }
    // Finite but huge boxes clamp to the grid instead of overflowing the conversion.
    const GridRange<2> all = grid.cellsTouching(Aabb<double, 2>{{-1e300, -1e300}, {1e300, 1e300}});
    CHECK(all.lo == GridIndex<2>{0, 0});
    CHECK(all.hi == GridIndex<2>{9, 9});
    CHECK(grid.cellsTouching(Aabb<double, 2>{{1e300, 0.0}, {1e301, 1.0}}).empty());
    CHECK(grid.cellsTouching(Aabb<double, 2>{{-1e301, 0.0}, {-1e300, 1.0}}).empty());

    const RegularGrid<float, 2> narrow{{0.0F, 0.0F}, 1.0F, {11, 11}}; // float overflows sooner
    CHECK_FALSE(narrow.cellOf({3.0e38F, 1.0F}));
    CHECK(narrow.cellsTouching(Aabb<float, 2>{{-3.0e38F, 0.0F}, {3.0e38F, 1.0F}}).hi == GridIndex<2>{9, 1});

    const RegularGrid<double, 2> degenerate{{0.0, 0.0}, 1.0, {1, 0}}; // no cells at all
    CHECK_FALSE(degenerate.cellOf({0.0, 0.0}));
    CHECK(degenerate.cellsTouching(Aabb<double, 2>{{-1.0, -1.0}, {1.0, 1.0}}).empty());
}

TEST_CASE("Regular grid: coverage is closed and equals brute force on every boundary")
{
    // 5 x 4 cells, origin (-1, 0.5), spacing 0.5: nodes and all test corners are multiples of 0.5 (exact in
    // binary), so corners fall on, between and beyond nodes.
    const RegularGrid<double, 2> grid{{-1.0, 0.5}, 0.5, {6, 5}};
    std::vector<double> coords;
    for (int k = -8; k <= 12; ++k) coords.push_back(0.5 * k);

    // A cell [a, b] is touched by the closed interval [lo, hi] iff lo <= b and hi >= a.
    const auto expected = [](double lo, double hi, double origin, int cells, int& first, int& last) {
        first = cells;
        last = -1;
        for (int c = 0; c < cells; ++c)
        {
            const double a = origin + 0.5 * c;
            const double b = a + 0.5;
            if (lo <= b && hi >= a)
            {
                first = std::min(first, c);
                last = c;
            }
        }
    };
    std::size_t boxes = 0;
    for (const double xlo : coords)
    {
        for (const double xhi : coords)
        {
            if (xhi < xlo) continue;
            int fx = 0;
            int lx = 0;
            expected(xlo, xhi, -1.0, 5, fx, lx);
            for (const double ylo : coords)
            {
                for (const double yhi : coords)
                {
                    if (yhi < ylo) continue;
                    int fy = 0;
                    int ly = 0;
                    expected(ylo, yhi, 0.5, 4, fy, ly);
                    const GridRange<2> r = grid.cellsTouching(Aabb<double, 2>{{xlo, ylo}, {xhi, yhi}});
                    ++boxes;
                    if (lx < 0 || ly < 0)
                    {
                        if (!r.empty()) FAIL("expected empty for ", xlo, ' ', ylo, ' ', xhi, ' ', yhi);
                    }
                    else if (r.lo != GridIndex<2>{fx, fy} || r.hi != GridIndex<2>{lx, ly})
                    {
                        FAIL("range mismatch for ", xlo, ' ', ylo, ' ', xhi, ' ', yhi);
                    }
                }
            }
        }
    }
    CHECK(boxes > 20000);
}

TEST_CASE("Regular grid: coverage named cases")
{
    using namespace sub0math::f64::literals;
    const RegularGrid<L, 2> page{{0_m, 0_m}, 10_mm, {257, 257}};
    const auto range = page.cellsTouching(Aabb<L, 2>{{-1_m, 25_mm}, {35_mm, 25_mm}});
    CHECK(range.lo == GridIndex<2>{0, 2});
    CHECK(range.hi == GridIndex<2>{3, 2});
    CHECK(range.count() == 4);
    CHECK(page.cellsTouching(Aabb<L, 2>{{3_m, 0_m}, {4_m, 1_m}}).empty());
    CHECK(page.cellsTouching(Aabb<L, 2>{{-5_m, -5_m}, {5_m, 5_m}}).count() == 256 * 256);

    const RegularGrid<double, 2> g{{0.0, 0.0}, 1.0, {5, 5}}; // 4 x 4 cells
    const auto box = [&](double x0, double y0, double x1, double y1) {
        return g.cellsTouching(Aabb<double, 2>{{x0, y0}, {x1, y1}});
    };
    // Touching the lower edge from outside includes the first cell; so does touching the far edge the last.
    CHECK(box(-2.0, 1.5, 0.0, 1.5).lo == GridIndex<2>{0, 1});
    CHECK(box(-2.0, 1.5, 0.0, 1.5).hi == GridIndex<2>{0, 1});
    CHECK(box(4.0, 1.5, 6.0, 1.5).lo == GridIndex<2>{3, 1});
    CHECK(box(4.0, 1.5, 6.0, 1.5).hi == GridIndex<2>{3, 1});
    CHECK(box(-2.0, 1.5, -0.0000001, 1.5).empty());
    CHECK(box(4.0000001, 1.5, 6.0, 1.5).empty());
    // A point on an interior node touches all four incident cells.
    const auto node = box(2.0, 2.0, 2.0, 2.0);
    CHECK(node.lo == GridIndex<2>{1, 1});
    CHECK(node.hi == GridIndex<2>{2, 2});
    // A box edge on a boundary includes both sides; strictly inside it stays within one cell.
    CHECK(box(1.0, 0.5, 2.0, 0.5).count() == 3);
    CHECK(box(1.25, 0.5, 1.75, 0.5).count() == 1);
    // The corner node of the grid belongs to a single cell.
    CHECK(box(0.0, 0.0, 0.0, 0.0).count() == 1);
    CHECK(box(4.0, 4.0, 4.0, 4.0).count() == 1);
}
