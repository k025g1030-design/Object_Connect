#include "RetroFPS/Collision/GridCollision.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace fps {
namespace {

void ValidateArguments(
    const Float2 center, const float radius, const float cellSize) {
    if (!std::isfinite(center.x) || !std::isfinite(center.z)) {
        throw std::invalid_argument("circle center must be finite");
    }
    if (!std::isfinite(radius) || radius <= 0.0f) {
        throw std::invalid_argument("circle radius must be finite and greater than zero");
    }
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) {
        throw std::invalid_argument("cell size must be finite and greater than zero");
    }
}

} // namespace

bool GridCollision::OverlapsSolid(
    const GridMap& map,
    const Float2 center,
    const float radius,
    const float cellSize) {
    ValidateArguments(center, radius, cellSize);

    const std::size_t width = map.GetWidth();
    const std::size_t height = map.GetHeight();
    if (width == 0 || height == 0) {
        throw std::invalid_argument("collision map must not be empty");
    }

    const double centerX = static_cast<double>(center.x);
    const double centerZ = static_cast<double>(center.z);
    const double radiusValue = static_cast<double>(radius);
    const double cellSizeValue = static_cast<double>(cellSize);
    const double minimumX = centerX - radiusValue;
    const double maximumX = centerX + radiusValue;
    const double minimumZ = centerZ - radiusValue;
    const double maximumZ = centerZ + radiusValue;
    const double mapMaximumX = static_cast<double>(width) * cellSizeValue;
    const double mapMaximumZ = static_cast<double>(height) * cellSizeValue;

    // GridMap はマップ範囲外をすべて壁として扱う。厳密な比較により、円と壁が
    // ちょうど接する状態を許可する既存仕様を維持する。
    if (minimumX < 0.0 || minimumZ < 0.0 ||
        maximumX > mapMaximumX || maximumZ > mapMaximumZ) {
        return true;
    }

    const auto toCell = [cellSizeValue](const double value) {
        return static_cast<std::size_t>(std::floor(value / cellSizeValue));
    };
    const std::size_t minimumColumn = (std::min)(toCell(minimumX), width - 1);
    const std::size_t maximumColumn = (std::min)(toCell(maximumX), width - 1);
    const std::size_t minimumRow = (std::min)(toCell(minimumZ), height - 1);
    const std::size_t maximumRow = (std::min)(toCell(maximumZ), height - 1);
    const double radiusSquared = radiusValue * radiusValue;

    for (std::size_t row = minimumRow; row <= maximumRow; ++row) {
        for (std::size_t column = minimumColumn; column <= maximumColumn; ++column) {
            if (map.GetCell(row, column) != '#') {
                continue;
            }

            const double cellMinimumX = static_cast<double>(column) * cellSizeValue;
            const double cellMaximumX = cellMinimumX + cellSizeValue;
            const double cellMinimumZ = static_cast<double>(row) * cellSizeValue;
            const double cellMaximumZ = cellMinimumZ + cellSizeValue;
            const double nearestX = std::clamp(centerX, cellMinimumX, cellMaximumX);
            const double nearestZ = std::clamp(centerZ, cellMinimumZ, cellMaximumZ);
            const double differenceX = centerX - nearestX;
            const double differenceZ = centerZ - nearestZ;
            const double distanceSquared =
                differenceX * differenceX + differenceZ * differenceZ;

            if (distanceSquared < radiusSquared) {
                return true;
            }
        }
    }

    return false;
}

Float2 GridCollision::MoveCircle(
    const GridMap& map,
    const Float2 start,
    const Float2 displacement,
    const float radius,
    const float cellSize) {
    ValidateArguments(start, radius, cellSize);
    if (!std::isfinite(displacement.x) || !std::isfinite(displacement.z)) {
        throw std::invalid_argument("circle displacement must be finite");
    }

    const double distance = std::hypot(
        static_cast<double>(displacement.x), static_cast<double>(displacement.z));
    if (distance == 0.0) {
        return start;
    }

    const double maximumStepLength = static_cast<double>(radius) * 0.5;
    const double requestedSteps =
        std::ceil(distance / maximumStepLength);
    const double safeMaximumStepCount = std::nextafter(
        static_cast<double>((std::numeric_limits<std::size_t>::max)()), 0.0);
    if (!std::isfinite(requestedSteps) || requestedSteps > safeMaximumStepCount) {
        throw std::invalid_argument("circle displacement exceeds the supported range");
    }

    const std::size_t stepCount =
        (std::max)(std::size_t{1}, static_cast<std::size_t>(requestedSteps));
    const float inverseStepCount = 1.0f / static_cast<float>(stepCount);
    const Float2 step{
        displacement.x * inverseStepCount,
        displacement.z * inverseStepCount,
    };

    Float2 result = start;
    for (std::size_t index = 0; index < stepCount; ++index) {
        const Float2 xCandidate{result.x + step.x, result.z};
        if (!OverlapsSolid(map, xCandidate, radius, cellSize)) {
            result.x = xCandidate.x;
        }

        const Float2 zCandidate{result.x, result.z + step.z};
        if (!OverlapsSolid(map, zCandidate, radius, cellSize)) {
            result.z = zCandidate.z;
        }
    }

    return result;
}

} // namespace fps
