#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <span>

namespace object_connect {

[[nodiscard]] bool PointInCircle(Vec2 point, Vec2 center, float radius) noexcept;
[[nodiscard]] bool CircleOverlapsRectangle(Vec2 circleCenter, float circleRadius,
                                           Vec2 rectangleCenter, float width,
                                           float height) noexcept;
[[nodiscard]] bool SegmentIntersectsCircle(Vec2 start, Vec2 end, Vec2 center,
                                           float radius) noexcept;
[[nodiscard]] bool SegmentIntersectsRectangle(Vec2 start, Vec2 end, Vec2 center,
                                              float width, float height) noexcept;
[[nodiscard]] bool IsConnectionBlocked(Vec2 start, Vec2 end,
                                       std::span<const ObstacleDefinition> obstacles,
                                       float clearance) noexcept;

} // namespace object_connect
