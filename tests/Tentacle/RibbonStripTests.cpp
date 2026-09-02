#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include "TestSupport.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace object_connect::tests {
namespace {

[[nodiscard]] bool VecNearlyEqual(const object_connect::Vec2 left,
                                  const object_connect::Vec2 right,
                                  const float tolerance = 0.0001f) noexcept {
    return NearlyEqual(left.x, right.x, tolerance) &&
           NearlyEqual(left.y, right.y, tolerance);
}

void TestPixelFleshDefault(TestContext& context) {
    constexpr std::array<object_connect::Vec2, 4> centerline{{
        {1.0f, 0.0f},
        {9.0f, 0.0f},
        {17.0f, 0.0f},
        {25.0f, 0.0f},
    }};
    const object_connect::TentacleStyle style{};
    const auto vertices = object_connect::BuildRibbonStrip(centerline, style);

    context.Expect(NearlyEqual(style.baseWidth, style.tipWidth) &&
                       style.widthVariation > 0.0f &&
                       NearlyEqual(style.pixelGridSize, 2.0f),
                   "the default flesh style keeps one nominal width with pixel roughness");
    context.Expect(NearlyEqual(style.color.r, 134.0f / 255.0f) &&
                       NearlyEqual(style.color.g, 27.0f / 255.0f) &&
                       NearlyEqual(style.color.b, 43.0f / 255.0f) &&
                       NearlyEqual(style.color.a, 1.0f),
                   "the default flesh-tentacle style uses an opaque deep crimson");
    bool foundLocalWidthChange = false;
    for (std::size_t index = 0; index < centerline.size(); ++index) {
        const std::size_t vertexIndex = index * 2;
        const float width = object_connect::Length(
            vertices[vertexIndex].position - vertices[vertexIndex + 1].position);
        foundLocalWidthChange = foundLocalWidthChange ||
                                !NearlyEqual(width, style.baseWidth, 0.001f);
        for (std::size_t side = 0; side < 2; ++side) {
            const object_connect::Vec2 position =
                vertices[vertexIndex + side].position;
            context.Expect(
                NearlyEqual(position.x / style.pixelGridSize,
                            std::round(position.x / style.pixelGridSize)) &&
                    NearlyEqual(position.y / style.pixelGridSize,
                                std::round(position.y / style.pixelGridSize)),
                "pixel flesh vertices stay on the configured render grid");
        }
    }
    context.Expect(foundLocalWidthChange,
                   "pixel roughness creates a deterministic notch in the silhouette");
}

void TestStraightStrip(TestContext& context) {
    constexpr std::array<object_connect::Vec2, 3> centerline{{
        {0.0f, 0.0f},
        {10.0f, 0.0f},
        {20.0f, 0.0f},
    }};
    object_connect::TentacleStyle style;
    style.baseWidth = 20.0f;
    style.tipWidth = 10.0f;
    style.widthVariation = 0.0f;
    style.pixelGridSize = 0.0f;
    style.color = {0.8f, 0.1f, 0.2f, 0.9f};

    const std::vector<object_connect::RibbonVertex> vertices =
        object_connect::BuildRibbonStrip(centerline, style);
    context.Expect(vertices.size() == centerline.size() * 2,
                   "RibbonStrip emits two triangle-strip vertices per point");
    context.Expect(VecNearlyEqual(vertices[0].position, {0.0f, 10.0f}) &&
                       VecNearlyEqual(vertices[1].position, {0.0f, -10.0f}),
                   "RibbonStrip applies the full base width around the centerline");
    context.Expect(VecNearlyEqual(vertices[4].position, {20.0f, 5.0f}) &&
                       VecNearlyEqual(vertices[5].position, {20.0f, -5.0f}),
                   "RibbonStrip tapers to the configured tip width");
    context.Expect(vertices[2].color == style.color &&
                       vertices[3].color == style.color,
                   "RibbonStrip copies style color to both sides");
}

void TestDeterministicVariation(TestContext& context) {
    constexpr std::array<object_connect::Vec2, 5> centerline{{
        {0.0f, 0.0f},
        {10.0f, 2.0f},
        {20.0f, 1.0f},
        {30.0f, 4.0f},
        {40.0f, 3.0f},
    }};
    object_connect::TentacleStyle style;
    style.baseWidth = 18.0f;
    style.tipWidth = 6.0f;
    style.widthVariation = 0.2f;
    style.widthPhase = 1.25f;
    style.pixelGridSize = 0.0f;

    const auto first = object_connect::BuildRibbonStrip(centerline, style);
    const auto second = object_connect::BuildRibbonStrip(centerline, style);
    context.Expect(first.size() == second.size(),
                   "Deterministic ribbon builds have matching sizes");
    for (std::size_t index = 0; index < first.size(); ++index) {
        context.Expect(VecNearlyEqual(first[index].position, second[index].position),
                       "Width variation is deterministic for a fixed phase");
    }

    for (std::size_t pointIndex = 0; pointIndex < centerline.size(); ++pointIndex) {
        const float along = static_cast<float>(pointIndex) /
                            static_cast<float>(centerline.size() - 1);
        const float nominalWidth =
            style.baseWidth + (style.tipWidth - style.baseWidth) * along;
        const float actualWidth = object_connect::Length(
            first[pointIndex * 2].position -
            first[pointIndex * 2 + 1].position);
        context.Expect(actualWidth <= nominalWidth + 0.0001f,
                       "Organic variation only cuts inward from the nominal width");
    }

    const float baseWidth = object_connect::Length(
        first[0].position - first[1].position);
    const float tipWidth = object_connect::Length(
        first[first.size() - 2].position - first.back().position);
    context.Expect(NearlyEqual(baseWidth, style.baseWidth),
                   "Organic variation preserves the exact base width");
    context.Expect(NearlyEqual(tipWidth, style.tipWidth),
                   "Organic variation preserves the exact tip width");
}

void TestDegenerateCenterline(TestContext& context) {
    constexpr std::array<object_connect::Vec2, 4> centerline{{
        {15.0f, 25.0f},
        {15.0f, 25.0f},
        {15.0f, 25.0f},
        {15.0f, 25.0f},
    }};
    object_connect::TentacleStyle style;
    const auto vertices = object_connect::BuildRibbonStrip(centerline, style);
    context.Expect(vertices.size() == centerline.size() * 2,
                   "A repeated centerline still emits a complete strip");
    for (const object_connect::RibbonVertex& vertex : vertices) {
        context.Expect(object_connect::IsFinite(vertex.position),
                       "Degenerate centerline vertices remain finite");
    }
}

void TestSingleAndNonFinitePoints(TestContext& context) {
    constexpr std::array<object_connect::Vec2, 1> single{{{4.0f, 6.0f}}};
    object_connect::TentacleStyle style;
    style.baseWidth = 12.0f;
    const auto singleVertices = object_connect::BuildRibbonStrip(single, style);
    context.Expect(singleVertices.size() == 2,
                   "A one-point centerline still follows the two-vertices-per-point contract");
    context.Expect(VecNearlyEqual(singleVertices[0].position, {4.0f, 12.0f}) &&
                       VecNearlyEqual(singleVertices[1].position, {4.0f, 0.0f}),
                   "A one-point centerline uses a stable fallback normal");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<object_connect::Vec2, 3> nonFinite{{
        {nan, nan},
        {8.0f, 4.0f},
        {8.0f, 4.0f},
    }};
    const auto sanitized = object_connect::BuildRibbonStrip(nonFinite, style);
    for (const object_connect::RibbonVertex& vertex : sanitized) {
        context.Expect(object_connect::IsFinite(vertex.position),
                       "RibbonStrip sanitizes non-finite centerline points");
    }
}

void TestEmptyStrip(TestContext& context) {
    const std::vector<object_connect::Vec2> empty;
    context.Expect(object_connect::BuildRibbonStrip(empty, {}).empty(),
                   "An empty centerline produces no ribbon vertices");
}

} // namespace

void RunRibbonStripTests(TestContext& context) {
    TestPixelFleshDefault(context);
    TestStraightStrip(context);
    TestDeterministicVariation(context);
    TestDegenerateCenterline(context);
    TestSingleAndNonFinitePoints(context);
    TestEmptyStrip(context);
}

} // namespace object_connect::tests
