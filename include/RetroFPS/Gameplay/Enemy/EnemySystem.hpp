#pragma once

#include "RetroFPS/Collision/GridCollision.hpp"
#include "RetroFPS/Data/GameData.hpp"
#include "RetroFPS/Math/Vector.hpp"
#include "RetroFPS/World/GridMap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fps {

using EnemyId = std::uint64_t;

inline constexpr float kEnemyHitFlashSeconds = 0.12f;

enum class EnemyState : std::uint8_t {
    Idle,
    Moving,
    Attacking,
    Dead,
};

struct EnemySettings final {
    float collisionRadius = 0.20f;
    // Compatibility defaults used only by Initialize's marker-spawning path.
    // Runtime Spawn calls always use the supplied EnemyDefinition instead.
    float health = 1.0f;
    float repathIntervalSeconds = 0.20f;

    float meleeSpeed = 1.8f;
    float meleeAttackSurfaceDistance = 0.15f;
    float meleeAttackStateSeconds = 0.30f;
    float meleeAttackIntervalSeconds = 0.90f;

    float rangedSpeed = 1.4f;
    float rangedTooCloseSurfaceDistance = 2.0f;
    float rangedIdealSurfaceDistance = 4.0f;
    float rangedMaximumAttackSurfaceDistance = 6.0f;
    float rangedAttackStateSeconds = 0.25f;
    float rangedAttackIntervalSeconds = 1.25f;
};

[[nodiscard]] bool ValidateEnemySettings(
    const EnemySettings& settings,
    std::string& error);

struct EnemySnapshot final {
    EnemyId id = 0;
    EnemyDefinitionId definitionId{};
    EnemyKind kind = EnemyKind::Melee;
    EnemyState state = EnemyState::Idle;
    Float2 position{};
    float collisionRadius = 0.0f;
    float hitboxHeight = 0.0f;
    float health = 0.0f;
    float maxHealth = 0.0f;
    float defense = 0.0f;
    float hitFlashRemainingSeconds = 0.0f;
    float stateElapsedSeconds = 0.0f;
    std::string texturePath;
};

struct EnemyAttackEvent final {
    EnemyId enemyId = 0;
    EnemyDefinitionId definitionId{};
    EnemyKind kind = EnemyKind::Melee;
    Float3 origin{};
    Float3 target{};
    float damage = 0.0f;
};

struct EnemyTarget final {
    Float2 position{};
    float collisionRadius = 0.0f;
    float hitboxHeight = 0.0f;
};

enum class EnemySpawnStatus : std::uint8_t {
    Spawned,
    Blocked,
    Invalid,
};

struct EnemySpawnResult final {
    EnemySpawnStatus status = EnemySpawnStatus::Invalid;
    EnemyId enemyId = 0;

    [[nodiscard]] bool Spawned() const noexcept {
        return status == EnemySpawnStatus::Spawned;
    }
};

struct EnemyDamageResult final {
    bool applied = false;
    bool killed = false;
    float rawDamage = 0.0f;
    float appliedDamage = 0.0f;
    float remainingHealth = 0.0f;
};

// Engine-independent enemy simulation. The map remains owned by the caller;
// Initialize and Update therefore both receive the map used by the current
// level session.
class EnemySystem final {
public:
    EnemySystem() = default;

    [[nodiscard]] bool Initialize(
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        float cellSize,
        EnemySettings settings,
        std::string& error);
    // Initializes navigation/collision state without consuming map spawn markers.
    // EnemySpawnDirector uses this path and supplies a definition to each Spawn.
    [[nodiscard]] bool InitializeEmpty(
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        float cellSize,
        EnemySettings settings,
        std::string& error);
    void Reset() noexcept;

    [[nodiscard]] EnemySpawnResult Spawn(
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        Float2 spawnPosition,
        const EnemyDefinition& definition,
        std::string& error);
    [[nodiscard]] bool Retire(EnemyId id) noexcept;
    [[nodiscard]] bool RetireDead(EnemyId id) noexcept;
    [[nodiscard]] std::size_t RetireExpiredDead() noexcept;

    void Update(
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        float deltaSeconds);
    void Update(
        const GridMap& map,
        const EnemyTarget& player,
        float deltaSeconds);

    [[nodiscard]] std::span<const EnemySnapshot> GetSnapshots() const noexcept {
        return snapshots_;
    }
    [[nodiscard]] std::span<const EnemyAttackEvent> GetAttackEvents() const noexcept {
        return attackEvents_;
    }
    [[nodiscard]] std::vector<CircleObstacle> CollectAliveColliders() const;
    // Includes live instances and dead instances that are still flashing, so a
    // wave spawner cannot reuse a visibly occupied slot.
    [[nodiscard]] std::vector<CircleObstacle> CollectOccupiedColliders() const;

    // Applies max(1, raw damage - definition defense). Unknown/dead enemies and
    // invalid raw damage return an unapplied result.
    [[nodiscard]] EnemyDamageResult ApplyDamage(EnemyId id, float rawDamage) noexcept;
    [[nodiscard]] bool Kill(EnemyId id) noexcept;

    [[nodiscard]] const EnemySettings& GetSettings() const noexcept { return settings_; }
    [[nodiscard]] float GetCellSize() const noexcept { return cellSize_; }
    [[nodiscard]] std::size_t GetAliveCount() const noexcept;
    [[nodiscard]] std::size_t GetInstanceCount() const noexcept { return enemies_.size(); }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    enum class NavigationPurpose : std::uint8_t {
        None,
        Chase,
        Retreat,
        FiringPosition,
    };

    struct RuntimeEnemy final {
        EnemyId id = 0;
        EnemyKind kind = EnemyKind::Melee;
        EnemyState state = EnemyState::Idle;
        Float2 position{};
        EnemyDefinition definition{};
        float health = 0.0f;
        float hitFlashRemainingSeconds = 0.0f;
        float stateElapsedSeconds = 0.0f;
        float attackCooldownSeconds = 0.0f;
        float repathElapsedSeconds = 0.0f;
        float stuckElapsedSeconds = 0.0f;
        NavigationPurpose navigationPurpose = NavigationPurpose::None;
        std::vector<GridCoordinate> path;
        std::size_t nextWaypointIndex = 0;
        std::optional<GridCoordinate> lastPlayerCell;
    };

    [[nodiscard]] bool InitializeState(
        const GridMap& map,
        Float2 playerPosition,
        float playerCollisionRadius,
        float cellSize,
        EnemySettings settings,
        std::string& error);
    [[nodiscard]] bool ValidateDefinition(
        const EnemyDefinition& definition,
        std::string& error) const;
    void MarkDead(RuntimeEnemy& enemy) noexcept;
    void RefreshSnapshots();

    EnemySettings settings_{};
    float cellSize_ = 1.0f;
    std::size_t mapWidth_ = 0;
    std::size_t mapHeight_ = 0;
    EnemyId nextEnemyId_ = 1;
    bool initialized_ = false;
    std::vector<RuntimeEnemy> enemies_;
    std::vector<EnemySnapshot> snapshots_;
    std::vector<EnemyAttackEvent> attackEvents_;
};

} // namespace fps
