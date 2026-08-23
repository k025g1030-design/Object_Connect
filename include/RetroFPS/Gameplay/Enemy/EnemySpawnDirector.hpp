#pragma once

#include "RetroFPS/Data/GameData.hpp"
#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/World/GridMap.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fps {

struct EnemySpawnBatchResult final {
    std::size_t spawnedCount = 0;
    bool blocked = false;
    bool quotaExhausted = false;
};

// Engine-independent wave allocator. It alternates melee/ranged quotas while
// respecting the level's live-enemy cap. Marker positions are copied from the
// map in row-major order and reused round-robin across later waves.
class EnemySpawnDirector final {
public:
    [[nodiscard]] bool Initialize(
        const GridMap& map,
        const LevelDefinition& level,
        const EnemyCatalog& enemies,
        std::string& error);
    [[nodiscard]] bool Initialize(
        const GridMap& map,
        const LevelDefinition& level,
        const EnemyDefinition& meleeDefinition,
        const EnemyDefinition& rangedDefinition,
        std::string& error);
    void Reset() noexcept;

    [[nodiscard]] EnemySpawnBatchResult SpawnAvailable(
        EnemySystem& system,
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        std::string& error);

    [[nodiscard]] std::uint32_t GetRemainingMeleeCount() const noexcept {
        return remainingMeleeCount_;
    }
    [[nodiscard]] std::uint32_t GetRemainingRangedCount() const noexcept {
        return remainingRangedCount_;
    }
    [[nodiscard]] std::uint32_t GetSpawnedCount() const noexcept {
        return spawnedCount_;
    }
    [[nodiscard]] std::uint32_t GetTotalQuota() const noexcept {
        return totalQuota_;
    }
    [[nodiscard]] std::uint32_t GetAliveLimit() const noexcept {
        return aliveLimit_;
    }
    [[nodiscard]] bool IsQuotaExhausted() const noexcept {
        return remainingMeleeCount_ == 0 && remainingRangedCount_ == 0;
    }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    enum class SpawnAttemptStatus : std::uint8_t {
        Spawned,
        Blocked,
        Invalid,
        Exhausted,
    };

    struct SpawnAttemptResult final {
        SpawnAttemptStatus status = SpawnAttemptStatus::Invalid;
    };

    [[nodiscard]] SpawnAttemptResult TrySpawnKind(
        EnemyKind kind,
        EnemySystem& system,
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        std::string& error);

    LevelDefinitionId levelId_{};
    EnemyDefinition meleeDefinition_{};
    EnemyDefinition rangedDefinition_{};
    std::vector<GridCoordinate> meleeMarkers_;
    std::vector<GridCoordinate> rangedMarkers_;
    std::uint32_t remainingMeleeCount_ = 0;
    std::uint32_t remainingRangedCount_ = 0;
    std::uint32_t spawnedCount_ = 0;
    std::uint32_t totalQuota_ = 0;
    std::uint32_t aliveLimit_ = 0;
    std::size_t nextMeleeMarkerIndex_ = 0;
    std::size_t nextRangedMarkerIndex_ = 0;
    EnemyKind nextKind_ = EnemyKind::Melee;
    bool initialized_ = false;
};

} // namespace fps
