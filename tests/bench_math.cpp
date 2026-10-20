#define ANKERL_NANOBENCH_IMPLEMENT
#include <algorithm>
#include <bench_runner.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

// A triangulated height page (32 x 16 cells, 1 cm, 2 triangles per cell = 1024 triangles): the kernel of
// an exact terrain gap query (terrain design D0). Hand-written Ericson on doubles, then
// closestPoint(Triangle<f64::Length, 3>). Both accumulate the nearest squared distance.
struct HeightTriangles
{
    std::vector<double> ax, ay, az, bx, by, bz, cx, cy, cz;
    std::vector<Triangle<f64::Length, 3>> tris;
};

HeightTriangles makeHeightTriangles()
{
    HeightTriangles h;
    const auto z = [](int i, int j) { return 0.02 * std::sin(0.3 * i) * std::cos(0.2 * j); };
    const auto add = [&](double x0, double y0, double z0, double x1, double y1, double z1, double x2, double y2, double z2) {
        h.ax.push_back(x0); h.ay.push_back(y0); h.az.push_back(z0);
        h.bx.push_back(x1); h.by.push_back(y1); h.bz.push_back(z1);
        h.cx.push_back(x2); h.cy.push_back(y2); h.cz.push_back(z2);
        const auto m = [](double v) { return f64::Length::fromSi(v); };
        h.tris.push_back({{m(x0), m(y0), m(z0)}, {m(x1), m(y1), m(z1)}, {m(x2), m(y2), m(z2)}});
    };
    for (int j = 0; j < 16; ++j)
        for (int i = 0; i < 32; ++i)
        {
            const double x = 0.01 * i, y = 0.01 * j;
            add(x, y, z(i, j), x + 0.01, y, z(i + 1, j), x + 0.01, y + 0.01, z(i + 1, j + 1));
            add(x, y, z(i, j), x + 0.01, y + 0.01, z(i + 1, j + 1), x, y + 0.01, z(i, j + 1));
        }
    return h;
}

void benchTriangleClosest(sub0bench::BenchRunner& runner)
{
    const HeightTriangles h = makeHeightTriangles();
    const double px = 0.163, py = 0.071, pz = 0.012;
    runner.run("triangle closest point, hand-written scalars", [&] {
        double best = 1e300;
        for (std::size_t k = 0; k < Count; ++k)
        {
            const double abx = h.bx[k] - h.ax[k], aby = h.by[k] - h.ay[k], abz = h.bz[k] - h.az[k];
            const double acx = h.cx[k] - h.ax[k], acy = h.cy[k] - h.ay[k], acz = h.cz[k] - h.az[k];
            const double apx = px - h.ax[k], apy = py - h.ay[k], apz = pz - h.az[k];
            const double d1 = abx * apx + aby * apy + abz * apz, d2 = acx * apx + acy * apy + acz * apz;
            double qx, qy, qz;
            if (d1 <= 0 && d2 <= 0) { qx = h.ax[k]; qy = h.ay[k]; qz = h.az[k]; }
            else
            {
                const double bpx = px - h.bx[k], bpy = py - h.by[k], bpz = pz - h.bz[k];
                const double d3 = abx * bpx + aby * bpy + abz * bpz, d4 = acx * bpx + acy * bpy + acz * bpz;
                const double cpx = px - h.cx[k], cpy = py - h.cy[k], cpz = pz - h.cz[k];
                const double d5 = abx * cpx + aby * cpy + abz * cpz, d6 = acx * cpx + acy * cpy + acz * cpz;
                const double vc = d1 * d4 - d3 * d2, vb = d5 * d2 - d1 * d6, va = d3 * d6 - d5 * d4;
                if (d3 >= 0 && d4 <= d3) { qx = h.bx[k]; qy = h.by[k]; qz = h.bz[k]; }
                else if (vc <= 0 && d1 >= 0 && d3 <= 0) { const double v = d1 / (d1 - d3); qx = h.ax[k] + abx * v; qy = h.ay[k] + aby * v; qz = h.az[k] + abz * v; }
                else if (d6 >= 0 && d5 <= d6) { qx = h.cx[k]; qy = h.cy[k]; qz = h.cz[k]; }
                else if (vb <= 0 && d2 >= 0 && d6 <= 0) { const double w = d2 / (d2 - d6); qx = h.ax[k] + acx * w; qy = h.ay[k] + acy * w; qz = h.az[k] + acz * w; }
                else if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0)
                {
                    const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
                    qx = h.bx[k] + (h.cx[k] - h.bx[k]) * w; qy = h.by[k] + (h.cy[k] - h.by[k]) * w; qz = h.bz[k] + (h.cz[k] - h.bz[k]) * w;
                }
                else
                {
                    const double sum = va + vb + vc, v = vb / sum, w = vc / sum;
                    qx = h.ax[k] + abx * v + acx * w; qy = h.ay[k] + aby * v + acy * w; qz = h.az[k] + abz * v + acz * w;
                }
            }
            best = std::min(best, (px - qx) * (px - qx) + (py - qy) * (py - qy) + (pz - qz) * (pz - qz));
        }
        ankerl::nanobench::doNotOptimizeAway(best);
    });

    const Point3<f64::Length> p{f64::Length::fromSi(px), f64::Length::fromSi(py), f64::Length::fromSi(pz)};
    runner.run("triangle closest point, closestPoint(Triangle<f64::Length>)", [&] {
        f64::Area best = f64::Area::fromSi(1e300);
        for (const auto& tri : h.tris)
        {
            best = std::min(best, lengthSquared(p - closestPoint(tri, p).point));
        }
        ankerl::nanobench::doNotOptimizeAway(best);
    });
}

// Corridor coverage: which 8 x 8 cell blocks (8 cm) of a 32 x 32 block page an oriented sweep box
// touches. Hand-written 2D separating axes on doubles, then AabbOverlapTester (box terms computed once).
void benchBoxBlocks(sub0bench::BenchRunner& runner)
{
    const double angle = 0.4, c = std::cos(angle), s = std::sin(angle);
    const double cx = 1.3, cy = 1.1, hx = 0.9, hy = 0.18, block = 0.08;
    runner.run("box vs 1024 blocks, hand-written 2D SAT", [&] {
        int hits = 0;
        for (int j = 0; j < 32; ++j)
            for (int i = 0; i < 32; ++i)
            {
                // The fastest form: centres straight from the grid index, one half size for every block.
                const double bx = (i + 0.5) * block, by = (j + 0.5) * block, bh = block / 2;
                const double dx = bx - cx, dy = by - cy;
                const bool sep = std::abs(dx) > bh + hx * std::abs(c) + hy * std::abs(s)
                    || std::abs(dy) > bh + hx * std::abs(s) + hy * std::abs(c)
                    || std::abs(dx * c + dy * s) > hx + bh * (std::abs(c) + std::abs(s))
                    || std::abs(-dx * s + dy * c) > hy + bh * (std::abs(s) + std::abs(c));
                hits += sep ? 0 : 1;
            }
        ankerl::nanobench::doNotOptimizeAway(hits);
    });

    Mat<double, 2, 2> axes{};
    axes.columns[0] = {c, s};
    axes.columns[1] = {-s, c};
    const AabbOverlapTester<double, 2> box(OrientedBox<double, 2>{{cx, cy}, {hx, hy}, axes});
    runner.run("box vs 1024 blocks, AabbOverlapTester(Aabb corners)", [&] {
        int hits = 0;
        for (int j = 0; j < 32; ++j)
            for (int i = 0; i < 32; ++i)
            {
                const Aabb<double, 2> b{{i * block, j * block}, {(i + 1) * block, (j + 1) * block}};
                hits += box(b) ? 1 : 0;
            }
        ankerl::nanobench::doNotOptimizeAway(hits);
    });
    const Vec2d blockHalf{block / 2, block / 2};
    runner.run("box vs 1024 blocks, AabbOverlapTester(centre, half)", [&] {
        int hits = 0;
        for (int j = 0; j < 32; ++j)
            for (int i = 0; i < 32; ++i)
            {
                hits += box.overlapsCentred(Point2d{(i + 0.5) * block, (j + 0.5) * block}, blockHalf) ? 1 : 0;
            }
        ankerl::nanobench::doNotOptimizeAway(hits);
    });
}

// Grid indexing: owning cell of 1024 points and the cells touched by 1024 boxes, on a 64 x 64 cell grid
// (4 cm). About one input in six lies outside the grid, so the unknown path is timed too. The hand-written
// versions are plain scalar code with the same rules and the same refusal of non-finite input.
void benchGrid(sub0bench::BenchRunner& runner)
{
    constexpr double ox = -0.5, oy = 0.25, spacing = 0.04;
    constexpr int cells = 64;
    std::vector<Point<f64::Length, 2>> points;
    std::vector<Aabb<f64::Length, 2>> boxes;
    std::vector<double> px, py, bx0, by0, bx1, by1;
    std::uint32_t state = 12345U;
    const auto uniform = [&state] {
        state = state * 1664525U + 1013904223U;
        return static_cast<double>(state >> 8) / static_cast<double>(1U << 24);
    };
    for (std::size_t i = 0; i < Count; ++i)
    {
        const double x = ox - 0.4 + uniform() * (cells * spacing + 0.8);
        const double y = oy - 0.4 + uniform() * (cells * spacing + 0.8);
        const double w = uniform() * 0.3, h = uniform() * 0.3;
        px.push_back(x);
        py.push_back(y);
        bx0.push_back(x);
        by0.push_back(y);
        bx1.push_back(x + w);
        by1.push_back(y + h);
        points.push_back({metres(x), metres(y)});
        boxes.push_back({{metres(x), metres(y)}, {metres(x + w), metres(y + h)}});
    }

    runner.run("grid cellOf x1024, hand-written scalars", [&] {
        long sum = 0;
        for (std::size_t i = 0; i < Count; ++i)
        {
            const double ux = (px[i] - ox) / spacing, uy = (py[i] - oy) / spacing;
            if (!(ux >= 0.0) || ux > cells || !(uy >= 0.0) || uy > cells) continue;
            sum += std::min(static_cast<int>(std::floor(ux)), cells - 1) +
                   65536L * std::min(static_cast<int>(std::floor(uy)), cells - 1) + 1;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    const RegularGrid<f64::Length, 2> grid{{metres(ox), metres(oy)}, metres(spacing), {cells + 1, cells + 1}};
    runner.run("grid cellOf x1024, RegularGrid<f64::Length>::cellOf", [&] {
        long sum = 0;
        for (std::size_t i = 0; i < Count; ++i)
        {
            const GridCell<2> cell = grid.cellOf(points[i]);
            if (cell) sum += cell.index[0] + 65536L * cell.index[1] + 1;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });

    runner.run("grid cellsTouching x1024, hand-written scalars", [&] {
        long sum = 0;
        for (std::size_t i = 0; i < Count; ++i)
        {
            const double lx = (bx0[i] - ox) / spacing, hx = (bx1[i] - ox) / spacing;
            const double ly = (by0[i] - oy) / spacing, hy = (by1[i] - oy) / spacing;
            if (!(std::isfinite(lx) && std::isfinite(hx) && std::isfinite(ly) && std::isfinite(hy))) continue;
            const int x0 = static_cast<int>(std::max(0.0, std::min(std::ceil(lx) - 1.0, double{cells})));
            const int y0 = static_cast<int>(std::max(0.0, std::min(std::ceil(ly) - 1.0, double{cells})));
            const int x1 = static_cast<int>(std::max(-1.0, std::min(std::floor(hx), cells - 1.0)));
            const int y1 = static_cast<int>(std::max(-1.0, std::min(std::floor(hy), cells - 1.0)));
            sum += (x1 >= x0 && y1 >= y0) ? static_cast<long>(x1 - x0 + 1) * (y1 - y0 + 1) : 0L;
        }
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
    runner.run("grid cellsTouching x1024, RegularGrid<f64::Length>::cellsTouching", [&] {
        long sum = 0;
        for (std::size_t i = 0; i < Count; ++i) sum += grid.cellsTouching(boxes[i]).count();
        ankerl::nanobench::doNotOptimizeAway(sum);
    });
}

// Support-projected sampling: a ray along -n against every triangle of the page. Hand-written
// Moller-Trumbore on doubles, then intersect(Ray<f64::Length, 3>, Triangle).
void benchRayTriangle(sub0bench::BenchRunner& runner)
{
    const HeightTriangles h = makeHeightTriangles();
    const double ox = 0.163, oy = 0.071, oz = 0.5;
    const double nx = 0.1, ny = 0.05, nz = -std::sqrt(1.0 - 0.0125);
    runner.run("ray vs triangles, hand-written Moller-Trumbore", [&] {
        double nearest = 1e300;
        for (std::size_t k = 0; k < Count; ++k)
        {
            const double e1x = h.bx[k] - h.ax[k], e1y = h.by[k] - h.ay[k], e1z = h.bz[k] - h.az[k];
            const double e2x = h.cx[k] - h.ax[k], e2y = h.cy[k] - h.ay[k], e2z = h.cz[k] - h.az[k];
            const double px = ny * e2z - nz * e2y, py = nz * e2x - nx * e2z, pz = nx * e2y - ny * e2x;
            const double det = e1x * px + e1y * py + e1z * pz;
            const double scale = e1x * e1x + e1y * e1y + e1z * e1z + e2x * e2x + e2y * e2y + e2z * e2z;
            if (std::abs(det) <= scale * 1.7763568394002505e-15) continue;
            const double sx = ox - h.ax[k], sy = oy - h.ay[k], sz = oz - h.az[k];
            const double u = (sx * px + sy * py + sz * pz) / det;
            if (u < 0 || u > 1) continue;
            const double qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
            const double v = (nx * qx + ny * qy + nz * qz) / det;
            if (v < 0 || u + v > 1) continue;
            const double t = (e2x * qx + e2y * qy + e2z * qz) / det;
            if (t >= 0 && t <= 2.0) nearest = std::min(nearest, t);
        }
        ankerl::nanobench::doNotOptimizeAway(nearest);
    });

    const Ray<f64::Length, 3> ray{{f64::Length::fromSi(ox), f64::Length::fromSi(oy), f64::Length::fromSi(oz)}, {nx, ny, nz}};
    runner.run("ray vs triangles, intersect(Ray<f64::Length>, Triangle)", [&] {
        f64::Length nearest = f64::Length::fromSi(1e300);
        for (const auto& tri : h.tris)
        {
            if (const auto hit = intersect(ray, tri, f64::Length::fromSi(2.0))) nearest = std::min(nearest, hit->distance);
        }
        ankerl::nanobench::doNotOptimizeAway(nearest);
    });
}

// Spatial algebra for rigid-body dynamics: one Plücker transform per body, applied to a motion and (as
// its transpose) to a force, and a composite inertia carried to the parent frame. The hand-written side
// is plain scalar code on the same column-major data, written for speed (the inertia result is
// symmetric, so it computes six entries and mirrors them).
struct SpatialData
{
    std::vector<PluckerTransformd> transforms;
    std::vector<SpatialMotiond> motions;
    std::vector<SpatialForced> forces;
    std::vector<SpatialInertiad> inertias;
};

SpatialData makeSpatialData()
{
    SpatialData d;
    for (std::size_t i = 0; i < Count; ++i)
    {
        const double f = static_cast<double>(i % 29) * 0.21;
        const RigidTransformd pose{Quatd::fromAxisAngle(normalized(Vec3d{1.0, f - 2.0, 0.5}), f64::Angle::fromSi(f)),
                                   {0.3 * f, -0.2, 1.0 - f}};
        d.transforms.push_back(PluckerTransformd::fromPose(pose));
        d.motions.push_back({{f, -0.5, 0.25}, {0.1, f * 0.5, -1.0}});
        d.forces.push_back({{0.2, f, -0.3}, {-f, 0.7, 0.4}});
        Mat3d ic{};
        ic(0, 0) = 0.3 + 0.01 * f;
        ic(1, 1) = 0.2;
        ic(2, 2) = 0.4;
        ic(0, 1) = ic(1, 0) = 0.01;
        d.inertias.push_back(SpatialInertiad::fromCentreOfMass(1.0 + f, {0.1, -0.3 * f, 0.2}, ic));
    }
    return d;
}

void benchSpatialTransform(sub0bench::BenchRunner& runner)
{
    const SpatialData d = makeSpatialData();
    std::vector<SpatialMotiond> outMotion(Count);
    std::vector<SpatialForced> outForce(Count);

    runner.run("spatial transform x1024, hand-written scalars", [&] {
        for (std::size_t k = 0; k < Count; ++k)
        {
            const auto& c = d.transforms[k].rotation.columns; // column-major: e[c * 3 + r]
            const double e[9] = {c[0].x, c[0].y, c[0].z, c[1].x, c[1].y, c[1].z, c[2].x, c[2].y, c[2].z};
            const double rx = d.transforms[k].translation.x, ry = d.transforms[k].translation.y, rz = d.transforms[k].translation.z;
            const SpatialMotiond& m = d.motions[k];
            const double wx = m.angular.x, wy = m.angular.y, wz = m.angular.z;
            const double ux = m.linear.x - (ry * wz - rz * wy), uy = m.linear.y - (rz * wx - rx * wz), uz = m.linear.z - (rx * wy - ry * wx);
            const SpatialForced& f = d.forces[k];
            const double fx = e[0] * f.linear.x + e[1] * f.linear.y + e[2] * f.linear.z;
            const double fy = e[3] * f.linear.x + e[4] * f.linear.y + e[5] * f.linear.z;
            const double fz = e[6] * f.linear.x + e[7] * f.linear.y + e[8] * f.linear.z;
            const double ox = e[0] * wx + e[3] * wy + e[6] * wz, oy = e[1] * wx + e[4] * wy + e[7] * wz, oz = e[2] * wx + e[5] * wy + e[8] * wz;
            const double lx = e[0] * ux + e[3] * uy + e[6] * uz, ly = e[1] * ux + e[4] * uy + e[7] * uz, lz = e[2] * ux + e[5] * uy + e[8] * uz;
            const double nx = e[0] * f.angular.x + e[1] * f.angular.y + e[2] * f.angular.z + (ry * fz - rz * fy);
            const double ny = e[3] * f.angular.x + e[4] * f.angular.y + e[5] * f.angular.z + (rz * fx - rx * fz);
            const double nz = e[6] * f.angular.x + e[7] * f.angular.y + e[8] * f.angular.z + (rx * fy - ry * fx);
            // Results are stored once, after every input is read: the outputs could alias the inputs.
            outMotion[k] = {{ox, oy, oz}, {lx, ly, lz}};
            outForce[k] = {{nx, ny, nz}, {fx, fy, fz}};
        }
        ankerl::nanobench::doNotOptimizeAway(outMotion.data());
        ankerl::nanobench::doNotOptimizeAway(outForce.data());
    });

    runner.run("spatial transform x1024, PluckerTransform apply/applyInverse", [&] {
        for (std::size_t k = 0; k < Count; ++k)
        {
            outMotion[k] = d.transforms[k].apply(d.motions[k]);
            outForce[k] = d.transforms[k].applyInverse(d.forces[k]);
        }
        ankerl::nanobench::doNotOptimizeAway(outMotion.data());
        ankerl::nanobench::doNotOptimizeAway(outForce.data());
    });

    std::vector<SpatialInertiad> outInertia(Count);
    runner.run("inertia to parent x1024, hand-written scalars", [&] {
        for (std::size_t k = 0; k < Count; ++k)
        {
            const auto& ec = d.transforms[k].rotation.columns;
            const auto& ic = d.inertias[k].rotational.columns;
            const double e[9] = {ec[0].x, ec[0].y, ec[0].z, ec[1].x, ec[1].y, ec[1].z, ec[2].x, ec[2].y, ec[2].z};
            const double i[9] = {ic[0].x, ic[0].y, ic[0].z, ic[1].x, ic[1].y, ic[1].z, ic[2].x, ic[2].y, ic[2].z};
            const double rx = d.transforms[k].translation.x, ry = d.transforms[k].translation.y, rz = d.transforms[k].translation.z;
            const double m = d.inertias[k].mass;
            const Vec3d& h = d.inertias[k].firstMoment;
            const double yx = e[0] * h.x + e[1] * h.y + e[2] * h.z;
            const double yy = e[3] * h.x + e[4] * h.y + e[5] * h.z;
            const double yz = e[6] * h.x + e[7] * h.y + e[8] * h.z;
            // A = I E (column-major), then the upper triangle of E^T A.
            double a[9];
            for (int c = 0; c < 3; ++c)
            {
                a[c * 3 + 0] = i[0] * e[c * 3] + i[3] * e[c * 3 + 1] + i[6] * e[c * 3 + 2];
                a[c * 3 + 1] = i[1] * e[c * 3] + i[4] * e[c * 3 + 1] + i[7] * e[c * 3 + 2];
                a[c * 3 + 2] = i[2] * e[c * 3] + i[5] * e[c * 3 + 1] + i[8] * e[c * 3 + 2];
            }
            const double y[3] = {yx, yy, yz};
            const double r[3] = {rx, ry, rz};
            const double diagonal = 2.0 * (rx * yx + ry * yy + rz * yz) + m * (rx * rx + ry * ry + rz * rz);
            SpatialInertiad out;
            double o[9];
            for (int c = 0; c < 3; ++c)
            {
                for (int row = 0; row <= c; ++row)
                {
                    const double v = e[row * 3] * a[c * 3] + e[row * 3 + 1] * a[c * 3 + 1] + e[row * 3 + 2] * a[c * 3 + 2]
                                     - y[row] * r[c] - r[row] * (y[c] + m * r[c]) + (row == c ? diagonal : 0.0);
                    o[c * 3 + row] = v;
                    o[row * 3 + c] = v;
                }
            }
            out.mass = m;
            out.firstMoment = {yx + m * rx, yy + m * ry, yz + m * rz};
            out.rotational.columns = {Vec3d{o[0], o[1], o[2]}, Vec3d{o[3], o[4], o[5]}, Vec3d{o[6], o[7], o[8]}};
            outInertia[k] = out;
        }
        ankerl::nanobench::doNotOptimizeAway(outInertia.data());
    });

    runner.run("inertia to parent x1024, PluckerTransform::applyInverse", [&] {
        for (std::size_t k = 0; k < Count; ++k)
        {
            outInertia[k] = d.transforms[k].applyInverse(d.inertias[k]);
        }
        ankerl::nanobench::doNotOptimizeAway(outInertia.data());
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
    benchTriangleClosest(runner);
    benchBoxBlocks(runner);
    benchRayTriangle(runner);
    benchGrid(runner);
    benchSpatialTransform(runner);
    return runner.finish();
}
