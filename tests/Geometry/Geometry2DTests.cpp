#include "TestSupport.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <limits>
#include <vector>

namespace object_connect::tests {
namespace {

void TestAxisAlignedBoxValidityAndPointHit(TestContext& context) {
    const AxisAlignedBox box{{16.0f, 32.0f}, {48.0f, 64.0f}};
    context.Expect(IsValidAxisAlignedBox(box), "ordered finite AABB is valid");
    context.Expect(PointInAxisAlignedBox({32.0f, 48.0f}, box),
                   "point inside a tile AABB hits");
    context.Expect(PointInAxisAlignedBox({16.0f, 64.0f}, box),
                   "AABB boundary is part of the hit area");
    context.Expect(!PointInAxisAlignedBox({15.99f, 48.0f}, box),
                   "point outside a tile AABB misses");

    const AxisAlignedBox reversed{{48.0f, 32.0f}, {16.0f, 64.0f}};
    context.Expect(!IsValidAxisAlignedBox(reversed),
                   "AABB with reversed limits is invalid");
    context.Expect(!PointInAxisAlignedBox({32.0f, 48.0f}, reversed),
                   "invalid AABB never receives a point hit");
}

void TestExpansion(TestContext& context) {
    const AxisAlignedBox box{{16.0f, 32.0f}, {48.0f, 64.0f}};
    const AxisAlignedBox expanded = ExpandAxisAlignedBox(box, 8.0f);
    context.Expect(expanded.minimum == Vec2{8.0f, 24.0f} &&
                       expanded.maximum == Vec2{56.0f, 72.0f},
                   "clearance expands all four AABB sides");

    const AxisAlignedBox invalid = ExpandAxisAlignedBox(box, -1.0f);
    context.Expect(!IsValidAxisAlignedBox(invalid),
                   "negative clearance produces an invalid fail-closed box");
}

void TestSegmentIntersection(TestContext& context) {
    const AxisAlignedBox box{{16.0f, 16.0f}, {32.0f, 32.0f}};
    context.Expect(SegmentIntersectsAxisAlignedBox({0.0f, 24.0f}, {48.0f, 24.0f}, box),
                   "segment crossing a tile AABB intersects");
    context.Expect(!SegmentIntersectsAxisAlignedBox({0.0f, 8.0f}, {48.0f, 8.0f}, box),
                   "parallel segment outside a tile AABB misses");
    context.Expect(SegmentIntersectsAxisAlignedBox({0.0f, 16.0f}, {48.0f, 16.0f}, box),
                   "segment touching an AABB edge is blocked");
    context.Expect(SegmentIntersectsAxisAlignedBox({24.0f, 24.0f}, {24.0f, 24.0f}, box),
                   "stationary segment inside an AABB intersects");
    context.Expect(!SegmentIntersectsAxisAlignedBox({4.0f, 4.0f}, {4.0f, 4.0f}, box),
                   "stationary segment outside an AABB misses");

    const std::optional<float> entry = SegmentAxisAlignedBoxEntryTime(
        {0.0f, 24.0f}, {48.0f, 24.0f}, box);
    context.Expect(entry.has_value() && NearlyEqual(*entry, 1.0f / 3.0f, 0.0001f),
                   "AABB entry time identifies the first blocking boundary");
    const std::optional<float> insideEntry = SegmentAxisAlignedBoxEntryTime(
        {24.0f, 24.0f}, {48.0f, 24.0f}, box);
    context.Expect(insideEntry.has_value() && NearlyEqual(*insideEntry, 0.0f),
                   "segment beginning inside an AABB enters at time zero");
    context.Expect(!SegmentAxisAlignedBoxEntryTime(
                        {0.0f, 8.0f}, {48.0f, 8.0f}, box).has_value(),
                   "clear segment has no AABB entry time");
}

void TestAnyBoxAndClearance(TestContext& context) {
    const std::vector<AxisAlignedBox> boxes{
        {{32.0f, 32.0f}, {48.0f, 48.0f}},
        {{96.0f, 32.0f}, {112.0f, 48.0f}},
    };
    context.Expect(!SegmentIntersectsAnyAxisAlignedBox(
                       {0.0f, 23.0f}, {128.0f, 23.0f}, boxes, 8.0f),
                   "segment beyond dead-node clearance remains clear");
    context.Expect(SegmentIntersectsAnyAxisAlignedBox(
                       {0.0f, 24.0f}, {128.0f, 24.0f}, boxes, 8.0f),
                   "touching expanded dead-node boundary is blocked");
    context.Expect(SegmentIntersectsAnyAxisAlignedBox(
                       {0.0f, 24.0f}, {128.0f, 24.0f}, boxes, -1.0f),
                   "invalid clearance fails closed");

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    context.Expect(SegmentIntersectsAnyAxisAlignedBox(
                       {nan, 0.0f}, {128.0f, 24.0f}, boxes, 0.0f),
                   "non-finite segment fails closed");
}

} // namespace

void RunGeometry2DTests(TestContext& context) {
    TestAxisAlignedBoxValidityAndPointHit(context);
    TestExpansion(context);
    TestSegmentIntersection(context);
    TestAnyBoxAndClearance(context);
}

} // namespace object_connect::tests
