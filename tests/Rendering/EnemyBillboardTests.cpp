#include "../TestSupport.hpp"

#include "RetroFPS/Rendering/EnemyRenderSettings.hpp"

#include <numbers>

namespace fps::tests {
namespace {

void TestBillboardPose(TestContext& context) {
    const EnemyRenderSettings settings{};
    const EnemyBillboardPose melee = ResolveEnemyBillboardPose(
        settings, EnemyKind::Melee, {1.0f, 1.0f}, {1.0f, 2.0f});
    const EnemyBillboardPose ranged = ResolveEnemyBillboardPose(
        settings, EnemyKind::Ranged, {1.0f, 1.0f}, {2.0f, 1.0f});

    context.Expect(NearlyEqual(melee.width, 0.7f), "melee billboard uses configured width");
    context.Expect(NearlyEqual(melee.height, 0.8f), "melee billboard is 0.8 metres tall");
    context.Expect(
        NearlyEqual(ranged.height, 1.6f) && NearlyEqual(melee.height * 2.0f, ranged.height),
        "melee billboard height is exactly half the ranged height");
    context.Expect(
        NearlyEqual(melee.centerY, 0.4f) && NearlyEqual(ranged.centerY, 0.8f),
        "billboard centers keep their bottom edge on the ground");
    context.Expect(NearlyEqual(melee.yawRadians, 0.0f), "viewer on +Z produces zero yaw");
    context.Expect(
        NearlyEqual(ranged.yawRadians, std::numbers::pi_v<float> * 0.5f),
        "viewer on +X rotates the billboard by 90 degrees");

    const EnemyBillboardPose coincident = ResolveEnemyBillboardPose(
        settings, EnemyKind::Melee, {3.0f, 4.0f}, {3.0f, 4.0f}, 0.75f);
    context.Expect(
        NearlyEqual(coincident.yawRadians, 0.75f),
        "coincident viewer preserves the previous billboard yaw");
}

void TestAnimationSelection(TestContext& context) {
    EnemyRenderSettings settings{};
    settings.meleeAnimations.idle.frameTexturePaths = {"melee_idle.png"};
    settings.meleeAnimations.moving.frameTexturePaths = {"melee_move.png"};
    settings.rangedAnimations.attacking.frameTexturePaths = {"ranged_attack.png"};
    settings.rangedAnimations.dead.frameTexturePaths = {"ranged_dead.png"};

    context.Expect(
        GetEnemyAnimationClip(settings, EnemyKind::Melee, EnemyState::Idle)
                .frameTexturePaths.front() == "melee_idle.png",
        "melee idle state selects its reserved clip");
    context.Expect(
        GetEnemyAnimationClip(settings, EnemyKind::Melee, EnemyState::Moving)
                .frameTexturePaths.front() == "melee_move.png",
        "melee moving state selects its reserved clip");
    context.Expect(
        GetEnemyAnimationClip(settings, EnemyKind::Ranged, EnemyState::Attacking)
                .frameTexturePaths.front() == "ranged_attack.png",
        "ranged attacking state selects its reserved clip");
    context.Expect(
        GetEnemyAnimationClip(settings, EnemyKind::Ranged, EnemyState::Dead)
                .frameTexturePaths.front() == "ranged_dead.png",
        "ranged dead state selects its reserved clip");

    const EnemyAnimationClipSettings emptyClip{};
    context.Expect(
        !ResolveEnemyAnimationFrame(emptyClip, 1.0f, 0).has_value(),
        "empty animation clip selects the fallback rectangle");

    EnemyAnimationClipSettings looping{{"0", "1", "2"}, 0.1f, true};
    context.Expect(
        ResolveEnemyAnimationFrame(looping, 0.0f, 3) == 0,
        "animation starts on frame zero");
    context.Expect(
        ResolveEnemyAnimationFrame(looping, 0.35f, 3) == 0,
        "looping animation wraps its frame index");

    EnemyAnimationClipSettings clamped{{"0", "1", "2"}, 0.1f, false};
    context.Expect(
        ResolveEnemyAnimationFrame(clamped, 5.0f, 3) == 2,
        "non-looping animation holds its final frame");
}

} // namespace

void RunEnemyBillboardTests(TestContext& context) {
    TestBillboardPose(context);
    TestAnimationSelection(context);
}

} // namespace fps::tests
