#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace object_connect {
namespace {

constexpr float kTangentEpsilonSquared = 0.00000001f;
constexpr float kVariationFrequency = 2.39996323f;

[[nodiscard]] float FiniteNonNegative(const float value) noexcept {
    return std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
}

[[nodiscard]] Vec2 SnapToPixelGrid(const Vec2 value,
                                   const float gridSize) noexcept {
    if (gridSize <= 0.0f) {
        return value;
    }
    return {
        std::round(value.x / gridSize) * gridSize,
        std::round(value.y / gridSize) * gridSize,
    };
}

[[nodiscard]] std::vector<Vec2> MakeFiniteCenterline(
    const std::span<const Vec2> centerline) {
    std::vector<Vec2> result(centerline.size());
    Vec2 lastFinite{};
    bool hasFinite = false;
    for (std::size_t index = 0; index < centerline.size(); ++index) {
        if (IsFinite(centerline[index])) {
            lastFinite = centerline[index];
            hasFinite = true;
        }
        result[index] = hasFinite ? lastFinite : Vec2{};
    }
    return result;
}

[[nodiscard]] Vec2 ResolveTangent(const std::span<const Vec2> points,
                                  const std::size_t index,
                                  const Vec2 fallback) noexcept {
    Vec2 tangent{};
    if (points.size() > 1) {
        if (index == 0) {
            tangent = points[1] - points[0];
        } else if (index + 1 == points.size()) {
            tangent = points[index] - points[index - 1];
        } else {
            tangent = points[index + 1] - points[index - 1];
            if (LengthSquared(tangent) <= kTangentEpsilonSquared) {
                tangent = points[index + 1] - points[index];
            }
            if (LengthSquared(tangent) <= kTangentEpsilonSquared) {
                tangent = points[index] - points[index - 1];
            }
        }
    }
    return NormalizeOr(tangent, fallback);
}

} // namespace

std::vector<RibbonVertex> BuildRibbonStrip(
    const std::span<const Vec2> centerline, const TentacleStyle& style) {
    if (centerline.empty()) {
        return {};
    }

    const std::vector<Vec2> points = MakeFiniteCenterline(centerline);
    const float baseWidth = FiniteNonNegative(style.baseWidth);
    const float tipWidth = FiniteNonNegative(style.tipWidth);
    const float variation = std::isfinite(style.widthVariation)
                                ? std::clamp(style.widthVariation, 0.0f, 0.95f)
                                : 0.0f;
    const float phase = std::isfinite(style.widthPhase) ? style.widthPhase : 0.0f;
    const float pixelGridSize = std::isfinite(style.pixelGridSize)
                                    ? (std::max)(0.0f, style.pixelGridSize)
                                    : kDefaultTentaclePixelGridSize;

    std::vector<RibbonVertex> vertices;
    vertices.reserve(points.size() * 2);
    Vec2 previousTangent{1.0f, 0.0f};
    Vec2 previousNormal{0.0f, 1.0f};
    for (std::size_t index = 0; index < points.size(); ++index) {
        const Vec2 tangent = ResolveTangent(points, index, previousTangent);
        Vec2 normal = NormalizeOr(Perpendicular(tangent), previousNormal);
        if (index != 0 && Dot(normal, previousNormal) < 0.0f) {
            normal = -normal;
        }
        previousTangent = tangent;
        previousNormal = normal;

        const float along = points.size() == 1
                                ? 0.0f
                                : static_cast<float>(index) /
                                      static_cast<float>(points.size() - 1);
        const float taperedWidth =
            baseWidth + (tipWidth - baseWidth) * along;
        // Fade the variation to zero at both ends so the root and tip widths
        // remain exact while the middle keeps a stable organic silhouette.
        const float variationEnvelope =
            std::sin(std::numbers::pi_v<float> * along);
        const float variationWave = std::fabs(
            std::sin(phase + static_cast<float>(index) * kVariationFrequency));
        // Organic roughness only cuts small notches into the declared width.
        // This keeps the nominal width as the visible maximum, so presentation
        // never creates an unvalidated bulge through level geometry.
        const float width = taperedWidth *
                            (1.0f - variation * variationEnvelope * variationWave);
        const Vec2 halfWidthOffset = normal * (width * 0.5f);
        vertices.push_back(
            {SnapToPixelGrid(points[index] + halfWidthOffset, pixelGridSize),
             style.color});
        vertices.push_back(
            {SnapToPixelGrid(points[index] - halfWidthOffset, pixelGridSize),
             style.color});
    }
    return vertices;
}

} // namespace object_connect
