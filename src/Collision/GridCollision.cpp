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

void ValidateObstacle(const CircleObstacle& obstacle) {
    if (!std::isfinite(obstacle.center.x) ||
        !std::isfinite(obstacle.center.z)) {
        throw std::invalid_argument("circle obstacle center must be finite");
    }
    if (!std::isfinite(obstacle.radius) || obstacle.radius <= 0.0f) {
        throw std::invalid_argument(
            "circle obstacle radius must be finite and greater than zero");
    }
}

[[nodiscard]] bool OverlapsCircleUnchecked(
    const Float2 center,
    const float radius,
    const CircleObstacle& obstacle) noexcept {
    const double differenceX =
        static_cast<double>(center.x) - static_cast<double>(obstacle.center.x);
    const double differenceZ =
        static_cast<double>(center.z) - static_cast<double>(obstacle.center.z);
    // 半径はゲーム内と同じ float 精度で合成する。これにより、例えば
    // 0.2f + 0.3f の位置に置いた円も厳密な相切として扱える。
    const double combinedRadius =
        static_cast<double>(radius + obstacle.radius);
    return differenceX * differenceX + differenceZ * differenceZ <
           combinedRadius * combinedRadius;
}

[[nodiscard]] bool OverlapsAnyCircleUnchecked(
    const Float2 center,
    const float radius,
    const std::span<const CircleObstacle> obstacles) noexcept {
    return std::ranges::any_of(
        obstacles,
        [center, radius](const CircleObstacle& obstacle) {
            return OverlapsCircleUnchecked(center, radius, obstacle);
        });
}

[[nodiscard]] double FindCircleSweepFraction(
    const Float2 start,
    const Float2 displacement,
    const float radius,
    const std::span<const CircleObstacle> obstacles) noexcept {
    const double velocityX = static_cast<double>(displacement.x);
    const double velocityZ = static_cast<double>(displacement.z);
    const double velocitySquared =
        velocityX * velocityX + velocityZ * velocityZ;
    if (velocitySquared == 0.0) {
        return 1.0;
    }

    double allowedFraction = 1.0;
    for (const CircleObstacle& obstacle : obstacles) {
        const double differenceX =
            static_cast<double>(start.x) - static_cast<double>(obstacle.center.x);
        const double differenceZ =
            static_cast<double>(start.z) - static_cast<double>(obstacle.center.z);
        const double combinedRadius =
            static_cast<double>(radius + obstacle.radius);
        const double distanceFromSurface =
            differenceX * differenceX + differenceZ * differenceZ -
            combinedRadius * combinedRadius;
        const double approach =
            differenceX * velocityX + differenceZ * velocityZ;

        // 接点からの接線移動と、障害物から離れる移動は妨げない。
        if (approach >= 0.0) {
            continue;
        }
        if (distanceFromSurface <= 0.0) {
            allowedFraction = 0.0;
            continue;
        }

        const double discriminant =
            approach * approach - velocitySquared * distanceFromSurface;
        if (discriminant < 0.0) {
            continue;
        }

        const double entryFraction =
            (-approach - std::sqrt(discriminant)) / velocitySquared;
        if (entryFraction >= 0.0 && entryFraction <= allowedFraction) {
            allowedFraction = entryFraction;
        }
    }

    return std::clamp(allowedFraction, 0.0, 1.0);
}

[[nodiscard]] Float2 MoveAxisAgainstCircles(
    const Float2 start,
    const Float2 displacement,
    const float radius,
    const std::span<const CircleObstacle> obstacles) noexcept {
    const double fraction =
        FindCircleSweepFraction(start, displacement, radius, obstacles);
    const auto positionAt = [start, displacement](const double value) {
        return Float2{
            start.x + displacement.x * static_cast<float>(value),
            start.z + displacement.z * static_cast<float>(value),
        };
    };

    Float2 candidate = positionAt(fraction);
    if (!OverlapsAnyCircleUnchecked(candidate, radius, obstacles)) {
        return candidate;
    }

    // 二次方程式の接触点を float に戻す丸めで内側に入る場合だけ、安全側へ
    // 戻す。low は開始地点なので常に非重複である。
    double low = 0.0;
    double high = fraction;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const double middle = (low + high) * 0.5;
        if (OverlapsAnyCircleUnchecked(positionAt(middle), radius, obstacles)) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return positionAt(low);
}

} // namespace

bool GridCollision::OverlapsCircle(
    const Float2 center,
    const float radius,
    const CircleObstacle& obstacle) {
    if (!std::isfinite(center.x) || !std::isfinite(center.z)) {
        throw std::invalid_argument("circle center must be finite");
    }
    if (!std::isfinite(radius) || radius <= 0.0f) {
        throw std::invalid_argument("circle radius must be finite and greater than zero");
    }
    ValidateObstacle(obstacle);
    return OverlapsCircleUnchecked(center, radius, obstacle);
}

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
            if (!map.IsSolid(
                    static_cast<std::ptrdiff_t>(row),
                    static_cast<std::ptrdiff_t>(column))) {
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
    return MoveCircle(
        map,
        start,
        displacement,
        radius,
        std::span<const CircleObstacle>{},
        cellSize);
}

Float2 GridCollision::MoveCircle(
    const GridMap& map,
    const Float2 start,
    const Float2 displacement,
    const float radius,
    const std::span<const CircleObstacle> obstacles,
    const float cellSize) {
    ValidateArguments(start, radius, cellSize);
    if (!std::isfinite(displacement.x) || !std::isfinite(displacement.z)) {
        throw std::invalid_argument("circle displacement must be finite");
    }
    for (const CircleObstacle& obstacle : obstacles) {
        ValidateObstacle(obstacle);
    }
    if (OverlapsAnyCircleUnchecked(start, radius, obstacles)) {
        throw std::invalid_argument(
            "circle start must not overlap a dynamic obstacle");
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
        const Float2 xCandidate = MoveAxisAgainstCircles(
            result, {step.x, 0.0f}, radius, obstacles);
        if (!OverlapsSolid(map, xCandidate, radius, cellSize)) {
            result = xCandidate;
        }

        const Float2 zCandidate = MoveAxisAgainstCircles(
            result, {0.0f, step.z}, radius, obstacles);
        if (!OverlapsSolid(map, zCandidate, radius, cellSize)) {
            result = zCandidate;
        }
    }

    return result;
}

} // namespace fps
