#pragma once

#include "RetroFPS/Collision/CombatCollision.hpp"
#include "RetroFPS/Math/Vector.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace fps {

class GridMap;
struct WorldSettings;

using ProjectileId = std::uint64_t;

enum class ProjectileKind {
    PlayerTracer,
    EnemyBullet,
};

struct ProjectileSnapshot final {
    ProjectileId id = 0;
    ProjectileKind kind = ProjectileKind::PlayerTracer;
    Float3 position{};
    float radius = 0.0f;
};

struct PlayerProjectileHit final {
    ProjectileId projectileId = 0;
    float damage = 0.0f;
};

struct ProjectileSettings final {
    float playerTracerSpeed = 35.0f;
    float playerTracerRadius = 0.04f;
    float enemyProjectileSpeed = 8.0f;
    float enemyProjectileRadius = 0.06f;
    float enemyProjectileLifetimeSeconds = 5.0f;
};

class ProjectileSystem final {
public:
    [[nodiscard]] bool Configure(ProjectileSettings settings) noexcept;
    void Clear() noexcept;

    [[nodiscard]] ProjectileId SpawnPlayerTracer(Float3 start, Float3 end);
    [[nodiscard]] ProjectileId SpawnEnemyProjectile(
        Float3 start, Float3 target, float damage);

    [[nodiscard]] std::span<const PlayerProjectileHit> Update(
        const GridMap& map,
        const WorldSettings& worldSettings,
        const VerticalCapsule& playerCapsule,
        float deltaSeconds);

    [[nodiscard]] std::span<const ProjectileSnapshot> GetSnapshots() const noexcept {
        return snapshots_;
    }
    [[nodiscard]] const ProjectileSettings& GetSettings() const noexcept { return settings_; }

private:
    struct RuntimeProjectile final {
        ProjectileId id = 0;
        ProjectileKind kind = ProjectileKind::PlayerTracer;
        Float3 position{};
        Float3 velocity{};
        float radius = 0.0f;
        float remainingDistance = 0.0f;
        float remainingLifetimeSeconds = 0.0f;
        float damage = 0.0f;
    };

    void RefreshSnapshots();

    ProjectileSettings settings_{};
    ProjectileId nextId_ = 1;
    std::vector<RuntimeProjectile> projectiles_;
    std::vector<ProjectileSnapshot> snapshots_;
    std::vector<PlayerProjectileHit> playerHits_;
};

} // namespace fps
