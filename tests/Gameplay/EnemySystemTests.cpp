#include "../TestSupport.hpp"

#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseValidMap(
    TestContext& context,
    const std::string_view text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "valid enemy-system map should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error(
            "valid enemy-system test map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

void InitializeSystem(
    TestContext& context,
    EnemySystem& system,
    const GridMap& map,
    const EnemySettings settings = {}) {
    std::string error;
    const bool initialized = system.Initialize(
        map,
        map.GetSpawnPosition(),
        0.25f,
        1.0f,
        settings,
        error);
    context.Expect(initialized, "valid enemy system should initialize");
    if (!initialized) {
        throw std::runtime_error("enemy initialization failed: " + error);
    }
}

[[nodiscard]] float CenterDistance(
    const Float2 left,
    const Float2 right) noexcept {
    return std::hypot(right.x - left.x, right.z - left.z);
}

[[nodiscard]] EnemyDefinition MakeDefinition(
    const std::string& id,
    const EnemyKind kind) {
    return {
        id,
        kind,
        kind == EnemyKind::Melee ? 7.0f : 5.0f,
        kind == EnemyKind::Melee ? 0.5f : 0.6f,
        10.0f,
        3.0f,
        kind == EnemyKind::Melee ? 0.8f : 1.6f,
        "white1x1.png",
    };
}

void TestSettingsValidation(TestContext& context) {
    std::string error;
    context.Expect(
        ValidateEnemySettings({}, error) && error.empty(),
        "default enemy settings are valid");

    EnemySettings invalid{};
    invalid.collisionRadius = 0.0f;
    context.Expect(
        !ValidateEnemySettings(invalid, error) &&
            error.find("collision radius") != std::string::npos,
        "enemy settings reject a non-positive collision radius");

    invalid = {};
    invalid.rangedIdealSurfaceDistance =
        invalid.rangedTooCloseSurfaceDistance;
    context.Expect(
        !ValidateEnemySettings(invalid, error),
        "enemy settings require ordered ranged distances");

    invalid = {};
    invalid.meleeAttackIntervalSeconds =
        invalid.meleeAttackStateSeconds * 0.5f;
    context.Expect(
        !ValidateEnemySettings(invalid, error),
        "melee attack interval cannot be shorter than its state window");

    invalid = {};
    invalid.rangedAttackIntervalSeconds =
        invalid.rangedAttackStateSeconds * 0.5f;
    context.Expect(
        !ValidateEnemySettings(invalid, error),
        "ranged attack interval cannot be shorter than its state window");

    invalid = {};
    invalid.rangedSpeed = (std::numeric_limits<float>::quiet_NaN)();
    context.Expect(
        !ValidateEnemySettings(invalid, error),
        "enemy settings reject non-finite values");
}

void TestInitializationSnapshotsAndDeath(TestContext& context) {
    const GridMap emptyMap = ParseValidMap(context, "P...D");
    EnemySystem empty;
    InitializeSystem(context, empty, emptyMap);
    context.Expect(
        empty.GetSnapshots().empty() && empty.CollectAliveColliders().empty(),
        "a map may initialize with no enemies");

    const GridMap map = ParseValidMap(context, "P.M.RD");
    EnemySystem system;
    EnemySettings settings{};
    settings.health = 3.0f;
    InitializeSystem(context, system, map, settings);
    const std::span<const EnemySnapshot> initial = system.GetSnapshots();
    context.Expect(initial.size() == 2, "all typed spawns create snapshots");
    if (initial.size() != 2) {
        return;
    }
    context.Expect(
        initial[0].id == 1 && initial[1].id == 2,
        "enemy IDs are stable and follow row-major spawn order");
    context.Expect(
        initial[0].kind == EnemyKind::Melee &&
            initial[1].kind == EnemyKind::Ranged,
        "enemy snapshots preserve spawn kinds");
    context.Expect(
        initial[0].state == EnemyState::Idle &&
            NearlyEqual(initial[0].health, 3.0f),
        "new enemies start healthy and idle");
    context.Expect(
        system.CollectAliveColliders().size() == 2,
        "all live enemies initially block the player");

    const EnemyDamageResult partialDamage = system.ApplyDamage(1, 0.25f);
    context.Expect(partialDamage.applied, "ApplyDamage finds a live ID");
    context.Expect(
        NearlyEqual(system.GetSnapshots()[0].health, 2.0f) &&
            system.GetSnapshots()[0].id == 1,
        "minimum one damage changes health without changing the stable ID");
    context.Expect(
        !system.ApplyDamage(1, -1.0f).applied,
        "ApplyDamage rejects a non-positive amount");
    context.Expect(system.Kill(2), "Kill transitions a live enemy");
    context.Expect(
        system.GetSnapshots()[1].state == EnemyState::Dead &&
            NearlyEqual(system.GetSnapshots()[1].health, 0.0f),
        "Kill produces the terminal Dead state");
    context.Expect(
        system.GetSnapshots().size() == 2 &&
            system.CollectAliveColliders().size() == 1,
        "dead enemies remain visible but stop blocking");

    const Float2 deadPosition = system.GetSnapshots()[1].position;
    system.Update(map, map.GetSpawnPosition(), 0.25f, 0.5f);
    context.Expect(
        NearlyEqual(system.GetSnapshots()[1].position.x, deadPosition.x) &&
            NearlyEqual(system.GetSnapshots()[1].position.z, deadPosition.z) &&
            NearlyEqual(system.GetSnapshots()[1].stateElapsedSeconds, 0.5f),
        "dead enemies stop AI while their state time continues");
    context.Expect(!system.Kill(2), "Dead is terminal for Kill");
    context.Expect(
        !system.ApplyDamage(2, 1.0f).applied,
        "Dead is terminal for ApplyDamage");
    const EnemyDamageResult lethalDamage = system.ApplyDamage(1, 2.0f);
    context.Expect(
        lethalDamage.applied && lethalDamage.killed &&
            system.GetSnapshots()[0].state == EnemyState::Dead,
        "lethal damage enters Dead");
    context.Expect(
        system.CollectAliveColliders().empty(),
        "all dead enemies are removed from dynamic collision");

    system.Reset();
    context.Expect(
        !system.IsInitialized() && system.GetSnapshots().empty(),
        "Reset releases the level's enemy state");
}

void TestDataDrivenSpawnDamageFlashAndRetire(TestContext& context) {
    const GridMap map = ParseValidMap(context, "P...D");
    EnemySystem system;
    std::string error;
    context.Expect(
        system.InitializeEmpty(
            map,
            map.GetSpawnPosition(),
            0.25f,
            1.0f,
            {},
            error),
        "empty enemy simulation initializes for directed spawning");

    const EnemyDefinition definition =
        MakeDefinition("melee_test", EnemyKind::Melee);
    const Float2 spawnPosition = map.GetCellCenter({0, 2});
    const EnemySpawnResult spawned = system.Spawn(
        map,
        map.GetSpawnPosition(),
        0.25f,
        spawnPosition,
        definition,
        error);
    context.Expect(spawned.Spawned(), "a valid data definition spawns dynamically");
    context.Expect(
        system.GetSnapshots().size() == 1 &&
            system.GetSnapshots()[0].definitionId == definition.id &&
            NearlyEqual(system.GetSnapshots()[0].maxHealth, definition.maxHealth) &&
            NearlyEqual(system.GetSnapshots()[0].defense, definition.defense) &&
            NearlyEqual(system.GetSnapshots()[0].hitboxHeight, definition.hitboxHeight) &&
            system.GetSnapshots()[0].texturePath == definition.texturePath,
        "spawn snapshots expose definition-driven combat and rendering data");

    system.Update(
        map,
        {{2.0f, 0.5f}, 0.25f, 1.8f},
        0.01f);
    context.Expect(
        system.GetAttackEvents().size() == 1 &&
            system.GetAttackEvents()[0].definitionId == definition.id &&
            NearlyEqual(system.GetAttackEvents()[0].origin.y, 0.4f) &&
            NearlyEqual(system.GetAttackEvents()[0].target.y, 0.9f) &&
            NearlyEqual(system.GetAttackEvents()[0].damage, definition.damage),
        "attack events expose definition ID, 3D endpoints, and data-driven damage");

    const EnemyDamageResult partial = system.ApplyDamage(spawned.enemyId, 5.0f);
    context.Expect(
        partial.applied && !partial.killed &&
            NearlyEqual(partial.appliedDamage, 2.0f) &&
            NearlyEqual(partial.remainingHealth, 8.0f) &&
            NearlyEqual(
                system.GetSnapshots()[0].hitFlashRemainingSeconds,
                kEnemyHitFlashSeconds),
        "damage subtracts defense with a minimum of one and starts hit flash");

    system.Update(map, map.GetSpawnPosition(), 0.25f, 0.05f);
    context.Expect(
        NearlyEqual(
            system.GetSnapshots()[0].hitFlashRemainingSeconds,
            kEnemyHitFlashSeconds - 0.05f),
        "hit flash time decreases during simulation");

    const EnemyDamageResult lethal = system.ApplyDamage(spawned.enemyId, 100.0f);
    context.Expect(
        lethal.applied && lethal.killed && system.GetAliveCount() == 0 &&
            system.CollectAliveColliders().empty() &&
            system.CollectOccupiedColliders().size() == 1,
        "dead enemies leave the alive cap but occupy their slot while flashing");

    system.Update(map, map.GetSpawnPosition(), 0.25f, kEnemyHitFlashSeconds);
    context.Expect(
        system.RetireExpiredDead() == 1 && system.GetSnapshots().empty(),
        "expired dead enemies retire from the dynamic snapshot set");

    const EnemySpawnResult respawned = system.Spawn(
        map,
        map.GetSpawnPosition(),
        0.25f,
        spawnPosition,
        definition,
        error);
    context.Expect(
        respawned.Spawned() && respawned.enemyId > spawned.enemyId,
        "runtime IDs remain monotonic after retirement and later spawning");
}

void TestSpawnValidationAndEnemyOverlap(TestContext& context) {
    std::string error;

    const GridMap wallMap = ParseValidMap(
        context,
        "#####\n"
        "#P.M#\n"
        "#..D#\n"
        "#####");
    EnemySettings large{};
    large.collisionRadius = 0.6f;
    EnemySystem wallSystem;
    context.Expect(
        !wallSystem.Initialize(
            wallMap,
            wallMap.GetSpawnPosition(),
            0.25f,
            1.0f,
            large,
            error) &&
            error.find("Melee") != std::string::npos &&
            error.find("row 2, column 4") != std::string::npos,
        "wall-overlapping spawn errors include type and one-based coordinates");

    const GridMap playerOverlapMap = ParseValidMap(
        context,
        ".....\n"
        ".PM.D\n"
        ".....");
    EnemySystem overlapSystem;
    context.Expect(
        !overlapSystem.Initialize(
            playerOverlapMap,
            playerOverlapMap.GetSpawnPosition(),
            0.5f,
            1.0f,
            large,
            error) &&
            error.find("Melee") != std::string::npos &&
            error.find("row 2, column 3") != std::string::npos,
        "player-overlapping spawn errors identify the enemy spawn");

    EnemySettings tangentSettings{};
    tangentSettings.collisionRadius = 0.5f;
    EnemySystem tangentSystem;
    context.Expect(
        tangentSystem.Initialize(
            playerOverlapMap,
            playerOverlapMap.GetSpawnPosition(),
            0.5f,
            1.0f,
            tangentSettings,
            error),
        "exact player/enemy tangency is a legal spawn");

    const GridMap enemyOverlapMap = ParseValidMap(
        context,
        "......\n"
        ".PMM.D\n"
        "......");
    EnemySystem enemyOverlapSystem;
    context.Expect(
        enemyOverlapSystem.Initialize(
            enemyOverlapMap,
            enemyOverlapMap.GetSpawnPosition(),
            0.25f,
            1.0f,
            large,
            error) &&
            enemyOverlapSystem.GetSnapshots().size() == 2,
        "overlapping enemy spawn circles are legal");
}

void TestAttackStateEventsAndCooldown(TestContext& context) {
    const GridMap map = ParseValidMap(context, "P.MD");
    EnemySystem system;
    InitializeSystem(context, system, map);
    const Float2 nearbyPlayer{2.0f, 0.5f};

    system.Update(map, nearbyPlayer, 0.25f, 0.01f);
    context.Expect(
        system.GetSnapshots()[0].state == EnemyState::Attacking &&
            NearlyEqual(system.GetSnapshots()[0].stateElapsedSeconds, 0.0f),
        "entering attack exposes an Attacking snapshot for a drawable frame");
    context.Expect(
        system.GetAttackEvents().size() == 1 &&
            system.GetAttackEvents()[0].enemyId == system.GetSnapshots()[0].id &&
            system.GetAttackEvents()[0].kind == EnemyKind::Melee &&
            NearlyEqual(system.GetAttackEvents()[0].target.x, nearbyPlayer.x),
        "attack entry emits exactly one event with the sampled player position");

    system.Update(map, nearbyPlayer, 0.25f, 0.10f);
    context.Expect(
        system.GetSnapshots()[0].state == EnemyState::Attacking &&
            NearlyEqual(system.GetSnapshots()[0].stateElapsedSeconds, 0.10f) &&
            system.GetAttackEvents().empty(),
        "attack state time advances without repeating its event");

    system.Update(map, nearbyPlayer, 0.25f, 0.20f);
    context.Expect(
        system.GetSnapshots()[0].state == EnemyState::Idle &&
            system.GetAttackEvents().empty(),
        "attack window ends in Idle while cooldown remains");

    system.Update(map, nearbyPlayer, 0.25f, 0.58f);
    context.Expect(
        system.GetAttackEvents().empty(),
        "cooldown prevents an early repeated attack");
    system.Update(map, nearbyPlayer, 0.25f, 0.03f);
    context.Expect(
        system.GetSnapshots()[0].state == EnemyState::Attacking &&
            system.GetAttackEvents().size() == 1,
        "enemy attacks again only after the configured interval");

    const float elapsedBeforeInvalidDelta =
        system.GetSnapshots()[0].stateElapsedSeconds;
    system.Update(
        map,
        nearbyPlayer,
        0.25f,
        (std::numeric_limits<float>::quiet_NaN)());
    context.Expect(
        system.GetAttackEvents().empty() &&
            NearlyEqual(
                system.GetSnapshots()[0].stateElapsedSeconds,
                elapsedBeforeInvalidDelta),
        "invalid frame delta clears frame events but freezes simulation");
}

void TestMeleeNavigationAndBlockedAttack(TestContext& context) {
    const GridMap uMap = ParseValidMap(
        context,
        "#########\n"
        "#M......#\n"
        "#..###..#\n"
        "#..#P#..#\n"
        "#..#.#..#\n"
        "#.....D.#\n"
        "#########");
    EnemySystem system;
    InitializeSystem(context, system, uMap);

    float furthestZ = system.GetSnapshots()[0].position.z;
    bool attacked = false;
    for (int frame = 0; frame < 500 && !attacked; ++frame) {
        system.Update(uMap, uMap.GetSpawnPosition(), 0.25f, 0.05f);
        furthestZ = (std::max)(furthestZ, system.GetSnapshots()[0].position.z);
        attacked = !system.GetAttackEvents().empty();
    }
    context.Expect(
        furthestZ > 4.5f,
        "melee A* routes through the opening of a U-shaped wall");
    context.Expect(attacked, "melee attacks after navigating around the wall");

    const GridMap sealedMap = ParseValidMap(
        context,
        "#######\n"
        "#M#P.D#\n"
        "#######");
    EnemySystem sealed;
    InitializeSystem(context, sealed, sealedMap);
    const Float2 start = sealed.GetSnapshots()[0].position;
    for (int frame = 0; frame < 40; ++frame) {
        sealed.Update(sealedMap, sealedMap.GetSpawnPosition(), 0.25f, 0.05f);
        context.Expect(
            sealed.GetAttackEvents().empty(),
            "a wall blocks melee attack events");
    }
    context.Expect(
        sealed.GetSnapshots()[0].state == EnemyState::Idle &&
            NearlyEqual(sealed.GetSnapshots()[0].position.x, start.x) &&
            NearlyEqual(sealed.GetSnapshots()[0].position.z, start.z),
        "melee with no route remains safely Idle");
}

void TestRangedDistanceControlAndLineOfSight(TestContext& context) {
    const GridMap retreatMap = ParseValidMap(
        context,
        "###############\n"
        "#.............#\n"
        "#....RP......D#\n"
        "#.............#\n"
        "###############");
    EnemySystem retreat;
    InitializeSystem(context, retreat, retreatMap);
    const float initialX = retreat.GetSnapshots()[0].position.x;
    bool attacked = false;
    for (int frame = 0; frame < 200 && !attacked; ++frame) {
        retreat.Update(
            retreatMap, retreatMap.GetSpawnPosition(), 0.25f, 0.05f);
        attacked = !retreat.GetAttackEvents().empty();
    }
    const EnemySnapshot ranged = retreat.GetSnapshots()[0];
    const float rangedSurfaceDistance =
        CenterDistance(ranged.position, retreatMap.GetSpawnPosition()) -
        ranged.collisionRadius - 0.25f;
    context.Expect(
        ranged.position.x < initialX - 1.0f,
        "ranged enemy retreats when the player is too close");
    context.Expect(
        rangedSurfaceDistance >=
                retreat.GetSettings().rangedTooCloseSurfaceDistance - 0.1f &&
            rangedSurfaceDistance <=
                retreat.GetSettings().rangedMaximumAttackSurfaceDistance + 0.1f,
        "ranged retreat reaches a valid firing band");
    context.Expect(attacked, "ranged enemy attacks after establishing distance");

    const Float2 firingPosition = ranged.position;
    for (int frame = 0; frame < 5; ++frame) {
        retreat.Update(
            retreatMap, retreatMap.GetSpawnPosition(), 0.25f, 0.05f);
    }
    context.Expect(
        NearlyEqual(retreat.GetSnapshots()[0].position.x, firingPosition.x) &&
            NearlyEqual(retreat.GetSnapshots()[0].position.z, firingPosition.z),
        "ranged enemy holds its firing position while in range");

    const GridMap sightMap = ParseValidMap(
        context,
        "###########\n"
        "#R..#...P.#\n"
        "#...#.....#\n"
        "#.........#\n"
        "#D........#\n"
        "###########");
    EnemySystem sight;
    InitializeSystem(context, sight, sightMap);
    float furthestZ = sight.GetSnapshots()[0].position.z;
    attacked = false;
    for (int frame = 0; frame < 500 && !attacked; ++frame) {
        sight.Update(sightMap, sightMap.GetSpawnPosition(), 0.25f, 0.05f);
        furthestZ = (std::max)(furthestZ, sight.GetSnapshots()[0].position.z);
        attacked = !sight.GetAttackEvents().empty();
    }
    context.Expect(
        furthestZ > 3.0f,
        "ranged A* moves around a wall to a visible firing cell");
    context.Expect(attacked, "ranged enemy attacks only after finding line of sight");

    const GridMap sealedMap = ParseValidMap(
        context,
        "#######\n"
        "#R#P.D#\n"
        "#######");
    EnemySystem sealed;
    InitializeSystem(context, sealed, sealedMap);
    for (int frame = 0; frame < 40; ++frame) {
        sealed.Update(sealedMap, sealedMap.GetSpawnPosition(), 0.25f, 0.05f);
    }
    context.Expect(
        sealed.GetSnapshots()[0].state == EnemyState::Idle &&
            sealed.GetAttackEvents().empty(),
        "ranged enemy with no legal firing cell remains Idle");
}

void TestPlayerBlockingAndEnemyOverlap(TestContext& context) {
    const GridMap fastMap = ParseValidMap(
        context,
        "##########\n"
        "#M...P..D#\n"
        "##########");
    EnemySystem fast;
    InitializeSystem(context, fast, fastMap);
    fast.Update(fastMap, fastMap.GetSpawnPosition(), 0.25f, 10.0f);
    const EnemySnapshot stopped = fast.GetSnapshots()[0];
    context.Expect(
        CenterDistance(stopped.position, fastMap.GetSpawnPosition()) + 0.001f >=
            stopped.collisionRadius + 0.25f,
        "swept enemy movement cannot tunnel through the player collider");

    const GridMap sharedPathMap = ParseValidMap(
        context,
        "############\n"
        "#MM....P..D#\n"
        "############");
    EnemySystem sharedPath;
    InitializeSystem(context, sharedPath, sharedPathMap);
    for (int frame = 0; frame < 150; ++frame) {
        sharedPath.Update(
            sharedPathMap,
            sharedPathMap.GetSpawnPosition(),
            0.25f,
            0.05f);
    }
    const std::span<const EnemySnapshot> snapshots =
        sharedPath.GetSnapshots();
    context.Expect(
        snapshots.size() == 2 &&
            CenterDistance(snapshots[0].position, snapshots[1].position) < 0.05f,
        "enemies ignore one another and may overlap on a shared path");
}

} // namespace

void RunEnemySystemTests(TestContext& context) {
    TestSettingsValidation(context);
    TestInitializationSnapshotsAndDeath(context);
    TestDataDrivenSpawnDamageFlashAndRetire(context);
    TestSpawnValidationAndEnemyOverlap(context);
    TestAttackStateEventsAndCooldown(context);
    TestMeleeNavigationAndBlockedAttack(context);
    TestRangedDistanceControlAndLineOfSight(context);
    TestPlayerBlockingAndEnemyOverlap(context);
}

} // namespace fps::tests
