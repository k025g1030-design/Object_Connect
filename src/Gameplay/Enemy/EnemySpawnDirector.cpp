#include "RetroFPS/Gameplay/Enemy/EnemySpawnDirector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fps {
namespace {

[[nodiscard]] EnemyKind OppositeKind(const EnemyKind kind) noexcept {
    return kind == EnemyKind::Melee ? EnemyKind::Ranged : EnemyKind::Melee;
}

[[nodiscard]] bool MarkerLess(
    const GridCoordinate left,
    const GridCoordinate right) noexcept {
    return left.row < right.row ||
           (left.row == right.row && left.column < right.column);
}

[[nodiscard]] bool HasValidDefinitionBasics(
    const EnemyDefinition& definition,
    const EnemyKind expectedKind) noexcept {
    return !definition.id.empty() && definition.kind == expectedKind &&
           std::isfinite(definition.damage) && definition.damage > 0.0f &&
           std::isfinite(definition.attackIntervalSeconds) &&
           definition.attackIntervalSeconds > 0.0f &&
           std::isfinite(definition.maxHealth) && definition.maxHealth > 0.0f &&
           std::isfinite(definition.defense) && definition.defense >= 0.0f &&
           std::isfinite(definition.hitboxHeight) &&
           definition.hitboxHeight > 0.0f &&
           !definition.texturePath.empty();
}

} // namespace

bool EnemySpawnDirector::Initialize(
    const GridMap& map,
    const LevelDefinition& level,
    const EnemyCatalog& enemies,
    std::string& error) {
    const EnemyDefinition* const melee = enemies.FindByKind(EnemyKind::Melee);
    const EnemyDefinition* const ranged = enemies.FindByKind(EnemyKind::Ranged);
    if (melee == nullptr || ranged == nullptr) {
        Reset();
        error = "EnemySpawnDirector requires one melee and one ranged definition.";
        return false;
    }
    return Initialize(map, level, *melee, *ranged, error);
}

bool EnemySpawnDirector::Initialize(
    const GridMap& map,
    const LevelDefinition& level,
    const EnemyDefinition& meleeDefinition,
    const EnemyDefinition& rangedDefinition,
    std::string& error) {
    Reset();
    error.clear();

    if (level.id.empty()) {
        error = "EnemySpawnDirector level ID must be non-empty.";
        return false;
    }
    if (map.GetWidth() == 0 || map.GetHeight() == 0) {
        error = "EnemySpawnDirector requires a non-empty map.";
        return false;
    }
    if (!HasValidDefinitionBasics(meleeDefinition, EnemyKind::Melee) ||
        !HasValidDefinitionBasics(rangedDefinition, EnemyKind::Ranged) ||
        meleeDefinition.id == rangedDefinition.id) {
        error = "EnemySpawnDirector received invalid or duplicate enemy definitions.";
        return false;
    }
    if (level.activeEnemyLimit == 0) {
        error = "EnemySpawnDirector active enemy limit must be greater than zero.";
        return false;
    }

    const std::uint64_t totalQuota =
        static_cast<std::uint64_t>(level.meleeEnemyCount) +
        static_cast<std::uint64_t>(level.rangedEnemyCount);
    if (totalQuota > (std::numeric_limits<std::uint32_t>::max)()) {
        error = "EnemySpawnDirector enemy quota exceeds the supported range.";
        return false;
    }

    for (const EnemySpawnPoint& spawn : map.GetEnemySpawnPoints()) {
        if (spawn.kind == EnemyKind::Melee) {
            meleeMarkers_.push_back(spawn.cell);
        } else if (spawn.kind == EnemyKind::Ranged) {
            rangedMarkers_.push_back(spawn.cell);
        } else {
            error = "EnemySpawnDirector map contains an unsupported spawn marker.";
            Reset();
            return false;
        }
    }
    std::ranges::sort(meleeMarkers_, MarkerLess);
    std::ranges::sort(rangedMarkers_, MarkerLess);

    if (level.meleeEnemyCount > 0 && meleeMarkers_.empty()) {
        error = "EnemySpawnDirector melee quota requires at least one M marker.";
        Reset();
        return false;
    }
    if (level.rangedEnemyCount > 0 && rangedMarkers_.empty()) {
        error = "EnemySpawnDirector ranged quota requires at least one R marker.";
        Reset();
        return false;
    }

    levelId_ = level.id;
    meleeDefinition_ = meleeDefinition;
    rangedDefinition_ = rangedDefinition;
    remainingMeleeCount_ = level.meleeEnemyCount;
    remainingRangedCount_ = level.rangedEnemyCount;
    totalQuota_ = static_cast<std::uint32_t>(totalQuota);
    aliveLimit_ = level.activeEnemyLimit;
    nextKind_ = remainingMeleeCount_ > 0
                    ? EnemyKind::Melee
                    : EnemyKind::Ranged;
    initialized_ = true;
    return true;
}

void EnemySpawnDirector::Reset() noexcept {
    levelId_.clear();
    meleeDefinition_ = {};
    rangedDefinition_ = {};
    meleeMarkers_.clear();
    rangedMarkers_.clear();
    remainingMeleeCount_ = 0;
    remainingRangedCount_ = 0;
    spawnedCount_ = 0;
    totalQuota_ = 0;
    aliveLimit_ = 0;
    nextMeleeMarkerIndex_ = 0;
    nextRangedMarkerIndex_ = 0;
    nextKind_ = EnemyKind::Melee;
    initialized_ = false;
}

EnemySpawnBatchResult EnemySpawnDirector::SpawnAvailable(
    EnemySystem& system,
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    std::string& error) {
    EnemySpawnBatchResult batch{};
    error.clear();
    if (!initialized_) {
        error = "EnemySpawnDirector must be initialized before spawning.";
        return batch;
    }
    if (!system.IsInitialized()) {
        error = "EnemySpawnDirector requires an initialized EnemySystem.";
        return batch;
    }
    if (!std::isfinite(playerPosition.x) || !std::isfinite(playerPosition.z) ||
        !std::isfinite(playerCollisionRadius) || playerCollisionRadius <= 0.0f) {
        error = "EnemySpawnDirector player collider must be finite and valid.";
        return batch;
    }

    while (!IsQuotaExhausted() && system.GetAliveCount() < aliveLimit_) {
        bool spawned = false;
        bool blocked = false;
        const EnemyKind attemptOrder[2] = {
            nextKind_,
            OppositeKind(nextKind_),
        };
        for (const EnemyKind kind : attemptOrder) {
            const SpawnAttemptResult attempt = TrySpawnKind(
                kind,
                system,
                map,
                playerPosition,
                playerCollisionRadius,
                error);
            if (attempt.status == SpawnAttemptStatus::Invalid) {
                batch.blocked = blocked;
                batch.quotaExhausted = IsQuotaExhausted();
                return batch;
            }
            if (attempt.status == SpawnAttemptStatus::Blocked) {
                blocked = true;
                continue;
            }
            if (attempt.status == SpawnAttemptStatus::Spawned) {
                ++batch.spawnedCount;
                spawned = true;
                break;
            }
        }

        if (!spawned) {
            batch.blocked = blocked && !IsQuotaExhausted();
            break;
        }
    }

    batch.quotaExhausted = IsQuotaExhausted();
    return batch;
}

EnemySpawnDirector::SpawnAttemptResult EnemySpawnDirector::TrySpawnKind(
    const EnemyKind kind,
    EnemySystem& system,
    const GridMap& map,
    const Float2 playerPosition,
    const float playerCollisionRadius,
    std::string& error) {
    std::uint32_t& remaining = kind == EnemyKind::Melee
                                   ? remainingMeleeCount_
                                   : remainingRangedCount_;
    if (remaining == 0) {
        return {SpawnAttemptStatus::Exhausted};
    }

    const EnemyDefinition& definition =
        kind == EnemyKind::Melee ? meleeDefinition_ : rangedDefinition_;
    const std::vector<GridCoordinate>& markers =
        kind == EnemyKind::Melee ? meleeMarkers_ : rangedMarkers_;
    std::size_t& cursor = kind == EnemyKind::Melee
                              ? nextMeleeMarkerIndex_
                              : nextRangedMarkerIndex_;
    const std::size_t start = cursor % markers.size();

    for (std::size_t offset = 0; offset < markers.size(); ++offset) {
        const std::size_t markerIndex = (start + offset) % markers.size();
        const Float2 position =
            map.GetCellCenter(markers[markerIndex], system.GetCellSize());
        const EnemySpawnResult result = system.Spawn(
            map,
            playerPosition,
            playerCollisionRadius,
            position,
            definition,
            error);
        if (result.status == EnemySpawnStatus::Invalid) {
            if (error.empty()) {
                error = "EnemySpawnDirector received an invalid spawn request.";
            }
            return {SpawnAttemptStatus::Invalid};
        }
        if (result.status == EnemySpawnStatus::Blocked) {
            continue;
        }

        cursor = (markerIndex + 1) % markers.size();
        --remaining;
        ++spawnedCount_;
        nextKind_ = OppositeKind(kind);
        return {SpawnAttemptStatus::Spawned};
    }

    cursor = (start + 1) % markers.size();
    return {SpawnAttemptStatus::Blocked};
}

} // namespace fps
