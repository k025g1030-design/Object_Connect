#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace object_connect {
namespace {

[[nodiscard]] bool IsFiniteScalar(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool HasValidCircle(const Vec2 center, const float radius) noexcept {
    return IsFinite(center) && IsFiniteScalar(radius) && radius >= 0.0f;
}

[[nodiscard]] bool HasValidRectangle(const Vec2 center, const float width,
                                     const float height) noexcept {
    return IsFinite(center) && IsFiniteScalar(width) && IsFiniteScalar(height) &&
           width >= 0.0f && height >= 0.0f;
}

[[nodiscard]] bool HasExpectedCellCount(const std::size_t columns,
                                        const std::size_t rows,
                                        const std::size_t actualCount) noexcept {
    if (columns == 0 || rows == 0) {
        return columns == 0 && rows == 0 && actualCount == 0;
    }
    if (columns > (std::numeric_limits<std::size_t>::max)() / rows) {
        return false;
    }
    return columns * rows == actualCount;
}

} // namespace

bool PointInCircle(const Vec2 point, const Vec2 center, const float radius) noexcept {
    if (!IsFinite(point) || !HasValidCircle(center, radius)) {
        return false;
    }
    const Vec2 offset = point - center;
    return static_cast<double>(offset.x) * offset.x +
               static_cast<double>(offset.y) * offset.y <=
           static_cast<double>(radius) * radius;
}

bool CircleOverlapsRectangle(const Vec2 circleCenter, const float circleRadius,
                             const Vec2 rectangleCenter, const float width,
                             const float height) noexcept {
    if (!HasValidCircle(circleCenter, circleRadius) ||
        !HasValidRectangle(rectangleCenter, width, height)) {
        return false;
    }

    const double halfWidth = static_cast<double>(width) * 0.5;
    const double halfHeight = static_cast<double>(height) * 0.5;
    const double closestX = std::clamp(static_cast<double>(circleCenter.x),
                                       static_cast<double>(rectangleCenter.x) - halfWidth,
                                       static_cast<double>(rectangleCenter.x) + halfWidth);
    const double closestY = std::clamp(static_cast<double>(circleCenter.y),
                                       static_cast<double>(rectangleCenter.y) - halfHeight,
                                       static_cast<double>(rectangleCenter.y) + halfHeight);
    const double deltaX = static_cast<double>(circleCenter.x) - closestX;
    const double deltaY = static_cast<double>(circleCenter.y) - closestY;
    return deltaX * deltaX + deltaY * deltaY <=
           static_cast<double>(circleRadius) * circleRadius;
}

bool SegmentIntersectsCircle(const Vec2 start, const Vec2 end, const Vec2 center,
                             const float radius) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !HasValidCircle(center, radius)) {
        return false;
    }

    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaY = static_cast<double>(end.y) - start.y;
    const double lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (lengthSquared <= static_cast<double>((std::numeric_limits<float>::epsilon)())) {
        return PointInCircle(start, center, radius);
    }

    const double centerX = static_cast<double>(center.x) - start.x;
    const double centerY = static_cast<double>(center.y) - start.y;
    const double time = std::clamp((centerX * deltaX + centerY * deltaY) / lengthSquared,
                                   0.0, 1.0);
    const double closestX = static_cast<double>(start.x) + deltaX * time;
    const double closestY = static_cast<double>(start.y) + deltaY * time;
    const double offsetX = static_cast<double>(center.x) - closestX;
    const double offsetY = static_cast<double>(center.y) - closestY;
    return offsetX * offsetX + offsetY * offsetY <=
           static_cast<double>(radius) * radius;
}

bool SegmentIntersectsRectangle(const Vec2 start, const Vec2 end, const Vec2 center,
                                const float width, const float height) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !HasValidRectangle(center, width, height)) {
        return false;
    }

    const double minimumX = static_cast<double>(center.x) - width * 0.5;
    const double maximumX = static_cast<double>(center.x) + width * 0.5;
    const double minimumY = static_cast<double>(center.y) - height * 0.5;
    const double maximumY = static_cast<double>(center.y) + height * 0.5;
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaY = static_cast<double>(end.y) - start.y;
    double minimumTime = 0.0;
    double maximumTime = 1.0;

    const auto clipAxis = [&minimumTime, &maximumTime](const double origin,
                                                       const double delta,
                                                       const double minimum,
                                                       const double maximum) noexcept {
        if (std::abs(delta) <= (std::numeric_limits<double>::epsilon)()) {
            return origin >= minimum && origin <= maximum;
        }
        double entry = (minimum - origin) / delta;
        double exit = (maximum - origin) / delta;
        if (entry > exit) {
            std::swap(entry, exit);
        }
        minimumTime = (std::max)(minimumTime, entry);
        maximumTime = (std::min)(maximumTime, exit);
        return minimumTime <= maximumTime;
    };

    return clipAxis(start.x, deltaX, minimumX, maximumX) &&
           clipAxis(start.y, deltaY, minimumY, maximumY);
}

std::optional<TileCoordinate> WorldPointToTileCell(
    const Vec2 point, const Vec2 gridOrigin, const float tileSize,
    const std::size_t columns, const std::size_t rows) noexcept {
    if (!IsFinite(point) || !IsFinite(gridOrigin) || !IsFiniteScalar(tileSize) ||
        tileSize <= 0.0f || columns == 0 || rows == 0) {
        return std::nullopt;
    }

    const double localX = static_cast<double>(point.x) - gridOrigin.x;
    const double localY = static_cast<double>(point.y) - gridOrigin.y;
    const double gridWidth = static_cast<double>(columns) * tileSize;
    const double gridHeight = static_cast<double>(rows) * tileSize;
    if (!std::isfinite(gridWidth) || !std::isfinite(gridHeight) ||
        localX < 0.0 || localY < 0.0 || localX >= gridWidth ||
        localY >= gridHeight) {
        return std::nullopt;
    }

    const auto column = static_cast<std::size_t>(
        std::floor(localX / static_cast<double>(tileSize)));
    const auto row = static_cast<std::size_t>(
        std::floor(localY / static_cast<double>(tileSize)));
    if (column >= columns || row >= rows) {
        return std::nullopt;
    }
    return TileCoordinate{column, row};
}

bool PointHitsOccupiedTileStamp(const Vec2 point, const Vec2 stampOrigin,
                                const float tileSize,
                                const TileStamp& stamp) noexcept {
    if (!HasExpectedCellCount(stamp.columns, stamp.rows,
                              stamp.occupiedMask.size())) {
        return false;
    }
    const std::optional<TileCoordinate> cell = WorldPointToTileCell(
        point, stampOrigin, tileSize, stamp.columns, stamp.rows);
    return cell.has_value() && stamp.IsOccupied(cell->column, cell->row);
}

bool SegmentIntersectsSolidTileGrid(
    const Vec2 start, const Vec2 end, const Vec2 gridOrigin,
    const float tileSize, const TileGrid& grid, const float clearance,
    const float pixelPadding) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !IsFinite(gridOrigin) ||
        !IsFiniteScalar(tileSize) || tileSize <= 0.0f ||
        !IsFiniteScalar(clearance) || clearance < 0.0f ||
        !IsFiniteScalar(pixelPadding) || pixelPadding < 0.0f) {
        return true;
    }
    if (grid.columns == 0 && grid.rows == 0 && grid.cells.empty()) {
        return false;
    }
    if (!HasExpectedCellCount(grid.columns, grid.rows, grid.cells.size())) {
        return true;
    }

    const double expansion = static_cast<double>(clearance) + pixelPadding;
    const double expandedSize = static_cast<double>(tileSize) + expansion * 2.0;
    if (!std::isfinite(expansion) || !std::isfinite(expandedSize) ||
        expandedSize > (std::numeric_limits<float>::max)()) {
        return true;
    }

    for (std::size_t row = 0; row < grid.rows; ++row) {
        for (std::size_t column = 0; column < grid.columns; ++column) {
            const std::size_t index = row * grid.columns + column;
            if (grid.cells[index] == 0) {
                continue;
            }

            const double centerX = static_cast<double>(gridOrigin.x) +
                (static_cast<double>(column) + 0.5) * tileSize;
            const double centerY = static_cast<double>(gridOrigin.y) +
                (static_cast<double>(row) + 0.5) * tileSize;
            if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
                std::abs(centerX) > (std::numeric_limits<float>::max)() ||
                std::abs(centerY) > (std::numeric_limits<float>::max)()) {
                return true;
            }

            if (SegmentIntersectsRectangle(
                    start, end,
                    {static_cast<float>(centerX), static_cast<float>(centerY)},
                    static_cast<float>(expandedSize),
                    static_cast<float>(expandedSize))) {
                return true;
            }
        }
    }
    return false;
}

} // namespace object_connect
