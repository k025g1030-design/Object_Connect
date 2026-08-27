#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fps {
namespace {

constexpr float kPositionEpsilon = 0.0001f;
constexpr float kWaypointTolerance = 0.025f;
constexpr float kLegacyPlayerHitboxHeight = 1.8f;
constexpr float kLegacyMeleeHitboxHeight = 0.8f;
constexpr float kLegacyRangedHitboxHeight = 1.6f;

[[nodiscard]] bool IsFinite(const Float2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.z);
}

[[nodiscard]] const char* KindName(const EnemyKind kind) noexcept {
    switch (kind) {
    case EnemyKind::Melee:
        return "Melee";
    case EnemyKind::Ranged:
        return "Ranged";
    }
    return "Unknown";
}

[[nodiscard]] EnemyDefinition MakeLegacyDefinition(
    const EnemyKind kind,
    const EnemySettings& settings) {
    EnemyDefinition definition{};
    definition.id =
        kind == EnemyKind::Melee ? "legacy_melee" : "legacy_ranged";
    definition.kind = kind;
    definition.damage = 1.0f;
    definition.attackIntervalSeconds =
        kind == EnemyKind::Melee
            ? settings.meleeAttackIntervalSeconds
            : settings.rangedAttackIntervalSeconds;
    definition.maxHealth = settings.health;
    definition.defense = 0.0f;
    definition.hitboxRadius = settings.collisionRadius;
    definition.hitboxHeight =
        kind == EnemyKind::Melee
            ? kLegacyMeleeHitboxHeight
            : kLegacyRangedHitboxHeight;
    definition.renderWidth = definition.hitboxHeight;
    definition.renderHeight = definition.hitboxHeight;
    definition.texturePath = "white1x1.png";
    definition.frameWidthPixels = 1;
    definition.frameHeightPixels = 1;
    definition.animations.idle.frameCount = 3;
    definition.animations.idle.secondsPerFrame = 0.10f;
    definition.animations.moving.frameCount = 4;
    definition.animations.moving.secondsPerFrame = 0.10f;
    definition.animations.attacking.frameCount =
        kind == EnemyKind::Melee ? 6u : 5u;
    definition.animations.attacking.secondsPerFrame =
        (kind == EnemyKind::Melee
             ? settings.meleeAttackStateSeconds
             : settings.rangedAttackStateSeconds) /
        static_cast<float>(definition.animations.attacking.frameCount);
    definition.animations.attacking.eventFrameIndex =
        kind == EnemyKind::Melee ? 3u : 2u;
    if (kind == EnemyKind::Ranged) {
        definition.animations.attacking.muzzlePixel =
            EnemyAnimationPixelPoint{0, 0};
    }
    definition.animations.dead.frameCount = 4;
    definition.animations.dead.secondsPerFrame = 0.10f;
    return definition;
}

[[nodiscard]] float ClipDurationSeconds(
    const EnemyAnimationClipDefinition& clip) noexcept {
    const double duration =
        static_cast<double>(clip.frameCount) * clip.secondsPerFrame;
    return static_cast<float>((std::min)(
        duration, static_cast<double>((std::numeric_limits<float>::max)())));
}

[[nodiscard]] float DeadVisibilitySeconds(
    const EnemyDefinition& definition) noexcept {
    return (std::max)(
        kEnemyHitFlashSeconds,
        ClipDurationSeconds(definition.animations.dead));
}

[[nodiscard]] float Distance(const Float2 left, const Float2 right) noexcept {
    const double deltaX = static_cast<double>(right.x) - left.x;
    const double deltaZ = static_cast<double>(right.z) - left.z;
    const double distance = std::hypot(deltaX, deltaZ);
    return static_cast<float>((std::min)(
        distance, static_cast<double>((std::numeric_limits<float>::max)())));
}

[[nodiscard]] float SurfaceDistance(
    const Float2 left,
    const float leftRadius,
    const Float2 right,
    const float rightRadius) noexcept {
    return Distance(left, right) - leftRadius - rightRadius;
}

void AddElapsed(float& elapsedSeconds, const float deltaSeconds) noexcept {
    const double sum = static_cast<double>(elapsedSeconds) + deltaSeconds;
    elapsedSeconds = static_cast<float>((std::min)(
        sum, static_cast<double>((std::numeric_limits<float>::max)())));
}

void SubtractElapsed(float& remainingSeconds, const float deltaSeconds) noexcept {
    remainingSeconds = (std::max)(0.0f, remainingSeconds - deltaSeconds);
}

[[nodiscard]] bool SegmentIntersectsClosedCell(
    const Float2 start,
    const Float2 end,
    const GridCoordinate cell,
    const float cellSize) noexcept {
    const double minimumX = static_cast<double>(cell.column) * cellSize;
    const double maximumX = minimumX + cellSize;
    const double minimumZ = static_cast<double>(cell.row) * cellSize;
    const double maximumZ = minimumZ + cellSize;
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaZ = static_cast<double>(end.z) - start.z;
    double entryTime = 0.0;
    double exitTime = 1.0;

    const auto clipAxis = [&entryTime, &exitTime](
                              const double origin,
                              const double delta,
                              const double minimum,
                              const double maximum) noexcept {
        if (delta == 0.0) {
            return origin >= minimum && origin <= maximum;
        }

        double nearTime = (minimum - origin) / delta;
        double farTime = (maximum - origin) / delta;
        if (nearTime > farTime) {
            std::swap(nearTime, farTime);
        }
        entryTime = (std::max)(entryTime, nearTime);
        exitTime = (std::min)(exitTime, farTime);
        return entryTime <= exitTime;
    };

    return clipAxis(start.x, deltaX, minimumX, maximumX) &&
           clipAxis(start.z, deltaZ, minimumZ, maximumZ);
}

// Testing every touched closed wall cell is deliberately conservative: a ray
// that runs exactly along an edge or through a wall corner is considered
// blocked, matching a supercover traversal rather than a thin visual ray.
[[nodiscard]] bool HasWallLineOfSight(
    const GridMap& map,
    const Float2 start,
    const Float2 end,
    const float cellSize) {
    if (!map.TryGetCoordinateAtPosition(start, cellSize).has_value() ||
        !map.TryGetCoordinateAtPosition(end, cellSize).has_value()) {
        return false;
    }

    for (std::size_t row = 0; row < map.GetHeight(); ++row) {
        for (std::size_t column = 0; column < map.GetWidth(); ++column) {
            if (!map.IsSolid(
                    static_cast<std::ptrdiff_t>(row),
                    static_cast<std::ptrdiff_t>(column))) {
                continue;
            }
            if (SegmentIntersectsClosedCell(
                    start, end, {row, column}, cellSize)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] std::size_t GridIndex(
    const GridCoordinate coordinate,
    const std::size_t width) noexcept {
    return coordinate.row * width + coordinate.column;
}

[[nodiscard]] std::size_t ManhattanDistance(
    const GridCoordinate left,
    const GridCoordinate right) noexcept {
    const std::size_t rowDistance = left.row > right.row
                                        ? left.row - right.row
                                        : right.row - left.row;
    const std::size_t columnDistance = left.column > right.column
                                           ? left.column - right.column
                                           : right.column - left.column;
    return rowDistance + columnDistance;
}

[[nodiscard]] bool IsNavigationCell(
    const GridMap& map,
    const GridCoordinate coordinate,
    const float radius,
    const float cellSize,
    const std::optional<GridCoordinate> forbiddenCell = std::nullopt) {
    if (coordinate.row >= map.GetHeight() || coordinate.column >= map.GetWidth() ||
        !map.IsWalkable(
            static_cast<std::ptrdiff_t>(coordinate.row),
            static_cast<std::ptrdiff_t>(coordinate.column)) ||
        (forbiddenCell.has_value() && coordinate == *forbiddenCell)) {
        return false;
    }
    return !GridCollision::OverlapsSolid(
        map, map.GetCellCenter(coordinate, cellSize), radius, cellSize);
}

struct OpenNode final {
    GridCoordinate coordinate{};
    std::size_t pathCost = 0;
    std::size_t heuristic = 0;
};

struct OpenNodeCompare final {
    [[nodiscard]] bool operator()(const OpenNode& left, const OpenNode& right) const noexcept {
        const std::size_t leftMaximum =
            (std::numeric_limits<std::size_t>::max)() - left.heuristic;
        const std::size_t rightMaximum =
            (std::numeric_limits<std::size_t>::max)() - right.heuristic;
        const std::size_t leftTotal = left.pathCost > leftMaximum
                                          ? (std::numeric_limits<std::size_t>::max)()
                                          : left.pathCost + left.heuristic;
        const std::size_t rightTotal = right.pathCost > rightMaximum
                                           ? (std::numeric_limits<std::size_t>::max)()
                                           : right.pathCost + right.heuristic;
        if (leftTotal != rightTotal) {
            return leftTotal > rightTotal;
        }
        if (left.heuristic != right.heuristic) {
            return left.heuristic > right.heuristic;
        }
        if (left.coordinate.row != right.coordinate.row) {
            return left.coordinate.row > right.coordinate.row;
        }
        return left.coordinate.column > right.coordinate.column;
    }
};

[[nodiscard]] std::vector<GridCoordinate> FindPath(
    const GridMap& map,
    const GridCoordinate start,
    const GridCoordinate goal,
    const float radius,
    const float cellSize,
    const std::optional<GridCoordinate> forbiddenCell = std::nullopt) {
    const std::optional<GridCoordinate> startForbidden =
        forbiddenCell.has_value() && start == *forbiddenCell
            ? std::nullopt
            : forbiddenCell;
    if (!IsNavigationCell(map, start, radius, cellSize, startForbidden) ||
        !IsNavigationCell(map, goal, radius, cellSize, forbiddenCell)) {
        return {};
    }
    if (start == goal) {
        return {start};
    }

    const std::size_t width = map.GetWidth();
    const std::size_t cellCount = width * map.GetHeight();
    const std::size_t unreachable = (std::numeric_limits<std::size_t>::max)();
    std::vector<std::size_t> costs(cellCount, unreachable);
    std::vector<std::size_t> parents(cellCount, unreachable);
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenNodeCompare> open;

    costs[GridIndex(start, width)] = 0;
    open.push({start, 0, ManhattanDistance(start, goal)});

    constexpr std::array<std::array<std::ptrdiff_t, 2>, 4> kNeighbors{{
        {{-1, 0}},
        {{0, -1}},
        {{0, 1}},
        {{1, 0}},
    }};

    while (!open.empty()) {
        const OpenNode current = open.top();
        open.pop();
        const std::size_t currentIndex = GridIndex(current.coordinate, width);
        if (current.pathCost != costs[currentIndex]) {
            continue;
        }
        if (current.coordinate == goal) {
            std::vector<GridCoordinate> result;
            std::size_t index = currentIndex;
            const std::size_t startIndex = GridIndex(start, width);
            while (true) {
                result.push_back({index / width, index % width});
                if (index == startIndex) {
                    break;
                }
                index = parents[index];
                if (index == unreachable) {
                    return {};
                }
            }
            std::reverse(result.begin(), result.end());
            return result;
        }

        for (const auto& offset : kNeighbors) {
            const std::ptrdiff_t nextRow =
                static_cast<std::ptrdiff_t>(current.coordinate.row) + offset[0];
            const std::ptrdiff_t nextColumn =
                static_cast<std::ptrdiff_t>(current.coordinate.column) + offset[1];
            if (nextRow < 0 || nextColumn < 0) {
                continue;
            }
            const GridCoordinate next{
                static_cast<std::size_t>(nextRow),
                static_cast<std::size_t>(nextColumn),
            };
            if (!IsNavigationCell(map, next, radius, cellSize, forbiddenCell)) {
                continue;
            }

            const std::size_t nextIndex = GridIndex(next, width);
            const std::size_t nextCost = current.pathCost + 1;
            if (nextCost >= costs[nextIndex]) {
                continue;
            }
            costs[nextIndex] = nextCost;
            parents[nextIndex] = currentIndex;
            open.push({next, nextCost, ManhattanDistance(next, goal)});
        }
    }

    return {};
}

[[nodiscard]] bool IsPathValid(
    const GridMap& map,
    const std::vector<GridCoordinate>& path,
    const std::size_t nextWaypointIndex,
    const float radius,
    const float cellSize,
    const std::optional<GridCoordinate> forbiddenCell = std::nullopt) {
    if (path.empty() || nextWaypointIndex > path.size()) {
        return false;
    }
    for (std::size_t index = nextWaypointIndex; index < path.size(); ++index) {
        if (!IsNavigationCell(
                map, path[index], radius, cellSize, forbiddenCell)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Float2 MoveToward(
    const GridMap& map,
    const Float2 position,
    const Float2 target,
    const float maximumDistance,
    const float radius,
    const CircleObstacle& playerObstacle,
    const float cellSize) {
    const float targetDistance = Distance(position, target);
    if (targetDistance <= kPositionEpsilon || maximumDistance <= 0.0f) {
        return position;
    }
    if (GridCollision::OverlapsCircle(position, radius, playerObstacle)) {
        return position;
    }

    const float distanceToMove = (std::min)(maximumDistance, targetDistance);
    const float scale = distanceToMove / targetDistance;
    const Float2 displacement{
        (target.x - position.x) * scale,
        (target.z - position.z) * scale,
    };
    const std::span<const CircleObstacle> obstacles{&playerObstacle, 1};
    return GridCollision::MoveCircle(
        map, position, displacement, radius, obstacles, cellSize);
}

[[nodiscard]] bool ValidatePositive(
    const float value,
    const char* const label,
    std::string& error) {
    if (std::isfinite(value) && value > 0.0f) {
        return true;
    }
    error = "EnemySettings ";
    error += label;
    error += " must be finite and greater than zero.";
    return false;
}

[[nodiscard]] bool ValidateNonNegative(
    const float value,
    const char* const label,
    std::string& error) {
    if (std::isfinite(value) && value >= 0.0f) {
        return true;
    }
    error = "EnemySettings ";
    error += label;
    error += " must be finite and non-negative.";
    return false;
}

} // namespace

bool ValidateEnemySettings(
    const EnemySettings& settings,
    std::string& error) {
    error.clear();
    if (!ValidatePositive(settings.collisionRadius, "collision radius", error) ||
        !ValidatePositive(settings.health, "health", error) ||
        !ValidatePositive(
            settings.repathIntervalSeconds, "repath interval", error) ||
        !ValidatePositive(settings.meleeSpeed, "melee speed", error) ||
        !ValidateNonNegative(
            settings.meleeAttackSurfaceDistance,
            "melee attack surface distance",
            error) ||
        !ValidatePositive(
            settings.meleeAttackStateSeconds,
            "melee attack state duration",
            error) ||
        !ValidatePositive(
            settings.meleeAttackIntervalSeconds,
            "melee attack interval",
            error) ||
        !ValidatePositive(settings.rangedSpeed, "ranged speed", error) ||
        !ValidateNonNegative(
            settings.rangedTooCloseSurfaceDistance,
            "ranged too-close surface distance",
            error) ||
        !ValidatePositive(
            settings.rangedIdealSurfaceDistance,
            "ranged ideal surface distance",
            error) ||
        !ValidatePositive(
            settings.rangedMaximumAttackSurfaceDistance,
            "ranged maximum attack surface distance",
            error) ||
        !ValidatePositive(
            settings.rangedAttackStateSeconds,
            "ranged attack state duration",
            error) ||
        !ValidatePositive(
            settings.rangedAttackIntervalSeconds,
            "ranged attack interval",
            error)) {
        return false;
    }

    if (settings.rangedTooCloseSurfaceDistance >=
        settings.rangedIdealSurfaceDistance) {
        error = "EnemySettings ranged too-close distance must be less than the ideal distance.";
        return false;
    }
    if (settings.rangedIdealSurfaceDistance >
        settings.rangedMaximumAttackSurfaceDistance) {
        error = "EnemySettings ranged ideal distance must not exceed the maximum attack distance.";
        return false;
    }
    if (settings.meleeAttackIntervalSeconds <
        settings.meleeAttackStateSeconds) {
        error = "EnemySettings melee attack interval must not be shorter than the attack state duration.";
        return false;
    }
    if (settings.rangedAttackIntervalSeconds <
        settings.rangedAttackStateSeconds) {
        error = "EnemySettings ranged attack interval must not be shorter than the attack state duration.";
        return false;
    }
    return true;
}

bool EnemySystem::Initialize(
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    const float cellSize,
    EnemySettings settings,
    std::string& error) {
    if (!InitializeState(
            map,
            playerPosition,
            playerCollisionRadius,
            cellSize,
            std::move(settings),
            error)) {
        return false;
    }

    try {
        std::vector<RuntimeEnemy> candidates;
        candidates.reserve(map.GetEnemySpawnPoints().size());
        EnemyId candidateId = 1;
        const CircleObstacle playerObstacle{
            playerPosition,
            playerCollisionRadius,
        };

        for (const EnemySpawnPoint& spawn : map.GetEnemySpawnPoints()) {
            if (spawn.kind != EnemyKind::Melee && spawn.kind != EnemyKind::Ranged) {
                error = "Unknown enemy spawn type at row " +
                        std::to_string(spawn.cell.row + 1) + ", column " +
                        std::to_string(spawn.cell.column + 1) + ".";
                Reset();
                return false;
            }
            if (spawn.cell.row >= map.GetHeight() ||
                spawn.cell.column >= map.GetWidth()) {
                error = std::string{KindName(spawn.kind)} +
                        " enemy spawn at row " +
                        std::to_string(spawn.cell.row + 1) + ", column " +
                        std::to_string(spawn.cell.column + 1) +
                        " is outside the map.";
                Reset();
                return false;
            }

            EnemyDefinition definition = MakeLegacyDefinition(spawn.kind, settings_);
            const float collisionRadius = definition.hitboxRadius;
            const Float2 spawnPosition = map.GetCellCenter(spawn.cell, cellSize);
            if (GridCollision::OverlapsSolid(
                    map,
                    spawnPosition,
                    collisionRadius,
                    cellSize)) {
                error = std::string{KindName(spawn.kind)} +
                        " enemy spawn at row " +
                        std::to_string(spawn.cell.row + 1) + ", column " +
                        std::to_string(spawn.cell.column + 1) +
                        " overlaps a solid map cell.";
                Reset();
                return false;
            }
            if (GridCollision::OverlapsCircle(
                    spawnPosition, collisionRadius, playerObstacle)) {
                error = std::string{KindName(spawn.kind)} +
                        " enemy spawn at row " +
                        std::to_string(spawn.cell.row + 1) + ", column " +
                        std::to_string(spawn.cell.column + 1) +
                        " overlaps the player spawn circle.";
                Reset();
                return false;
            }

            RuntimeEnemy enemy{};
            enemy.id = candidateId;
            enemy.kind = spawn.kind;
            enemy.position = spawnPosition;
            enemy.definition = std::move(definition);
            enemy.health = enemy.definition.maxHealth;
            enemy.repathElapsedSeconds = settings_.repathIntervalSeconds;
            candidates.push_back(std::move(enemy));

            if (candidateId == (std::numeric_limits<EnemyId>::max)() &&
                candidates.size() != map.GetEnemySpawnPoints().size()) {
                error = "EnemySystem has more spawn points than stable IDs.";
                Reset();
                return false;
            }
            ++candidateId;
        }

        nextEnemyId_ = candidateId;
        enemies_ = std::move(candidates);
        snapshots_.reserve(enemies_.size());
        attackEvents_.reserve(enemies_.size());
        initialized_ = true;
        RefreshSnapshots();
        return true;
    } catch (const std::exception& exception) {
        Reset();
        error = "Failed to initialize enemies: ";
        error += exception.what();
    } catch (...) {
        Reset();
        error = "Failed to initialize enemies because of an unknown error.";
    }
    return false;
}

bool EnemySystem::InitializeEmpty(
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    const float cellSize,
    EnemySettings settings,
    std::string& error) {
    return InitializeState(
        map,
        playerPosition,
        playerCollisionRadius,
        cellSize,
        std::move(settings),
        error);
}

bool EnemySystem::InitializeState(
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    const float cellSize,
    EnemySettings settings,
    std::string& error) {
    Reset();
    error.clear();

    if (!ValidateEnemySettings(settings, error)) {
        return false;
    }
    if (!IsFinite(playerPosition)) {
        error = "EnemySystem player position must be finite.";
        return false;
    }
    if (!std::isfinite(playerCollisionRadius) || playerCollisionRadius <= 0.0f) {
        error = "EnemySystem player collision radius must be finite and greater than zero.";
        return false;
    }
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) {
        error = "EnemySystem cell size must be finite and greater than zero.";
        return false;
    }
    if (map.GetWidth() == 0 || map.GetHeight() == 0 ||
        map.GetWidth() >
            (std::numeric_limits<std::size_t>::max)() / map.GetHeight()) {
        error = "EnemySystem requires a non-empty map with a supported cell count.";
        return false;
    }

    try {
        if (GridCollision::OverlapsSolid(
                map, playerPosition, playerCollisionRadius, cellSize)) {
            error = "EnemySystem player collider overlaps a solid map cell.";
            return false;
        }
    } catch (const std::exception& exception) {
        error = "Failed to initialize enemies: ";
        error += exception.what();
        return false;
    }

    settings_ = std::move(settings);
    cellSize_ = cellSize;
    mapWidth_ = map.GetWidth();
    mapHeight_ = map.GetHeight();
    nextEnemyId_ = 1;
    initialized_ = true;
    return true;
}

bool EnemySystem::ValidateDefinition(
    const EnemyDefinition& definition,
    std::string& error) const {
    error.clear();
    if (definition.id.empty()) {
        error = "Enemy definition ID must be non-empty.";
        return false;
    }
    if (definition.kind != EnemyKind::Melee &&
        definition.kind != EnemyKind::Ranged) {
        error = "Enemy definition kind is unsupported.";
        return false;
    }
    if (!std::isfinite(definition.damage) || definition.damage <= 0.0f ||
        !std::isfinite(definition.attackIntervalSeconds) ||
        definition.attackIntervalSeconds <= 0.0f ||
        !std::isfinite(definition.maxHealth) || definition.maxHealth <= 0.0f ||
        !std::isfinite(definition.defense) || definition.defense < 0.0f ||
        !std::isfinite(definition.hitboxRadius) ||
        definition.hitboxRadius <= 0.0f ||
        !std::isfinite(definition.hitboxHeight) ||
        definition.hitboxHeight < definition.hitboxRadius * 2.0f ||
        !std::isfinite(definition.renderWidth) ||
        definition.renderWidth <= 0.0f ||
        !std::isfinite(definition.renderHeight) ||
        definition.renderHeight <= 0.0f ||
        definition.frameWidthPixels == 0 ||
        definition.frameHeightPixels == 0 ||
        definition.texturePath.empty()) {
        error = "Enemy definition combat, hitbox, render, frame, and texture values must be valid.";
        return false;
    }

    const auto validateClip = [&definition, &error](
                                  const EnemyAnimationClipDefinition& clip,
                                  const char* const state,
                                  const bool isAttack) {
        if (clip.frameCount == 0 ||
            !std::isfinite(clip.secondsPerFrame) ||
            clip.secondsPerFrame <= 0.0f) {
            error = "Enemy definition ";
            error += state;
            error += " animation values must be valid.";
            return false;
        }

        const std::uint64_t right =
            static_cast<std::uint64_t>(clip.originXpx) +
            static_cast<std::uint64_t>(definition.frameWidthPixels) *
                clip.frameCount;
        const std::uint64_t bottom =
            static_cast<std::uint64_t>(clip.originYpx) +
            definition.frameHeightPixels;
        if (right > (std::numeric_limits<std::uint32_t>::max)() ||
            bottom > (std::numeric_limits<std::uint32_t>::max)()) {
            error = "Enemy definition animation rectangle exceeds supported pixel coordinates.";
            return false;
        }

        if (!isAttack) {
            if (clip.eventFrameIndex.has_value() || clip.muzzlePixel.has_value()) {
                error = "Only an enemy attack animation may define an event frame or muzzle.";
                return false;
            }
            return true;
        }
        if (!clip.eventFrameIndex.has_value() ||
            *clip.eventFrameIndex >= clip.frameCount) {
            error = "Enemy definition attack event frame must be inside the attack animation.";
            return false;
        }
        if (definition.kind == EnemyKind::Melee) {
            if (clip.muzzlePixel.has_value()) {
                error = "A melee enemy attack animation must not define a muzzle.";
                return false;
            }
            return true;
        }
        if (!clip.muzzlePixel.has_value() ||
            clip.muzzlePixel->x >= definition.frameWidthPixels ||
            clip.muzzlePixel->y >= definition.frameHeightPixels) {
            error = "A ranged enemy attack animation requires an in-frame muzzle coordinate.";
            return false;
        }
        return true;
    };
    if (!validateClip(definition.animations.idle, "idle", false) ||
        !validateClip(definition.animations.moving, "moving", false) ||
        !validateClip(definition.animations.attacking, "attacking", true) ||
        !validateClip(definition.animations.dead, "dead", false)) {
        return false;
    }

    const float attackStateSeconds =
        ClipDurationSeconds(definition.animations.attacking);
    if (!std::isfinite(attackStateSeconds) || attackStateSeconds <= 0.0f) {
        error = "Enemy definition attack animation duration is unsupported.";
        return false;
    }
    if (definition.attackIntervalSeconds < attackStateSeconds) {
        error = "Enemy definition attack interval must not be shorter than its attack state duration.";
        return false;
    }
    return true;
}

void EnemySystem::Reset() noexcept {
    settings_ = {};
    cellSize_ = 1.0f;
    mapWidth_ = 0;
    mapHeight_ = 0;
    nextEnemyId_ = 1;
    initialized_ = false;
    enemies_.clear();
    snapshots_.clear();
    attackEvents_.clear();
}

EnemySpawnResult EnemySystem::Spawn(
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    const Float2 spawnPosition,
    const EnemyDefinition& definition,
    std::string& error) {
    error.clear();
    if (!initialized_) {
        error = "EnemySystem must be initialized before spawning enemies.";
        return {};
    }
    if (map.GetWidth() != mapWidth_ || map.GetHeight() != mapHeight_) {
        error = "Enemy spawn map dimensions differ from the initialized map.";
        return {};
    }
    if (!IsFinite(playerPosition) ||
        !std::isfinite(playerCollisionRadius) || playerCollisionRadius <= 0.0f ||
        !IsFinite(spawnPosition)) {
        error = "Enemy spawn positions and player radius must be finite and valid.";
        return {};
    }
    if (!ValidateDefinition(definition, error)) {
        return {};
    }
    if (nextEnemyId_ == (std::numeric_limits<EnemyId>::max)()) {
        error = "EnemySystem exhausted its stable runtime IDs.";
        return {};
    }

    try {
        if (GridCollision::OverlapsSolid(
                map,
                spawnPosition,
                definition.hitboxRadius,
                cellSize_) ||
            GridCollision::OverlapsCircle(
                spawnPosition,
                definition.hitboxRadius,
                {playerPosition, playerCollisionRadius})) {
            return {EnemySpawnStatus::Blocked, 0};
        }
        for (const CircleObstacle& obstacle : CollectOccupiedColliders()) {
            if (GridCollision::OverlapsCircle(
                    spawnPosition,
                    definition.hitboxRadius,
                    obstacle)) {
                return {EnemySpawnStatus::Blocked, 0};
            }
        }

        RuntimeEnemy enemy{};
        enemy.id = nextEnemyId_++;
        enemy.kind = definition.kind;
        enemy.position = spawnPosition;
        enemy.definition = definition;
        enemy.health = definition.maxHealth;
        enemy.repathElapsedSeconds = settings_.repathIntervalSeconds;
        const EnemyId spawnedId = enemy.id;
        enemies_.push_back(std::move(enemy));
        RefreshSnapshots();
        return {EnemySpawnStatus::Spawned, spawnedId};
    } catch (const std::exception& exception) {
        error = "Failed to spawn enemy: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to spawn enemy because of an unknown error.";
    }
    return {};
}

bool EnemySystem::Retire(const EnemyId id) noexcept {
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        if (enemies_[index].id != id) {
            continue;
        }
        enemies_.erase(enemies_.begin() + static_cast<std::ptrdiff_t>(index));
        snapshots_.erase(snapshots_.begin() + static_cast<std::ptrdiff_t>(index));
        std::erase_if(
            attackEvents_,
            [id](const EnemyAttackEvent& event) { return event.enemyId == id; });
        return true;
    }
    return false;
}

bool EnemySystem::RetireDead(const EnemyId id) noexcept {
    for (const RuntimeEnemy& enemy : enemies_) {
        if (enemy.id == id) {
            return enemy.state == EnemyState::Dead &&
                   enemy.stateElapsedSeconds >=
                       DeadVisibilitySeconds(enemy.definition) &&
                   Retire(id);
        }
    }
    return false;
}

std::size_t EnemySystem::RetireExpiredDead() noexcept {
    std::size_t retiredCount = 0;
    for (std::size_t index = enemies_.size(); index > 0; --index) {
        const RuntimeEnemy& enemy = enemies_[index - 1];
        if (enemy.state == EnemyState::Dead &&
            enemy.stateElapsedSeconds >= DeadVisibilitySeconds(enemy.definition)) {
            const EnemyId id = enemy.id;
            static_cast<void>(Retire(id));
            ++retiredCount;
        }
    }
    return retiredCount;
}

void EnemySystem::Update(
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    const float deltaSeconds) {
    Update(
        map,
        {playerPosition, playerCollisionRadius, kLegacyPlayerHitboxHeight},
        deltaSeconds);
}

void EnemySystem::Update(
    const GridMap& map,
    const EnemyTarget& player,
    const float deltaSeconds) {
    attackEvents_.clear();
    if (!initialized_ || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return;
    }
    const Float2 playerPosition = player.position;
    const float playerCollisionRadius = player.collisionRadius;
    if (!IsFinite(playerPosition)) {
        throw std::invalid_argument("enemy update player position must be finite");
    }
    if (!std::isfinite(playerCollisionRadius) || playerCollisionRadius <= 0.0f) {
        throw std::invalid_argument(
            "enemy update player collision radius must be finite and greater than zero");
    }
    if (!std::isfinite(player.hitboxHeight) || player.hitboxHeight <= 0.0f) {
        throw std::invalid_argument(
            "enemy update player hitbox height must be finite and greater than zero");
    }
    if (map.GetWidth() != mapWidth_ || map.GetHeight() != mapHeight_) {
        throw std::invalid_argument(
            "enemy update map dimensions differ from the initialized map");
    }

    const std::optional<GridCoordinate> playerCell =
        map.TryGetCoordinateAtPosition(playerPosition, cellSize_);
    if (!playerCell.has_value()) {
        throw std::invalid_argument("enemy update player position is outside the map");
    }

    const CircleObstacle playerObstacle{
        playerPosition,
        playerCollisionRadius,
    };

    const auto setState = [](RuntimeEnemy& enemy, const EnemyState state) noexcept {
        if (enemy.state != state) {
            enemy.state = state;
            enemy.stateElapsedSeconds = 0.0f;
        }
    };
    const auto clearNavigation = [](RuntimeEnemy& enemy) noexcept {
        enemy.navigationPurpose = NavigationPurpose::None;
        enemy.path.clear();
        enemy.nextWaypointIndex = 0;
        enemy.lastPlayerCell.reset();
        enemy.repathElapsedSeconds = 0.0f;
        enemy.stuckElapsedSeconds = 0.0f;
    };
    const auto assignPath = [playerCell](
                                RuntimeEnemy& enemy,
                                const NavigationPurpose purpose,
                                std::vector<GridCoordinate> path) {
        enemy.navigationPurpose = purpose;
        enemy.path = std::move(path);
        enemy.nextWaypointIndex = enemy.path.size() > 1 ? 1 : 0;
        enemy.lastPlayerCell = playerCell;
        enemy.repathElapsedSeconds = 0.0f;
        enemy.stuckElapsedSeconds = 0.0f;
    };
    const auto emitAttackEvent =
        [this,
         &map,
         playerPosition,
         playerCollisionRadius,
         &player](RuntimeEnemy& enemy) {
            if (enemy.attackEventEmitted) {
                return;
            }
            enemy.attackEventEmitted = true;

            Float3 origin{
                enemy.position.x,
                enemy.definition.hitboxHeight * 0.5f,
                enemy.position.z,
            };
            if (enemy.kind == EnemyKind::Melee) {
                const float eventSurfaceDistance = SurfaceDistance(
                    enemy.position,
                    enemy.definition.hitboxRadius,
                    playerPosition,
                    playerCollisionRadius);
                if (eventSurfaceDistance > settings_.meleeAttackSurfaceDistance ||
                    !HasWallLineOfSight(
                        map, enemy.position, playerPosition, cellSize_)) {
                    return;
                }
            } else {
                const EnemyAnimationPixelPoint muzzle =
                    *enemy.definition.animations.attacking.muzzlePixel;
                const float deltaX = playerPosition.x - enemy.position.x;
                const float deltaZ = playerPosition.z - enemy.position.z;
                const float distance = std::hypot(deltaX, deltaZ);
                const Float2 forward =
                    distance > kPositionEpsilon
                        ? Float2{deltaX / distance, deltaZ / distance}
                        : Float2{0.0f, 1.0f};
                const Float2 right{forward.z, -forward.x};
                const float horizontalOffset =
                    (static_cast<float>(muzzle.x) /
                         static_cast<float>(enemy.definition.frameWidthPixels) -
                     0.5f) *
                    enemy.definition.renderWidth;
                origin.x += right.x * horizontalOffset;
                origin.z += right.z * horizontalOffset;
                origin.y =
                    (1.0f -
                     static_cast<float>(muzzle.y) /
                         static_cast<float>(enemy.definition.frameHeightPixels)) *
                    enemy.definition.renderHeight;
            }

            attackEvents_.push_back({
                enemy.id,
                enemy.definition.id,
                enemy.kind,
                origin,
                {
                    playerPosition.x,
                    player.hitboxHeight * 0.5f,
                    playerPosition.z,
                },
                enemy.definition.damage,
            });
        };
    const auto enterAttack =
        [this, &setState, &emitAttackEvent](RuntimeEnemy& enemy) {
        setState(enemy, EnemyState::Attacking);
        enemy.attackEventEmitted = false;
        enemy.attackCooldownSeconds = enemy.definition.attackIntervalSeconds;
        enemy.path.clear();
        enemy.nextWaypointIndex = 0;
        enemy.navigationPurpose = NavigationPurpose::None;
        enemy.lastPlayerCell.reset();
        enemy.repathElapsedSeconds = 0.0f;
        enemy.stuckElapsedSeconds = 0.0f;
        if (*enemy.definition.animations.attacking.eventFrameIndex == 0) {
            emitAttackEvent(enemy);
        }
    };

    for (RuntimeEnemy& enemy : enemies_) {
        SubtractElapsed(enemy.hitFlashRemainingSeconds, deltaSeconds);
        if (enemy.state == EnemyState::Dead) {
            AddElapsed(enemy.stateElapsedSeconds, deltaSeconds);
            continue;
        }

        enemy.attackCooldownSeconds =
            (std::max)(0.0f, enemy.attackCooldownSeconds - deltaSeconds);
        AddElapsed(enemy.repathElapsedSeconds, deltaSeconds);

        if (enemy.state == EnemyState::Attacking) {
            const float previousElapsedSeconds = enemy.stateElapsedSeconds;
            AddElapsed(enemy.stateElapsedSeconds, deltaSeconds);
            const float eventSeconds =
                static_cast<float>(
                    *enemy.definition.animations.attacking.eventFrameIndex) *
                enemy.definition.animations.attacking.secondsPerFrame;
            if (!enemy.attackEventEmitted &&
                previousElapsedSeconds <= eventSeconds &&
                enemy.stateElapsedSeconds >= eventSeconds) {
                emitAttackEvent(enemy);
            }
            const float attackStateSeconds = ClipDurationSeconds(
                enemy.definition.animations.attacking);
            if (enemy.stateElapsedSeconds < attackStateSeconds) {
                continue;
            }
            setState(enemy, EnemyState::Idle);
        } else {
            AddElapsed(enemy.stateElapsedSeconds, deltaSeconds);
        }

        const float surfaceDistance = SurfaceDistance(
            enemy.position,
            enemy.definition.hitboxRadius,
            playerPosition,
            playerCollisionRadius);
        const bool hasLineOfSight = HasWallLineOfSight(
            map, enemy.position, playerPosition, cellSize_);
        const std::optional<GridCoordinate> enemyCell =
            map.TryGetCoordinateAtPosition(enemy.position, cellSize_);

        bool movementRequested = false;
        const Float2 previousPosition = enemy.position;

        if (enemy.kind == EnemyKind::Melee) {
            if (surfaceDistance <= settings_.meleeAttackSurfaceDistance &&
                hasLineOfSight) {
                clearNavigation(enemy);
                if (enemy.attackCooldownSeconds <= 0.0f) {
                    enterAttack(enemy);
                } else {
                    setState(enemy, EnemyState::Idle);
                }
                continue;
            }

            if (hasLineOfSight) {
                clearNavigation(enemy);
                movementRequested = true;
                enemy.position = MoveToward(
                    map,
                    enemy.position,
                    playerPosition,
                    settings_.meleeSpeed * deltaSeconds,
                    enemy.definition.hitboxRadius,
                    playerObstacle,
                    cellSize_);
            } else if (enemyCell.has_value()) {
                const bool needsPath =
                    enemy.navigationPurpose != NavigationPurpose::Chase ||
                    enemy.path.empty() ||
                    enemy.nextWaypointIndex >= enemy.path.size() ||
                    enemy.repathElapsedSeconds >= settings_.repathIntervalSeconds ||
                    enemy.stuckElapsedSeconds >= settings_.repathIntervalSeconds ||
                    !enemy.lastPlayerCell.has_value() ||
                    *enemy.lastPlayerCell != *playerCell ||
                    !IsPathValid(
                        map,
                        enemy.path,
                        enemy.nextWaypointIndex,
                        enemy.definition.hitboxRadius,
                        cellSize_);
                if (needsPath) {
                    assignPath(
                        enemy,
                        NavigationPurpose::Chase,
                        FindPath(
                            map,
                            *enemyCell,
                            *playerCell,
                            enemy.definition.hitboxRadius,
                            cellSize_));
                }
            } else {
                clearNavigation(enemy);
            }
        } else if (surfaceDistance <
                   settings_.rangedTooCloseSurfaceDistance) {
            if (enemyCell.has_value()) {
                const bool needsPath =
                    enemy.navigationPurpose != NavigationPurpose::Retreat ||
                    enemy.path.empty() ||
                    enemy.nextWaypointIndex >= enemy.path.size() ||
                    enemy.repathElapsedSeconds >= settings_.repathIntervalSeconds ||
                    enemy.stuckElapsedSeconds >= settings_.repathIntervalSeconds ||
                    !enemy.lastPlayerCell.has_value() ||
                    *enemy.lastPlayerCell != *playerCell ||
                    !IsPathValid(
                        map,
                        enemy.path,
                        enemy.nextWaypointIndex,
                        enemy.definition.hitboxRadius,
                        cellSize_,
                        playerCell);
                if (needsPath) {
                    std::vector<GridCoordinate> bestPath;
                    float bestIdealDifference =
                        (std::numeric_limits<float>::max)();
                    std::size_t bestPathCost =
                        (std::numeric_limits<std::size_t>::max)();
                    GridCoordinate bestCell{
                        (std::numeric_limits<std::size_t>::max)(),
                        (std::numeric_limits<std::size_t>::max)(),
                    };
                    const float currentIdealDifference = std::fabs(
                        surfaceDistance - settings_.rangedIdealSurfaceDistance);

                    for (std::size_t row = 0; row < map.GetHeight(); ++row) {
                        for (std::size_t column = 0;
                             column < map.GetWidth();
                             ++column) {
                            const GridCoordinate candidate{row, column};
                            if (!IsNavigationCell(
                                    map,
                                    candidate,
                                    enemy.definition.hitboxRadius,
                                    cellSize_,
                                    playerCell)) {
                                continue;
                            }
                            const Float2 candidatePosition =
                                map.GetCellCenter(candidate, cellSize_);
                            const float candidateSurfaceDistance = SurfaceDistance(
                                candidatePosition,
                                enemy.definition.hitboxRadius,
                                playerPosition,
                                playerCollisionRadius);
                            const float idealDifference = std::fabs(
                                candidateSurfaceDistance -
                                settings_.rangedIdealSurfaceDistance);
                            if (candidateSurfaceDistance <=
                                    surfaceDistance + kPositionEpsilon ||
                                idealDifference >=
                                    currentIdealDifference - kPositionEpsilon) {
                                continue;
                            }

                            std::vector<GridCoordinate> candidatePath = FindPath(
                                map,
                                *enemyCell,
                                candidate,
                                enemy.definition.hitboxRadius,
                                cellSize_,
                                playerCell);
                            if (candidatePath.empty()) {
                                continue;
                            }
                            const std::size_t pathCost = candidatePath.size() - 1;
                            const bool better =
                                idealDifference <
                                    bestIdealDifference - kPositionEpsilon ||
                                (std::fabs(
                                     idealDifference - bestIdealDifference) <=
                                     kPositionEpsilon &&
                                 (pathCost < bestPathCost ||
                                  (pathCost == bestPathCost &&
                                   (candidate.row < bestCell.row ||
                                    (candidate.row == bestCell.row &&
                                     candidate.column < bestCell.column)))));
                            if (better) {
                                bestIdealDifference = idealDifference;
                                bestPathCost = pathCost;
                                bestCell = candidate;
                                bestPath = std::move(candidatePath);
                            }
                        }
                    }
                    assignPath(
                        enemy,
                        NavigationPurpose::Retreat,
                        std::move(bestPath));
                }
            } else {
                clearNavigation(enemy);
            }
        } else if (surfaceDistance <=
                       settings_.rangedMaximumAttackSurfaceDistance &&
                   hasLineOfSight) {
            clearNavigation(enemy);
            if (enemy.attackCooldownSeconds <= 0.0f) {
                enterAttack(enemy);
            } else {
                setState(enemy, EnemyState::Idle);
            }
            continue;
        } else if (enemyCell.has_value()) {
            const bool needsPath =
                enemy.navigationPurpose != NavigationPurpose::FiringPosition ||
                enemy.path.empty() ||
                enemy.nextWaypointIndex >= enemy.path.size() ||
                enemy.repathElapsedSeconds >= settings_.repathIntervalSeconds ||
                enemy.stuckElapsedSeconds >= settings_.repathIntervalSeconds ||
                !enemy.lastPlayerCell.has_value() ||
                *enemy.lastPlayerCell != *playerCell ||
                !IsPathValid(
                    map,
                    enemy.path,
                    enemy.nextWaypointIndex,
                    enemy.definition.hitboxRadius,
                    cellSize_,
                    playerCell);
            if (needsPath) {
                std::vector<GridCoordinate> bestPath;
                std::size_t bestPathCost =
                    (std::numeric_limits<std::size_t>::max)();
                float bestIdealDifference =
                    (std::numeric_limits<float>::max)();
                GridCoordinate bestCell{
                    (std::numeric_limits<std::size_t>::max)(),
                    (std::numeric_limits<std::size_t>::max)(),
                };

                for (std::size_t row = 0; row < map.GetHeight(); ++row) {
                    for (std::size_t column = 0;
                         column < map.GetWidth();
                         ++column) {
                        const GridCoordinate candidate{row, column};
                        if (!IsNavigationCell(
                                map,
                                candidate,
                                enemy.definition.hitboxRadius,
                                cellSize_,
                                playerCell)) {
                            continue;
                        }
                        const Float2 candidatePosition =
                            map.GetCellCenter(candidate, cellSize_);
                        const float candidateSurfaceDistance = SurfaceDistance(
                            candidatePosition,
                            enemy.definition.hitboxRadius,
                            playerPosition,
                            playerCollisionRadius);
                        if (candidateSurfaceDistance <
                                settings_.rangedTooCloseSurfaceDistance ||
                            candidateSurfaceDistance >
                                settings_.rangedMaximumAttackSurfaceDistance ||
                            !HasWallLineOfSight(
                                map,
                                candidatePosition,
                                playerPosition,
                                cellSize_)) {
                            continue;
                        }

                        std::vector<GridCoordinate> candidatePath = FindPath(
                            map,
                            *enemyCell,
                            candidate,
                            enemy.definition.hitboxRadius,
                            cellSize_,
                            playerCell);
                        if (candidatePath.empty()) {
                            continue;
                        }
                        const std::size_t pathCost = candidatePath.size() - 1;
                        const float idealDifference = std::fabs(
                            candidateSurfaceDistance -
                            settings_.rangedIdealSurfaceDistance);
                        const bool better =
                            pathCost < bestPathCost ||
                            (pathCost == bestPathCost &&
                             (idealDifference <
                                  bestIdealDifference - kPositionEpsilon ||
                              (std::fabs(
                                   idealDifference - bestIdealDifference) <=
                                   kPositionEpsilon &&
                               (candidate.row < bestCell.row ||
                                (candidate.row == bestCell.row &&
                                 candidate.column < bestCell.column)))));
                        if (better) {
                            bestPathCost = pathCost;
                            bestIdealDifference = idealDifference;
                            bestCell = candidate;
                            bestPath = std::move(candidatePath);
                        }
                    }
                }
                assignPath(
                    enemy,
                    NavigationPurpose::FiringPosition,
                    std::move(bestPath));
            }
        } else {
            clearNavigation(enemy);
        }

        if (!movementRequested && !enemy.path.empty() &&
            enemy.nextWaypointIndex < enemy.path.size()) {
            float movementBudget =
                (enemy.kind == EnemyKind::Melee
                     ? settings_.meleeSpeed
                     : settings_.rangedSpeed) *
                deltaSeconds;
            std::size_t remainingIterations =
                enemy.path.size() - enemy.nextWaypointIndex + 1;
            while (movementBudget > kPositionEpsilon &&
                   enemy.nextWaypointIndex < enemy.path.size() &&
                   remainingIterations > 0) {
                --remainingIterations;
                const Float2 waypoint = map.GetCellCenter(
                    enemy.path[enemy.nextWaypointIndex], cellSize_);
                const float waypointDistance = Distance(enemy.position, waypoint);
                if (waypointDistance <= kWaypointTolerance) {
                    ++enemy.nextWaypointIndex;
                    continue;
                }

                movementRequested = true;
                const Float2 beforeStep = enemy.position;
                enemy.position = MoveToward(
                    map,
                    enemy.position,
                    waypoint,
                    movementBudget,
                    enemy.definition.hitboxRadius,
                    playerObstacle,
                    cellSize_);
                const float movedDistance = Distance(beforeStep, enemy.position);
                if (movedDistance <= kPositionEpsilon) {
                    break;
                }
                movementBudget =
                    (std::max)(0.0f, movementBudget - movedDistance);
                if (Distance(enemy.position, waypoint) <= kWaypointTolerance) {
                    ++enemy.nextWaypointIndex;
                }
                if (movedDistance + kPositionEpsilon <
                    (std::min)(movementBudget + movedDistance, waypointDistance)) {
                    break;
                }
            }
        }

        const float movedDistance = Distance(previousPosition, enemy.position);
        if (movementRequested && movedDistance <= kPositionEpsilon) {
            AddElapsed(enemy.stuckElapsedSeconds, deltaSeconds);
        } else {
            enemy.stuckElapsedSeconds = 0.0f;
        }
        setState(
            enemy,
            movedDistance > kPositionEpsilon
                ? EnemyState::Moving
                : EnemyState::Idle);
    }

    RefreshSnapshots();
}

std::vector<CircleObstacle> EnemySystem::CollectAliveColliders() const {
    std::vector<CircleObstacle> colliders;
    colliders.reserve(enemies_.size());
    for (const RuntimeEnemy& enemy : enemies_) {
        if (enemy.state != EnemyState::Dead) {
            colliders.push_back({
                enemy.position,
                enemy.definition.hitboxRadius,
            });
        }
    }
    return colliders;
}

std::vector<CircleObstacle> EnemySystem::CollectOccupiedColliders() const {
    std::vector<CircleObstacle> colliders;
    colliders.reserve(enemies_.size());
    for (const RuntimeEnemy& enemy : enemies_) {
        if (enemy.state != EnemyState::Dead ||
            enemy.stateElapsedSeconds < DeadVisibilitySeconds(enemy.definition)) {
            colliders.push_back({
                enemy.position,
                enemy.definition.hitboxRadius,
            });
        }
    }
    return colliders;
}

std::size_t EnemySystem::GetAliveCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        enemies_.begin(),
        enemies_.end(),
        [](const RuntimeEnemy& enemy) {
            return enemy.state != EnemyState::Dead;
        }));
}

EnemyDamageResult EnemySystem::ApplyDamage(
    const EnemyId id,
    const float rawDamage) noexcept {
    EnemyDamageResult result{};
    result.rawDamage = rawDamage;
    if (!std::isfinite(rawDamage) || rawDamage <= 0.0f) {
        return result;
    }

    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        RuntimeEnemy& enemy = enemies_[index];
        if (enemy.id != id || enemy.state == EnemyState::Dead) {
            continue;
        }

        const float resolvedDamage =
            (std::max)(1.0f, rawDamage - enemy.definition.defense);
        const float healthBefore = enemy.health;
        enemy.health = (std::max)(0.0f, enemy.health - resolvedDamage);
        enemy.hitFlashRemainingSeconds = kEnemyHitFlashSeconds;
        if (enemy.health <= 0.0f) {
            MarkDead(enemy);
        }

        snapshots_[index].state = enemy.state;
        snapshots_[index].health = enemy.health;
        snapshots_[index].hitFlashRemainingSeconds =
            enemy.hitFlashRemainingSeconds;
        snapshots_[index].stateElapsedSeconds = enemy.stateElapsedSeconds;
        result.applied = true;
        result.killed = enemy.state == EnemyState::Dead;
        result.appliedDamage = healthBefore - enemy.health;
        result.remainingHealth = enemy.health;
        return result;
    }
    return result;
}

bool EnemySystem::Kill(const EnemyId id) noexcept {
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        RuntimeEnemy& enemy = enemies_[index];
        if (enemy.id != id || enemy.state == EnemyState::Dead) {
            continue;
        }
        enemy.health = 0.0f;
        enemy.hitFlashRemainingSeconds = kEnemyHitFlashSeconds;
        MarkDead(enemy);
        snapshots_[index].state = enemy.state;
        snapshots_[index].health = enemy.health;
        snapshots_[index].hitFlashRemainingSeconds =
            enemy.hitFlashRemainingSeconds;
        snapshots_[index].stateElapsedSeconds = enemy.stateElapsedSeconds;
        return true;
    }
    return false;
}

void EnemySystem::MarkDead(RuntimeEnemy& enemy) noexcept {
    // An attack event can be emitted by Update and then the same enemy can be
    // killed by the player's shot before Game consumes this frame's queue.
    // Dead enemies must not deal that already-queued damage or spawn a bullet.
    std::erase_if(
        attackEvents_,
        [&enemy](const EnemyAttackEvent& event) {
            return event.enemyId == enemy.id;
        });
    enemy.state = EnemyState::Dead;
    enemy.stateElapsedSeconds = 0.0f;
    enemy.attackCooldownSeconds = 0.0f;
    enemy.repathElapsedSeconds = 0.0f;
    enemy.stuckElapsedSeconds = 0.0f;
    enemy.navigationPurpose = NavigationPurpose::None;
    enemy.path.clear();
    enemy.nextWaypointIndex = 0;
    enemy.lastPlayerCell.reset();
}

void EnemySystem::RefreshSnapshots() {
    snapshots_.resize(enemies_.size());
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        const RuntimeEnemy& enemy = enemies_[index];
        EnemySnapshot& snapshot = snapshots_[index];
        snapshot.id = enemy.id;
        snapshot.definitionId = enemy.definition.id;
        snapshot.kind = enemy.kind;
        snapshot.state = enemy.state;
        snapshot.position = enemy.position;
        snapshot.collisionRadius = enemy.definition.hitboxRadius;
        snapshot.hitboxHeight = enemy.definition.hitboxHeight;
        snapshot.health = enemy.health;
        snapshot.maxHealth = enemy.definition.maxHealth;
        snapshot.defense = enemy.definition.defense;
        snapshot.hitFlashRemainingSeconds =
            enemy.hitFlashRemainingSeconds;
        snapshot.stateElapsedSeconds = enemy.stateElapsedSeconds;
    }
}

} // namespace fps
