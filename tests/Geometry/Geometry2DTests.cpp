#include "TestSupport.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <limits>
#include <vector>

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

void TestObstacleClearance(TestContext& context) {
    ObstacleDefinition rectangle;
    rectangle.shape = ObstacleShape::Rectangle;
    rectangle.center = {5.0f, 3.0f};
    rectangle.width = 2.0f;
    rectangle.height = 2.0f;

    ObstacleDefinition circle;
    circle.shape = ObstacleShape::Circle;
    circle.center = {8.0f, 5.0f};
    circle.radius = 1.0f;

    const std::vector<ObstacleDefinition> obstacles{rectangle, circle};
    context.Expect(!IsConnectionBlocked({0.0f, 0.0f}, {10.0f, 0.0f}, obstacles, 0.0f),
                   "clear connection misses all obstacles");
    context.Expect(IsConnectionBlocked({0.0f, 0.0f}, {10.0f, 0.0f}, obstacles, 2.0f),
                   "vessel clearance expands a rectangle into the route");
    context.Expect(IsConnectionBlocked({0.0f, 5.0f}, {10.0f, 5.0f}, obstacles, 0.0f),
                   "connection through a circle is blocked");

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    context.Expect(IsConnectionBlocked({0.0f, 0.0f}, {10.0f, 0.0f}, obstacles, nan),
                   "invalid clearance fails closed");
    context.Expect(IsConnectionBlocked({nan, 0.0f}, {10.0f, 0.0f}, obstacles, 0.0f),
                   "invalid segment coordinates fail closed");

    ObstacleDefinition invalid = rectangle;
    invalid.width = -1.0f;
    context.Expect(IsConnectionBlocked({0.0f, 0.0f}, {10.0f, 0.0f},
                                       std::vector<ObstacleDefinition>{invalid}, 0.0f),
                   "invalid obstacle geometry fails closed");
}

} // namespace

void RunGeometry2DTests(TestContext& context) {
    TestPointAndCircleRectangle(context);
    TestSegmentCircle(context);
    TestSegmentRectangle(context);
    TestObstacleClearance(context);
}

} // namespace object_connect::tests
