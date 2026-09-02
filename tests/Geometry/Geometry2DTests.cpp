#include "TestSupport.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <limits>

namespace object_connect::tests {
namespace {

void TestPointAndCircleRectangle(TestContext& context) {
    context.Expect(PointInCircle({2.0f, 3.0f}, {2.0f, 3.0f}, 5.0f),
                   "circle contains its center");
    context.Expect(PointInCircle({5.0f, 3.0f}, {2.0f, 3.0f}, 3.0f),
                   "circle includes its boundary");
    context.Expect(!PointInCircle({5.1f, 3.0f}, {2.0f, 3.0f}, 3.0f),
                   "circle excludes points beyond its boundary");
    context.Expect(!PointInCircle({0.0f, 0.0f}, {0.0f, 0.0f}, -1.0f),
                   "negative circle radius is invalid");

    context.Expect(CircleOverlapsRectangle({0.0f, 0.0f}, 1.0f, {0.0f, 0.0f},
                                           4.0f, 2.0f),
                   "circle inside a rectangle overlaps it");
    context.Expect(CircleOverlapsRectangle({3.0f, 0.0f}, 1.0f, {0.0f, 0.0f},
                                           4.0f, 2.0f),
                   "circle tangent to a rectangle counts as overlap");
    context.Expect(!CircleOverlapsRectangle({3.1f, 0.0f}, 1.0f, {0.0f, 0.0f},
                                            4.0f, 2.0f),
                   "separated circle and rectangle do not overlap");
}

void TestSegmentCircle(TestContext& context) {
    context.Expect(SegmentIntersectsCircle({-5.0f, 0.0f}, {5.0f, 0.0f},
                                           {0.0f, 0.0f}, 2.0f),
                   "segment crossing a circle intersects it");
    context.Expect(SegmentIntersectsCircle({-5.0f, 2.0f}, {5.0f, 2.0f},
                                           {0.0f, 0.0f}, 2.0f),
                   "segment tangent to a circle intersects it");
    context.Expect(!SegmentIntersectsCircle({-5.0f, 2.1f}, {5.0f, 2.1f},
                                            {0.0f, 0.0f}, 2.0f),
                   "segment beyond a circle misses it");
    context.Expect(SegmentIntersectsCircle({1.0f, 0.0f}, {1.0f, 0.0f},
                                           {0.0f, 0.0f}, 2.0f),
                   "degenerate segment inside a circle intersects it");
    context.Expect(!SegmentIntersectsCircle({3.0f, 0.0f}, {3.0f, 0.0f},
                                            {0.0f, 0.0f}, 2.0f),
                   "degenerate segment outside a circle misses it");
}

void TestSegmentRectangle(TestContext& context) {
    context.Expect(SegmentIntersectsRectangle({-5.0f, 0.0f}, {5.0f, 0.0f},
                                              {0.0f, 0.0f}, 4.0f, 2.0f),
                   "segment crossing a rectangle intersects it");
    context.Expect(SegmentIntersectsRectangle({-5.0f, 1.0f}, {5.0f, 1.0f},
                                              {0.0f, 0.0f}, 4.0f, 2.0f),
                   "segment along a rectangle edge intersects it");
    context.Expect(!SegmentIntersectsRectangle({-5.0f, 1.1f}, {5.0f, 1.1f},
                                               {0.0f, 0.0f}, 4.0f, 2.0f),
                   "parallel segment beyond a rectangle misses it");
    context.Expect(SegmentIntersectsRectangle({0.0f, 0.0f}, {0.0f, 0.0f},
                                              {0.0f, 0.0f}, 4.0f, 2.0f),
                   "degenerate segment inside a rectangle intersects it");
    context.Expect(!SegmentIntersectsRectangle({4.0f, 4.0f}, {4.0f, 4.0f},
                                               {0.0f, 0.0f}, 4.0f, 2.0f),
                   "degenerate segment outside a rectangle misses it");
}

void TestWorldPointToTileCell(TestContext& context) {
    const Vec2 origin{10.0f, 20.0f};
    context.Expect(WorldPointToTileCell({10.0f, 20.0f}, origin, 16.0f, 3, 2) ==
                       TileCoordinate{0, 0},
                   "tile grid includes its top-left boundary");
    context.Expect(WorldPointToTileCell({25.99f, 35.99f}, origin, 16.0f, 3, 2) ==
                       TileCoordinate{0, 0},
                   "world points remain in their containing tile");
    context.Expect(WorldPointToTileCell({26.0f, 36.0f}, origin, 16.0f, 3, 2) ==
                       TileCoordinate{1, 1},
                   "internal tile boundaries select the cell to the right and below");
    context.Expect(!WorldPointToTileCell({58.0f, 20.0f}, origin, 16.0f, 3, 2),
                   "tile grid excludes its right boundary");
    context.Expect(!WorldPointToTileCell({10.0f, 52.0f}, origin, 16.0f, 3, 2),
                   "tile grid excludes its bottom boundary");
    context.Expect(!WorldPointToTileCell({9.99f, 20.0f}, origin, 16.0f, 3, 2),
                   "tile grid excludes points left of its origin");
    context.Expect(!WorldPointToTileCell({10.0f, 20.0f}, origin, 0.0f, 3, 2),
                   "non-positive tile size is invalid");
}

void TestIrregularTileStampHit(TestContext& context) {
    TileStamp stamp;
    stamp.columns = 3;
    stamp.rows = 2;
    stamp.cells = {1, 0, 2, 0, 3, 0};
    stamp.occupiedMask = {1, 0, 1, 0, 1, 0};
    stamp.anchor = {2, 1};

    const Vec2 origin{100.0f, 200.0f};
    context.Expect(PointHitsOccupiedTileStamp({101.0f, 201.0f}, origin, 16.0f,
                                               stamp),
                   "occupied stamp cell is hittable");
    context.Expect(!PointHitsOccupiedTileStamp({117.0f, 201.0f}, origin, 16.0f,
                                                stamp),
                   "an interior hole in an irregular stamp is not hittable");
    context.Expect(PointHitsOccupiedTileStamp({117.0f, 217.0f}, origin, 16.0f,
                                               stamp),
                   "a separate occupied island remains hittable");
    context.Expect(!PointHitsOccupiedTileStamp({149.0f, 201.0f}, origin, 16.0f,
                                                stamp),
                   "point outside the stamp does not hit");

    stamp.occupiedMask.pop_back();
    context.Expect(!PointHitsOccupiedTileStamp({101.0f, 201.0f}, origin, 16.0f,
                                                stamp),
                   "malformed occupied mask fails safely");
}

void TestSolidTileGridSegments(TestContext& context) {
    TileGrid grid;
    grid.columns = 3;
    grid.rows = 2;
    grid.cells = {0, 7, 0, 0, 0, 0};
    const Vec2 origin{10.0f, 20.0f};

    context.Expect(SegmentIntersectsSolidTileGrid(
                       {0.0f, 28.0f}, {70.0f, 28.0f}, origin, 16.0f, grid,
                       0.0f, 0.0f),
                   "nonzero tile blocks a crossing segment");
    context.Expect(SegmentIntersectsSolidTileGrid(
                       {26.0f, 0.0f}, {26.0f, 60.0f}, origin, 16.0f, grid,
                       0.0f, 0.0f),
                   "contact with a solid tile boundary counts as blocked");
    context.Expect(!SegmentIntersectsSolidTileGrid(
                       {0.0f, 19.0f}, {70.0f, 19.0f}, origin, 16.0f, grid,
                       0.0f, 0.0f),
                   "segment outside an unexpanded tile remains clear");
    context.Expect(SegmentIntersectsSolidTileGrid(
                       {0.0f, 19.0f}, {70.0f, 19.0f}, origin, 16.0f, grid,
                       0.0f, 1.0f),
                   "pixel padding expands every solid tile AABB");
    context.Expect(SegmentIntersectsSolidTileGrid(
                       {0.0f, 17.0f}, {70.0f, 17.0f}, origin, 16.0f, grid,
                       2.0f, 1.0f),
                   "clearance and pixel padding add together");

    const TileGrid empty;
    context.Expect(!SegmentIntersectsSolidTileGrid(
                       {0.0f, 0.0f}, {10.0f, 10.0f}, {}, 16.0f, empty,
                       0.0f, 0.0f),
                   "canonical empty grid blocks nothing");

    TileGrid malformed = grid;
    malformed.cells.pop_back();
    context.Expect(SegmentIntersectsSolidTileGrid(
                       {0.0f, 0.0f}, {10.0f, 10.0f}, origin, 16.0f,
                       malformed, 0.0f, 0.0f),
                   "malformed tile grid fails closed");
    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    context.Expect(SegmentIntersectsSolidTileGrid(
                       {0.0f, 0.0f}, {10.0f, 10.0f}, origin, 16.0f, grid,
                       nan, 0.0f),
                   "invalid tile clearance fails closed");
}

} // namespace

void RunGeometry2DTests(TestContext& context) {
    TestPointAndCircleRectangle(context);
    TestSegmentCircle(context);
    TestSegmentRectangle(context);
    TestWorldPointToTileCell(context);
    TestIrregularTileStampHit(context);
    TestSolidTileGridSegments(context);
}

} // namespace object_connect::tests
