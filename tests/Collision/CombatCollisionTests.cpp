#include "../TestSupport.hpp"

#include "RetroFPS/Collision/CombatCollision.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <stdexcept>
#include <string>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseCombatMap(TestContext& context, const std::string& text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "combat collision fixture parses");
    if (!result.map.has_value()) {
        throw std::runtime_error(result.error);
    }
    return std::move(*result.map);
}

void TestNearestWallAndTarget(TestContext& context) {
    const GridMap blocked = ParseCombatMap(context, "#####\n#P#D#\n#####");
    const CombatTarget target{42, {{3.5f, 1.5f}, 1.8f, 0.25f}};
    const std::optional<CombatHit> wallHit = CombatCollision::Raycast(
        blocked, {}, {1.5f, 1.0f, 1.5f}, {1.0f, 0.0f, 0.0f}, 10.0f, {&target, 1});
    context.Expect(
        wallHit.has_value() && wallHit->kind == CombatHitKind::Wall &&
            NearlyEqual(wallHit->distance, 0.5f),
        "wall occludes a capsule behind it");

    const GridMap open = ParseCombatMap(context, "P..D");
    const CombatTarget openTarget{7, {{2.5f, 0.5f}, 1.8f, 0.25f}};
    const std::optional<CombatHit> targetHit = CombatCollision::Raycast(
        open, {}, {0.5f, 1.0f, 0.5f}, {1.0f, 0.0f, 0.0f}, 10.0f, {&openTarget, 1});
    context.Expect(
        targetHit.has_value() && targetHit->kind == CombatHitKind::Target &&
            targetHit->targetId == 7 && NearlyEqual(targetHit->distance, 1.75f),
        "open ray returns the nearest capsule target");
}

void TestFloorAndCapsuleSweep(TestContext& context) {
    const GridMap map = ParseCombatMap(context, "P.D");
    const std::optional<CombatHit> floorHit = CombatCollision::Raycast(
        map, {}, {0.5f, 1.0f, 0.5f}, {0.0f, -1.0f, 0.0f}, 5.0f);
    context.Expect(
        floorHit.has_value() && floorHit->kind == CombatHitKind::Floor &&
            NearlyEqual(floorHit->distance, 1.0f),
        "downward combat ray hits the floor");

    const VerticalCapsule capsule{{2.5f, 0.5f}, 1.8f, 0.25f};
    const std::optional<float> fraction = CombatCollision::SweepSegmentAgainstCapsule(
        {0.5f, 1.0f, 0.5f}, {4.5f, 1.0f, 0.5f}, 0.05f, capsule);
    context.Expect(
        fraction.has_value() && NearlyEqual(*fraction, 0.425f),
        "swept sphere expands the target capsule and reports normalized time");

    const std::optional<float> startingInside =
        CombatCollision::SweepSegmentAgainstCapsule(
            {2.5f, 1.0f, 0.5f}, {2.6f, 1.0f, 0.5f}, 0.05f, capsule);
    context.Expect(
        startingInside.has_value() && NearlyEqual(*startingInside, 0.0f),
        "capsule sweep reports an immediate hit when starting overlapped");
    context.ExpectThrows<std::invalid_argument>(
        [&capsule]() {
            static_cast<void>(CombatCollision::SweepSegmentAgainstCapsule(
                {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, -0.1f, capsule));
        },
        "zero-length capsule sweep rejects a negative radius");
}

} // namespace

void RunCombatCollisionTests(TestContext& context) {
    TestNearestWallAndTarget(context);
    TestFloorAndCapsuleSweep(context);
}

} // namespace fps::tests
