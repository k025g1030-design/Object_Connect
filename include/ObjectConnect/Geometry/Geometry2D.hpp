#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <cstddef>
#include <optional>

namespace object_connect {

[[nodiscard]] bool PointInCircle(Vec2 point, Vec2 center, float radius) noexcept;
[[nodiscard]] bool CircleOverlapsRectangle(Vec2 circleCenter, float circleRadius,
                                           Vec2 rectangleCenter, float width,
                                           float height) noexcept;
[[nodiscard]] bool SegmentIntersectsCircle(Vec2 start, Vec2 end, Vec2 center,
                                           float radius) noexcept;
[[nodiscard]] bool SegmentIntersectsRectangle(Vec2 start, Vec2 end, Vec2 center,
                                              float width, float height) noexcept;
// Converts a world-space point to a zero-based tile coordinate. The grid uses
// half-open bounds: its left/top edges are inside and its right/bottom edges
// are outside.
[[nodiscard]] std::optional<TileCoordinate> WorldPointToTileCell(
    Vec2 point, Vec2 gridOrigin, float tileSize, std::size_t columns,
    std::size_t rows) noexcept;

// Tests the stamp's irregular occupied mask. stampOrigin is the world-space
// top-left corner of cell (0, 0); TileStamp::anchor does not change the stamp's
// occupied area.
[[nodiscard]] bool PointHitsOccupiedTileStamp(
    Vec2 point, Vec2 stampOrigin, float tileSize,
    const TileStamp& stamp) noexcept;

// Tile ID zero is empty and every nonzero cell is solid. Each solid cell AABB
// expands by clearance + pixelPadding on every side. Invalid input fails
// closed, while a canonical empty grid (0 x 0 with no cells) blocks nothing.
[[nodiscard]] bool SegmentIntersectsSolidTileGrid(
    Vec2 start, Vec2 end, Vec2 gridOrigin, float tileSize,
    const TileGrid& grid, float clearance, float pixelPadding) noexcept;

} // namespace object_connect
