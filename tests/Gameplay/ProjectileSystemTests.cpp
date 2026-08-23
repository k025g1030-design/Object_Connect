#include "../TestSupport.hpp"

#include "RetroFPS/Gameplay/Combat/ProjectileSystem.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <stdexcept>
#include <string>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseProjectileMap(TestContext& context, const std::string& text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "projectile fixture parses");
    if (!result.map.has_value()) {
        throw std::runtime_error(result.error);
    }
    return std::move(*result.map);
}

void TestEnemyProjectileSweep(TestContext& context) {
    ProjectileSystem projectiles;
    context.Expect(projectiles.Configure({}), "default projectile settings configure");
    const GridMap map = ParseProjectileMap(context, "P...D");
    const VerticalCapsule player{{2.5f, 0.5f}, 1.8f, 0.25f};
    const ProjectileId id = projectiles.SpawnEnemyProjectile(
        {0.5f, 1.0f, 0.5f}, {2.5f, 1.0f, 0.5f}, 10.5f);
    const std::span<const PlayerProjectileHit> hits =
        projectiles.Update(map, {}, player, 1.0f);
    context.Expect(
        hits.size() == 1 && hits[0].projectileId == id &&
            NearlyEqual(hits[0].damage, 10.5f),
        "fast enemy projectile preserves float damage and sweeps without tunneling");
    context.Expect(projectiles.GetSnapshots().empty(), "hit projectile is retired immediately");
}

void TestWallBlocksAndTracerExpires(TestContext& context) {
    ProjectileSystem projectiles;
    context.Expect(projectiles.Configure({}), "projectile system configures for wall test");
    const GridMap wallMap = ParseProjectileMap(context, "P#D");
    const VerticalCapsule player{{2.5f, 0.5f}, 1.8f, 0.25f};
    static_cast<void>(projectiles.SpawnEnemyProjectile(
        {0.5f, 1.0f, 0.5f}, {2.5f, 1.0f, 0.5f}, 10));
    context.Expect(
        projectiles.Update(wallMap, {}, player, 1.0f).empty(),
        "wall consumes an enemy projectile before the player capsule");

    const GridMap openMap = ParseProjectileMap(context, "P.D");
    static_cast<void>(projectiles.SpawnPlayerTracer(
        {0.5f, 1.0f, 0.5f}, {1.5f, 1.0f, 0.5f}));
    context.Expect(
        projectiles.GetSnapshots().size() == 1,
        "player tracer appears immediately for rendering");
    static_cast<void>(projectiles.Update(openMap, {}, player, 1.0f));
    context.Expect(projectiles.GetSnapshots().empty(), "tracer retires at its visual endpoint");
}

void TestEnemyProjectileLifetimeCapsTravel(TestContext& context) {
    ProjectileSystem projectiles;
    context.Expect(projectiles.Configure({}), "projectile system configures for lifetime test");
    const GridMap map = ParseProjectileMap(
        context, std::string{"P"} + std::string(43, '.') + "D");
    const VerticalCapsule player{{44.5f, 0.5f}, 1.8f, 0.25f};
    static_cast<void>(projectiles.SpawnEnemyProjectile(
        {0.5f, 1.0f, 0.5f}, {44.5f, 1.0f, 0.5f}, 10.0f));
    context.Expect(
        projectiles.Update(map, {}, player, 6.0f).empty(),
        "expired enemy projectile cannot hit beyond its five-second travel budget");
    context.Expect(
        projectiles.GetSnapshots().empty(),
        "enemy projectile retires when its lifetime is exhausted");
}

} // namespace

void RunProjectileSystemTests(TestContext& context) {
    TestEnemyProjectileSweep(context);
    TestWallBlocksAndTracerExpires(context);
    TestEnemyProjectileLifetimeCapsTravel(context);
}

} // namespace fps::tests
