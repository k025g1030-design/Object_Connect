#include "../TestSupport.hpp"

#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Gameplay/Player/PlayerCombatState.hpp"
#include "RetroFPS/Gameplay/Player/PlayerController.hpp"
#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap MakePlayerMap(TestContext& context) {
    MapLoadResult result = GridMapLoader::Parse("P.D");
    context.Expect(result.Succeeded(), "player combat test map parses");
    if (!result.map.has_value()) {
        throw std::runtime_error("player combat test map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

void TestPlayerHealth(TestContext& context) {
    PlayerCombatState combat;
    context.Expect(
        NearlyEqual(combat.GetHealth(), 100.0f) &&
            NearlyEqual(combat.GetMaximumHealth(), 100.0f) && !combat.IsDead(),
        "player combat state starts at 100 HP");

    const PlayerDamageResult partial = combat.ApplyDamage(25.5f);
    context.Expect(
        partial.applied && !partial.killed &&
            NearlyEqual(partial.appliedDamage, 25.5f) &&
            NearlyEqual(partial.remainingHealth, 74.5f),
        "finite positive player damage reduces HP");

    const float healthBeforeInvalidDamage = combat.GetHealth();
    context.Expect(
        !combat.ApplyDamage(0.0f).applied &&
            !combat.ApplyDamage(-1.0f).applied &&
            !combat.ApplyDamage((std::numeric_limits<float>::quiet_NaN)()).applied &&
            NearlyEqual(combat.GetHealth(), healthBeforeInvalidDamage),
        "invalid player damage is rejected without changing HP");

    const PlayerDamageResult lethal = combat.ApplyDamage(1000.0f);
    context.Expect(
        lethal.applied && lethal.killed &&
            NearlyEqual(lethal.appliedDamage, healthBeforeInvalidDamage) &&
            NearlyEqual(lethal.remainingHealth, 0.0f) && combat.IsDead(),
        "lethal damage clamps at zero and reports the death transition");
    context.Expect(
        !combat.ApplyDamage(1.0f).applied,
        "dead player combat state rejects repeated damage");

    combat.Reset();
    context.Expect(
        NearlyEqual(combat.GetHealth(), 100.0f) && !combat.IsDead(),
        "reset restores the player to 100 HP");
}

void TestBodyHeightValidation(TestContext& context) {
    const auto expectInvalid = [&context](
                                   const PlayerSettings& settings,
                                   const std::string_view description) {
        std::string error;
        context.Expect(!ValidatePlayerSettings(settings, error) && !error.empty(), description);
    };

    PlayerSettings settings{};
    settings.bodyHeight = 0.0f;
    expectInvalid(settings, "zero player body height is rejected");

    settings = {};
    settings.bodyHeight = 0.4f;
    expectInvalid(settings, "body height smaller than the collision diameter is rejected");

    settings = {};
    settings.eyeHeight = 1.81f;
    expectInvalid(settings, "eye height above the player body is rejected");

    std::string error;
    context.Expect(
        ValidatePlayerSettings(PlayerSettings{}, error) && error.empty(),
        "default 1.8 meter player body is valid");
}

void TestSafeVerticalRecoil(TestContext& context) {
    const GridMap map = MakePlayerMap(context);
    PlayerController controller;
    Player player;
    std::string error;
    context.Expect(
        controller.Initialize(player, map, {}, error),
        "player initializes for recoil tests");

    context.Expect(
        controller.SetVerticalRecoilDegrees(player, 5.0f),
        "finite non-negative recoil is accepted");
    const float fiveDegreesRadians = 5.0f * std::numbers::pi_v<float> / 180.0f;
    context.Expect(
        NearlyEqual(player.GetPitchRadians(), -fiveDegreesRadians) &&
            NearlyEqual(player.GetRecoilDegrees(), 5.0f),
        "positive recoil kicks the effective view upward");

    InputState lookDown{};
    lookDown.mouse.captured = true;
    lookDown.mouse.deltaY =
        (10.0f * std::numbers::pi_v<float> / 180.0f) /
        controller.GetSettings().mouseSensitivity;
    controller.Update(player, lookDown, 0.0f, map, {});
    context.Expect(
        NearlyEqual(player.GetPitchRadians(), fiveDegreesRadians),
        "mouse aim and recoil remain separate additive inputs");

    const float pitchBeforeInvalidRecoil = player.GetPitchRadians();
    context.Expect(
        !controller.SetVerticalRecoilDegrees(player, -1.0f) &&
            !controller.SetVerticalRecoilDegrees(
                player, (std::numeric_limits<float>::quiet_NaN)()) &&
            NearlyEqual(player.GetPitchRadians(), pitchBeforeInvalidRecoil),
        "invalid recoil is rejected without changing the view");

    context.Expect(
        controller.SetVerticalRecoilDegrees(player, 1000.0f),
        "large finite recoil is safely accepted");
    const float maximumPitchRadians =
        controller.GetSettings().maxPitchDegrees * std::numbers::pi_v<float> / 180.0f;
    context.Expect(
        NearlyEqual(player.GetPitchRadians(), -maximumPitchRadians),
        "large recoil is clamped to the configured pitch limit");

    controller.ClearVerticalRecoil(player);
    context.Expect(
        NearlyEqual(
            player.GetPitchRadians(),
            10.0f * std::numbers::pi_v<float> / 180.0f) &&
            NearlyEqual(player.GetRecoilDegrees(), 0.0f),
        "clearing recoil restores the underlying mouse aim");
}

} // namespace

void RunPlayerCombatStateTests(TestContext& context) {
    TestPlayerHealth(context);
    TestBodyHeightValidation(context);
    TestSafeVerticalRecoil(context);
}

} // namespace fps::tests
