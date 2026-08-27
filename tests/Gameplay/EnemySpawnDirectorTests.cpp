#include "../TestSupport.hpp"

#include "RetroFPS/Gameplay/Enemy/EnemySpawnDirector.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseDirectorMap(
    TestContext& context,
    const std::string_view text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "valid spawn-director map should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error(
            "spawn-director map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

[[nodiscard]] EnemyDefinition DirectorDefinition(const EnemyKind kind) {
    EnemyDefinition definition{};
    definition.id =
        kind == EnemyKind::Melee ? "melee_director" : "ranged_director";
    definition.kind = kind;
    definition.damage = kind == EnemyKind::Melee ? 15.0f : 10.0f;
    definition.attackIntervalSeconds =
        kind == EnemyKind::Melee ? 0.9f : 1.25f;
    definition.maxHealth = kind == EnemyKind::Melee ? 50.0f : 40.0f;
    definition.defense = kind == EnemyKind::Melee ? 5.0f : 0.0f;
    definition.hitboxRadius = 0.20f;
    definition.hitboxHeight = kind == EnemyKind::Melee ? 0.8f : 1.6f;
    definition.renderWidth =
        kind == EnemyKind::Melee ? 0.973913f : 1.230769f;
    definition.renderHeight = definition.hitboxHeight;
    definition.texturePath = "white1x1.png";
    definition.frameWidthPixels = kind == EnemyKind::Melee ? 560u : 700u;
    definition.frameHeightPixels = kind == EnemyKind::Melee ? 460u : 910u;
    definition.animations.idle.frameCount = 3;
    definition.animations.idle.secondsPerFrame = 0.10f;
    definition.animations.moving.frameCount = 4;
    definition.animations.moving.secondsPerFrame = 0.10f;
    definition.animations.attacking.frameCount =
        kind == EnemyKind::Melee ? 6u : 5u;
    definition.animations.attacking.secondsPerFrame = 0.05f;
    definition.animations.attacking.eventFrameIndex =
        kind == EnemyKind::Melee ? 3u : 2u;
    if (kind == EnemyKind::Ranged) {
        definition.animations.attacking.muzzlePixel =
            EnemyAnimationPixelPoint{350, 420};
    }
    definition.animations.dead.frameCount = 4;
    definition.animations.dead.secondsPerFrame = 0.10f;
    return definition;
}

void InitializeEmptySystem(
    TestContext& context,
    EnemySystem& system,
    const GridMap& map) {
    std::string error;
    const bool initialized = system.InitializeEmpty(
        map,
        map.GetSpawnPosition(),
        0.25f,
        1.0f,
        {},
        error);
    context.Expect(initialized, "spawn director enemy system initializes empty");
    if (!initialized) {
        throw std::runtime_error(
            "empty enemy system failed to initialize: " + error);
    }
}

[[nodiscard]] LevelDefinition DirectorLevel(
    const std::uint32_t meleeCount,
    const std::uint32_t rangedCount,
    const std::uint32_t aliveLimit) {
    LevelDefinition level{};
    level.id = "director_room";
    level.name = "Director Room";
    level.mapPath = "maps/director.txt";
    level.meleeEnemyCount = meleeCount;
    level.rangedEnemyCount = rangedCount;
    level.activeEnemyLimit = aliveLimit;
    level.clearKillCount = meleeCount + rangedCount;
    return level;
}

void TestAlternatingQuotaAliveCapAndRoundRobin(TestContext& context) {
    const GridMap map = ParseDirectorMap(
        context,
        "#############\n"
        "#P.M.M.R.R.D#\n"
        "#############");
    EnemySystem system;
    InitializeEmptySystem(context, system, map);

    EnemySpawnDirector director;
    std::string error;
    const LevelDefinition level = DirectorLevel(3, 2, 2);
    context.Expect(
        director.Initialize(
            map,
            level,
            DirectorDefinition(EnemyKind::Melee),
            DirectorDefinition(EnemyKind::Ranged),
            error),
        "valid alternating spawn quota initializes");

    const EnemySpawnBatchResult first = director.SpawnAvailable(
        system,
        map,
        map.GetSpawnPosition(),
        0.25f,
        error);
    context.Expect(
        first.spawnedCount == 2 && system.GetAliveCount() == 2 &&
            system.GetSnapshots()[0].kind == EnemyKind::Melee &&
            system.GetSnapshots()[1].kind == EnemyKind::Ranged,
        "director alternates melee then ranged until the alive cap");
    context.Expect(
        NearlyEqual(system.GetSnapshots()[0].position.x, map.GetCellCenter({1, 3}).x) &&
            NearlyEqual(system.GetSnapshots()[0].position.z, map.GetCellCenter({1, 3}).z) &&
            NearlyEqual(system.GetSnapshots()[1].position.x, map.GetCellCenter({1, 7}).x) &&
            NearlyEqual(system.GetSnapshots()[1].position.z, map.GetCellCenter({1, 7}).z),
        "first wave consumes each kind's first row-major marker");

    const EnemyId firstMeleeId = system.GetSnapshots()[0].id;
    const EnemyId firstRangedId = system.GetSnapshots()[1].id;
    context.Expect(system.Kill(firstMeleeId), "first melee enemy can die");
    const EnemySpawnBatchResult second = director.SpawnAvailable(
        system,
        map,
        map.GetSpawnPosition(),
        0.25f,
        error);
    context.Expect(
        second.spawnedCount == 1 && system.GetAliveCount() == 2 &&
            system.GetSnapshots().back().kind == EnemyKind::Melee &&
            NearlyEqual(system.GetSnapshots().back().position.x, map.GetCellCenter({1, 5}).x) &&
            NearlyEqual(system.GetSnapshots().back().position.z, map.GetCellCenter({1, 5}).z),
        "dead flashing enemies leave the cap while the next safe marker is used");

    context.Expect(system.Kill(firstRangedId), "first ranged enemy can die");
    const EnemySpawnBatchResult third = director.SpawnAvailable(
        system,
        map,
        map.GetSpawnPosition(),
        0.25f,
        error);
    context.Expect(
        third.spawnedCount == 1 &&
            system.GetSnapshots().back().kind == EnemyKind::Ranged &&
            NearlyEqual(system.GetSnapshots().back().position.x, map.GetCellCenter({1, 9}).x) &&
            NearlyEqual(system.GetSnapshots().back().position.z, map.GetCellCenter({1, 9}).z),
        "ranged quota advances round-robin after the next cap slot opens");

    const EnemyId secondMeleeId = system.GetSnapshots()[2].id;
    context.Expect(
        !system.RetireDead(firstMeleeId) &&
            !system.RetireDead(firstRangedId),
        "dead enemies cannot retire before their death animation completes");
    system.Update(map, map.GetSpawnPosition(), 0.25f, 0.4f);
    context.Expect(
        system.RetireDead(firstMeleeId) && system.RetireDead(firstRangedId),
        "dead enemies can retire after their death animation completes");
    context.Expect(system.Kill(secondMeleeId), "second melee opens the final quota slot");
    const EnemySpawnBatchResult finalBatch = director.SpawnAvailable(
        system,
        map,
        map.GetSpawnPosition(),
        0.25f,
        error);
    context.Expect(
        finalBatch.spawnedCount == 1 && finalBatch.quotaExhausted &&
            director.GetSpawnedCount() == 5 && director.IsQuotaExhausted(),
        "director exhausts the exact total quota across capped waves");
    context.Expect(
        NearlyEqual(system.GetSnapshots().back().position.x, map.GetCellCenter({1, 3}).x) &&
            NearlyEqual(system.GetSnapshots().back().position.z, map.GetCellCenter({1, 3}).z),
        "marker cursor wraps to the first row-major slot after a full cycle");
}

void TestSafeSlotRetryAndValidation(TestContext& context) {
    const GridMap map = ParseDirectorMap(context, "PMM.D");
    EnemySystem system;
    InitializeEmptySystem(context, system, map);
    EnemySpawnDirector director;
    std::string error;
    const LevelDefinition level = DirectorLevel(1, 0, 1);
    context.Expect(
        director.Initialize(
            map,
            level,
            DirectorDefinition(EnemyKind::Melee),
            DirectorDefinition(EnemyKind::Ranged),
            error),
        "single-kind quota initializes without opposite markers");

    const Float2 firstMarker = map.GetCellCenter({0, 1});
    const EnemySpawnBatchResult spawned = director.SpawnAvailable(
        system,
        map,
        firstMarker,
        0.25f,
        error);
    context.Expect(
        spawned.spawnedCount == 1 && spawned.quotaExhausted &&
            NearlyEqual(system.GetSnapshots()[0].position.x, map.GetCellCenter({0, 2}).x) &&
            NearlyEqual(system.GetSnapshots()[0].position.z, map.GetCellCenter({0, 2}).z),
        "director retries later row-major markers when the first slot is unsafe");

    const GridMap missingMarkerMap = ParseDirectorMap(context, "P...D");
    EnemySpawnDirector invalid;
    context.Expect(
        !invalid.Initialize(
            missingMarkerMap,
            level,
            DirectorDefinition(EnemyKind::Melee),
            DirectorDefinition(EnemyKind::Ranged),
            error) &&
            !invalid.IsInitialized(),
        "positive quotas reject maps without a matching marker kind");
}

} // namespace

void RunEnemySpawnDirectorTests(TestContext& context) {
    TestAlternatingQuotaAliveCapAndRoundRobin(context);
    TestSafeSlotRetryAndValidation(context);
}

} // namespace fps::tests
