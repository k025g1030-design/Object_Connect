#pragma once

#include "ObjectConnect/Math/Color.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <span>
#include <vector>

namespace object_connect {

inline constexpr float kDefaultTentaclePixelGridSize = 2.0f;

struct TentacleStyle final {
    float baseWidth = 16.0f;
    float tipWidth = 16.0f;
    float widthVariation = 0.16f;
    float widthPhase = 0.0f;
    float pixelGridSize = kDefaultTentaclePixelGridSize;
    Color color{134.0f / 255.0f, 27.0f / 255.0f, 43.0f / 255.0f, 1.0f};
};

struct RibbonVertex final {
    Vec2 position{};
    Color color{};
};

[[nodiscard]] std::vector<RibbonVertex> BuildRibbonStrip(
    std::span<const Vec2> centerline, const TentacleStyle& style);

} // namespace object_connect
