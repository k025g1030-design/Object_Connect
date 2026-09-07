#include "TestSupport.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <cmath>
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

void TestWrappedPointCanonicalization(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {1000.0f, 600.0f}};
    const CanonicalWrappedPoint maximum =
        CanonicalizeWrappedPoint({1000.0f, 600.0f}, bounds);
    context.Expect(maximum.valid && maximum.position == Vec2{0.0f, 0.0f} &&
                       maximum.winding == WrapWinding{1, 1},
                   "half-open maxima canonicalize to the opposite minima");

    const CanonicalWrappedPoint negative =
        CanonicalizeWrappedPoint({-1.0f, -1.0f}, bounds);
    context.Expect(negative.valid &&
                       negative.position == Vec2{999.0f, 599.0f} &&
                       negative.winding == WrapWinding{-1, -1},
                   "negative coordinates retain their image winding");

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    context.Expect(!CanonicalizeWrappedPoint({nan, 0.0f}, bounds).valid &&
                       !CanonicalizeWrappedPoint(
                            {10.0f, 10.0f}, {{0.0f, 0.0f}, {0.0f, 600.0f}})
                            .valid,
                   "canonicalization rejects non-finite points and empty bounds");
}

void TestFourDirectionWrappedPaths(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {1000.0f, 600.0f}};
    const std::optional<WrappedPath> right =
        BuildWrappedPath({990.0f, 100.0f}, {1010.0f, 100.0f}, bounds);
    context.Expect(right.has_value() && right->visibleSegments.size() == 2 &&
                       right->visibleSegments[0].start == Vec2{990.0f, 100.0f} &&
                       right->visibleSegments[0].end == Vec2{1000.0f, 100.0f} &&
                       right->visibleSegments[1].start == Vec2{0.0f, 100.0f} &&
                       right->visibleSegments[1].end == Vec2{10.0f, 100.0f} &&
                       right->canonicalEnd == Vec2{10.0f, 100.0f} &&
                       NearlyEqual(right->length, 20.0f) &&
                       right->drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}, {-1000.0f, 0.0f}},
                   "right-to-left wrap splits the visible path without charging the portal jump");

    const std::optional<WrappedPath> left =
        BuildWrappedPath({10.0f, 100.0f}, {-10.0f, 100.0f}, bounds);
    context.Expect(left.has_value() && left->visibleSegments.size() == 2 &&
                       left->canonicalEnd == Vec2{990.0f, 100.0f} &&
                       NearlyEqual(left->length, 20.0f) &&
                       left->drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}, {1000.0f, 0.0f}},
                   "left-to-right wrap preserves the remaining displacement");

    const std::optional<WrappedPath> down =
        BuildWrappedPath({100.0f, 590.0f}, {100.0f, 610.0f}, bounds);
    const std::optional<WrappedPath> up =
        BuildWrappedPath({100.0f, 10.0f}, {100.0f, -10.0f}, bounds);
    context.Expect(down.has_value() && down->visibleSegments.size() == 2 &&
                       down->canonicalEnd == Vec2{100.0f, 10.0f} &&
                       NearlyEqual(down->length, 20.0f),
                   "down-to-up wrap preserves its vertical remainder");
    context.Expect(up.has_value() && up->visibleSegments.size() == 2 &&
                       up->canonicalEnd == Vec2{100.0f, 590.0f} &&
                       NearlyEqual(up->length, 20.0f),
                   "up-to-down wrap preserves its vertical remainder");
}

void TestCornerBoundaryAndMultipleWraps(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {1000.0f, 600.0f}};
    const std::optional<WrappedPath> corner =
        BuildWrappedPath({990.0f, 590.0f}, {1010.0f, 610.0f}, bounds);
    context.Expect(corner.has_value() && corner->visibleSegments.size() == 2 &&
                       corner->visibleSegments[0].end ==
                           Vec2{1000.0f, 600.0f} &&
                       corner->visibleSegments[1].start == Vec2{0.0f, 0.0f} &&
                       corner->endWinding == WrapWinding{1, 1} &&
                       NearlyEqual(corner->length, std::sqrt(800.0f), 0.001f),
                   "simultaneous corner crossing is one DDA event");

    const std::optional<WrappedPath> staggered =
        BuildWrappedPath({990.0f, 500.0f}, {1010.0f, 610.0f}, bounds);
    context.Expect(staggered.has_value() &&
                       staggered->visibleSegments.size() == 3 &&
                       staggered->drawTranslations.size() == 3,
                   "non-simultaneous horizontal and vertical crossings remain ordered");

    const std::optional<WrappedPath> exactPositive =
        BuildWrappedPath({990.0f, 100.0f}, {1000.0f, 100.0f}, bounds);
    const std::optional<WrappedPath> exactNegative =
        BuildWrappedPath({0.0f, 100.0f}, {-10.0f, 100.0f}, bounds);
    context.Expect(exactPositive.has_value() &&
                       exactPositive->visibleSegments.size() == 1 &&
                       exactPositive->canonicalEnd == Vec2{0.0f, 100.0f} &&
                       NearlyEqual(exactPositive->length, 10.0f),
                   "ending exactly at a half-open maximum does not loop");
    context.Expect(exactNegative.has_value() &&
                       exactNegative->visibleSegments.size() == 1 &&
                       exactNegative->visibleSegments[0].start ==
                           Vec2{1000.0f, 100.0f} &&
                       NearlyEqual(exactNegative->length, 10.0f),
                   "negative movement from a minimum skips a zero-length portal segment");

    const std::optional<WrappedPath> multiple =
        BuildWrappedPath({10.0f, 100.0f}, {2020.0f, 100.0f}, bounds);
    context.Expect(multiple.has_value() && multiple->visibleSegments.size() == 3 &&
                       multiple->drawTranslations.size() == 3 &&
                       multiple->canonicalEnd == Vec2{20.0f, 100.0f} &&
                       multiple->endWinding == WrapWinding{2, 0} &&
                       NearlyEqual(multiple->length, 2010.0f, 0.01f),
                   "fast motion can cross multiple images in one update");

    const std::optional<WrappedPath> multipleReverse =
        BuildWrappedPath({990.0f, 100.0f}, {-1020.0f, 100.0f}, bounds);
    context.Expect(multipleReverse.has_value() &&
                       multipleReverse->visibleSegments.size() == 3 &&
                       multipleReverse->drawTranslations.size() == 3 &&
                       multipleReverse->canonicalEnd == Vec2{980.0f, 100.0f} &&
                       multipleReverse->endWinding == WrapWinding{-2, 0} &&
                       NearlyEqual(multipleReverse->length, 2010.0f, 0.01f),
                   "fast reverse motion preserves every crossed image");

    const std::optional<WrappedPath> stationary =
        BuildWrappedPath({250.0f, 300.0f}, {250.0f, 300.0f}, bounds);
    context.Expect(stationary.has_value() &&
                       stationary->visibleSegments.size() == 1 &&
                       stationary->drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}} &&
                       stationary->canonicalEnd == Vec2{250.0f, 300.0f} &&
                       NearlyEqual(stationary->length, 0.0f),
                   "zero displacement remains a stable single-image path");
}

void TestWrapCrossingCapAndDisabledPath(TestContext& context) {
    const AxisAlignedBox unitBounds{{0.0f, 0.0f}, {1.0f, 1.0f}};
    const std::optional<WrappedPath> atCap =
        BuildWrappedPath({0.5f, 0.5f}, {256.5f, 0.5f}, unitBounds);
    context.Expect(atCap.has_value() &&
                       atCap->visibleSegments.size() == 257 &&
                       atCap->drawTranslations.size() == 257,
                   "the defensive cap permits exactly 256 crossing events");
    context.Expect(!BuildWrappedPath(
                        {0.5f, 0.5f}, {257.5f, 0.5f}, unitBounds)
                        .has_value(),
                   "the defensive cap rejects a 257th crossing event");
    const float infinity = (std::numeric_limits<float>::infinity)();
    context.Expect(!BuildWrappedPath(
                        {0.5f, 0.5f}, {infinity, 0.5f}, unitBounds)
                        .has_value() &&
                       !BuildWrappedPath(
                            {0.5f, 0.5f}, {0.75f, 0.5f},
                            {{0.0f, 0.0f}, {0.0f, 1.0f}})
                            .has_value(),
                   "wrapped paths fail closed for non-finite input and empty bounds");

    const std::optional<WrappedPath> disabled = BuildWrappedPath(
        {990.0f, 100.0f}, {1010.0f, 100.0f}, {}, false);
    context.Expect(disabled.has_value() &&
                       disabled->visibleSegments.size() == 1 &&
                       disabled->drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}} &&
                       disabled->canonicalEnd == Vec2{1010.0f, 100.0f} &&
                       NearlyEqual(disabled->length, 20.0f),
                   "disabled wrapping retains the legacy direct path without bounds");
}

} // namespace

void RunGeometry2DTests(TestContext& context) {
    TestAxisAlignedBoxValidityAndPointHit(context);
    TestExpansion(context);
    TestSegmentIntersection(context);
    TestAnyBoxAndClearance(context);
    TestWrappedPointCanonicalization(context);
    TestFourDirectionWrappedPaths(context);
    TestCornerBoundaryAndMultipleWraps(context);
    TestWrapCrossingCapAndDisabledPath(context);
}

} // namespace object_connect::tests
