#include "RetroFPS/Gameplay/Combat/ProjectileSystem.hpp"

#include "RetroFPS/World/GridMap.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fps {
namespace {

constexpr float kEpsilon = 0.000001f;

[[nodiscard]] bool IsFinite(const Float3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float Length(const Float3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] Float3 Normalize(const Float3 value) {
    const float length = Length(value);
    if (!std::isfinite(length) || length <= kEpsilon) {
        throw std::invalid_argument("projectile direction must be finite and non-zero");
    }
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] Float3 AddScaled(
    const Float3 value, const Float3 direction, const float distance) noexcept {
    return {
        value.x + direction.x * distance,
        value.y + direction.y * distance,
        value.z + direction.z * distance,
    };
}

[[nodiscard]] bool ValidateSettings(const ProjectileSettings& settings) noexcept {
    return std::isfinite(settings.playerTracerSpeed) && settings.playerTracerSpeed > 0.0f &&
           std::isfinite(settings.playerTracerRadius) && settings.playerTracerRadius > 0.0f &&
           std::isfinite(settings.enemyProjectileSpeed) &&
           settings.enemyProjectileSpeed > 0.0f &&
           std::isfinite(settings.enemyProjectileRadius) &&
           settings.enemyProjectileRadius > 0.0f &&
           std::isfinite(settings.enemyProjectileLifetimeSeconds) &&
           settings.enemyProjectileLifetimeSeconds > 0.0f;
}

} // namespace

bool ProjectileSystem::Configure(const ProjectileSettings settings) noexcept {
    if (!ValidateSettings(settings)) {
        return false;
    }
    settings_ = settings;
    Clear();
    return true;
}

void ProjectileSystem::Clear() noexcept {
    projectiles_.clear();
    snapshots_.clear();
    playerHits_.clear();
    nextId_ = 1;
}

ProjectileId ProjectileSystem::SpawnPlayerTracer(const Float3 start, const Float3 end) {
    if (!IsFinite(start) || !IsFinite(end)) {
        throw std::invalid_argument("player tracer endpoints must be finite");
    }
    const Float3 delta{end.x - start.x, end.y - start.y, end.z - start.z};
    const float distance = Length(delta);
    if (!std::isfinite(distance) || distance <= kEpsilon) {
        return 0;
    }
    const Float3 direction = Normalize(delta);
    const ProjectileId id = nextId_++;
    projectiles_.push_back({
        id,
        ProjectileKind::PlayerTracer,
        start,
        {
            direction.x * settings_.playerTracerSpeed,
            direction.y * settings_.playerTracerSpeed,
            direction.z * settings_.playerTracerSpeed,
        },
        settings_.playerTracerRadius,
        distance,
        distance / settings_.playerTracerSpeed,
        0,
    });
    RefreshSnapshots();
    return id;
}

ProjectileId ProjectileSystem::SpawnEnemyProjectile(
    const Float3 start, const Float3 target, const float damage) {
    if (!IsFinite(start) || !IsFinite(target) || !std::isfinite(damage) || damage <= 0.0f) {
        throw std::invalid_argument(
            "enemy projectile requires finite endpoints and positive damage");
    }
    const Float3 direction = Normalize(
        {target.x - start.x, target.y - start.y, target.z - start.z});
    const ProjectileId id = nextId_++;
    projectiles_.push_back({
        id,
        ProjectileKind::EnemyBullet,
        start,
        {
            direction.x * settings_.enemyProjectileSpeed,
            direction.y * settings_.enemyProjectileSpeed,
            direction.z * settings_.enemyProjectileSpeed,
        },
        settings_.enemyProjectileRadius,
        (std::numeric_limits<float>::max)(),
        settings_.enemyProjectileLifetimeSeconds,
        damage,
    });
    RefreshSnapshots();
    return id;
}

std::span<const PlayerProjectileHit> ProjectileSystem::Update(
    const GridMap& map,
    const WorldSettings& worldSettings,
    const VerticalCapsule& playerCapsule,
    const float deltaSeconds) {
    playerHits_.clear();
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return playerHits_;
    }

    for (RuntimeProjectile& projectile : projectiles_) {
        if (projectile.remainingLifetimeSeconds <= 0.0f) {
            continue;
        }

        const float stepSeconds =
            (std::min)(deltaSeconds, projectile.remainingLifetimeSeconds);
        const float velocityLength = Length(projectile.velocity);
        float travelDistance = velocityLength * stepSeconds;
        if (projectile.kind == ProjectileKind::PlayerTracer) {
            travelDistance = (std::min)(travelDistance, projectile.remainingDistance);
        }
        const Float3 direction = Normalize(projectile.velocity);
        const Float3 end = AddScaled(projectile.position, direction, travelDistance);

        if (projectile.kind == ProjectileKind::EnemyBullet) {
            const std::optional<CombatHit> worldHit = CombatCollision::Raycast(
                map,
                worldSettings,
                projectile.position,
                direction,
                travelDistance,
                {},
                projectile.radius);
            const std::optional<float> playerFraction =
                CombatCollision::SweepSegmentAgainstCapsule(
                    projectile.position, end, projectile.radius, playerCapsule);
            const float worldFraction = worldHit.has_value() && travelDistance > kEpsilon
                                            ? worldHit->distance / travelDistance
                                            : (std::numeric_limits<float>::max)();
            if (playerFraction.has_value() && *playerFraction <= worldFraction) {
                projectile.position = AddScaled(
                    projectile.position, direction, travelDistance * *playerFraction);
                playerHits_.push_back({projectile.id, projectile.damage});
                projectile.remainingLifetimeSeconds = 0.0f;
                continue;
            }
            if (worldHit.has_value()) {
                projectile.position = worldHit->position;
                projectile.remainingLifetimeSeconds = 0.0f;
                continue;
            }
        }

        projectile.position = end;
        projectile.remainingLifetimeSeconds =
            (std::max)(0.0f, projectile.remainingLifetimeSeconds - stepSeconds);
        if (projectile.kind == ProjectileKind::PlayerTracer) {
            projectile.remainingDistance =
                (std::max)(0.0f, projectile.remainingDistance - travelDistance);
            if (projectile.remainingDistance <= kEpsilon) {
                projectile.remainingLifetimeSeconds = 0.0f;
            }
        }
    }

    std::erase_if(projectiles_, [](const RuntimeProjectile& projectile) {
        return projectile.remainingLifetimeSeconds <= 0.0f;
    });
    RefreshSnapshots();
    return playerHits_;
}

void ProjectileSystem::RefreshSnapshots() {
    snapshots_.clear();
    snapshots_.reserve(projectiles_.size());
    for (const RuntimeProjectile& projectile : projectiles_) {
        snapshots_.push_back({
            projectile.id,
            projectile.kind,
            projectile.position,
            projectile.radius,
        });
    }
}

} // namespace fps
