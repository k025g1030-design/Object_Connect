#include "../TestSupport.hpp"

#include "RetroFPS/Data/GameData.hpp"
#include "RetroFPS/Gameplay/Weapon/WeaponController.hpp"
#include "RetroFPS/Gameplay/Weapon/WeaponState.hpp"
#include "RetroFPS/Input/InputState.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace fps::tests {
namespace {

[[nodiscard]] WeaponDefinition MakeDefinition(const bool automatic = false) {
    return {
        "test_weapon",
        12.5f,
        5,
        7,
        2.0f,
        automatic,
        "",
        0.10f,
        1.0f,
    };
}

void ConfigureAndInitialize(
    TestContext& context,
    WeaponController& controller,
    WeaponState& state,
    WeaponDefinition definition,
    const WeaponControllerSettings settings = {}) {
    std::string error;
    const bool configured = controller.Configure(definition, settings, error);
    context.Expect(configured && error.empty(), "valid weapon definition configures");
    if (!configured) {
        throw std::runtime_error("weapon test configuration failed: " + error);
    }

    const bool initialized = controller.Initialize(state, error);
    context.Expect(initialized && error.empty(), "configured weapon state initializes");
    if (!initialized) {
        throw std::runtime_error("weapon test state initialization failed: " + error);
    }
}

void TestInitialStateAndSemiAutomaticFire(TestContext& context) {
    WeaponController controller;
    WeaponState state;
    ConfigureAndInitialize(context, controller, state, MakeDefinition());

    const WeaponHudSnapshot initial = controller.MakeHudSnapshot(state);
    context.Expect(
        state.IsInitialized() && state.GetWeaponId() == "test_weapon" &&
            initial.magazineAmmo == 5 && initial.reserveAmmo == 7 &&
            !initial.reloading && NearlyEqual(initial.reloadProgress, 0.0f) &&
            NearlyEqual(
                controller.GetSettings().recoilRecoveryDegreesPerSecond,
                8.0f),
        "weapon state starts with configured magazine and reserve ammo");

    InputState heldOnly{};
    heldOnly.mouse.leftHeld = true;
    controller.Update(state, heldOnly, 0.0f);
    context.Expect(
        controller.GetShotEvents().empty() && state.GetMagazineAmmo() == 5,
        "semi-automatic weapon ignores a held button without a press edge");

    InputState pressed{};
    pressed.mouse.leftPressed = true;
    controller.Update(state, pressed, 0.0f);
    const std::span<const ShotEvent> firstShot = controller.GetShotEvents();
    context.Expect(
        firstShot.size() == 1 && firstShot[0].weaponId == "test_weapon" &&
            NearlyEqual(firstShot[0].damage, 12.5f) &&
            NearlyEqual(firstShot[0].recoilDegrees, 2.0f) &&
            firstShot[0].magazineAmmoAfterShot == 4 &&
            state.GetMagazineAmmo() == 4,
        "semi-automatic press emits one shot event and consumes one round");

    controller.Update(state, pressed, 0.05f);
    context.Expect(
        controller.GetShotEvents().empty() && state.GetMagazineAmmo() == 4,
        "fire interval blocks an early repeated semi-automatic press");

    controller.Update(state, {}, 0.05f);
    controller.Update(state, pressed, 0.0f);
    context.Expect(
        controller.GetShotEvents().size() == 1 && state.GetMagazineAmmo() == 3,
        "semi-automatic weapon fires again after its interval expires");

    std::string resetError;
    context.Expect(
        controller.Initialize(state, resetError) &&
            controller.GetShotEvents().empty() && state.GetMagazineAmmo() == 5 &&
            state.GetReserveAmmo() == 7,
        "state reinitialization resets ammo, timers, recoil, and stale shot events");
}

void TestAutomaticFireAndNoAutomaticReload(TestContext& context) {
    WeaponDefinition definition = MakeDefinition(true);
    definition.magazineCapacity = 2;
    definition.reserveAmmo = 4;

    WeaponController controller;
    WeaponState state;
    ConfigureAndInitialize(context, controller, state, definition);

    InputState pressedOnly{};
    pressedOnly.mouse.leftPressed = true;
    controller.Update(state, pressedOnly, 0.0f);
    context.Expect(
        controller.GetShotEvents().empty(),
        "automatic weapon uses held input rather than the press edge alone");

    InputState held{};
    held.mouse.leftHeld = true;
    controller.Update(state, held, 0.0f);
    context.Expect(
        controller.GetShotEvents().size() == 1 && state.GetMagazineAmmo() == 1,
        "automatic weapon fires immediately while held");

    controller.Update(state, held, 0.05f);
    context.Expect(
        controller.GetShotEvents().empty() && state.GetMagazineAmmo() == 1,
        "automatic weapon respects its fire interval while held");
    controller.Update(state, held, 0.05f);
    context.Expect(
        controller.GetShotEvents().size() == 1 && state.GetMagazineAmmo() == 0,
        "automatic weapon repeats when the interval expires");

    controller.Update(state, held, 1.0f);
    context.Expect(
        controller.GetShotEvents().empty() && !state.IsReloading() &&
            state.GetMagazineAmmo() == 0 && state.GetReserveAmmo() == 4,
        "empty automatic weapon does not start an implicit reload");
}

void TestManualReloadAndAmmoTransfer(TestContext& context) {
    WeaponController controller;
    WeaponState state;
    ConfigureAndInitialize(context, controller, state, MakeDefinition());

    InputState fire{};
    fire.mouse.leftPressed = true;
    controller.Update(state, fire, 0.0f);

    InputState reload{};
    reload.keyboard.rPressed = true;
    controller.Update(state, reload, 0.0f);
    context.Expect(
        state.IsReloading() && state.GetMagazineAmmo() == 4 &&
            state.GetReserveAmmo() == 7,
        "R starts a reload without transferring ammo immediately");

    InputState fireDuringReload = fire;
    controller.Update(state, fireDuringReload, 0.4f);
    const WeaponHudSnapshot halfway = controller.MakeHudSnapshot(state);
    context.Expect(
        controller.GetShotEvents().empty() && halfway.reloading &&
            NearlyEqual(halfway.reloadProgress, 0.4f) &&
            halfway.magazineAmmo == 4 && halfway.reserveAmmo == 7,
        "reload progress advances and blocks firing");

    // A paused caller performs no Update; read-only snapshots do not advance
    // any timers or transfer ammunition.
    const WeaponHudSnapshot pausedAgain = controller.MakeHudSnapshot(state);
    context.Expect(
        NearlyEqual(pausedAgain.reloadProgress, halfway.reloadProgress) &&
            pausedAgain.magazineAmmo == halfway.magazineAmmo,
        "state remains frozen when the caller does not update gameplay");

    controller.Update(state, fireDuringReload, 0.6f);
    context.Expect(
        controller.GetShotEvents().empty() && !state.IsReloading() &&
            state.GetMagazineAmmo() == 5 && state.GetReserveAmmo() == 6,
        "completed reload transfers only the missing magazine rounds");

    controller.Update(state, reload, 0.0f);
    context.Expect(
        !state.IsReloading(),
        "R does not reload an already full magazine");

    WeaponDefinition limitedDefinition = MakeDefinition(true);
    limitedDefinition.reserveAmmo = 2;
    WeaponController limitedController;
    WeaponState limitedState;
    ConfigureAndInitialize(
        context, limitedController, limitedState, limitedDefinition);

    InputState held{};
    held.mouse.leftHeld = true;
    for (std::uint32_t shot = 0; shot < limitedDefinition.magazineCapacity; ++shot) {
        limitedController.Update(
            limitedState,
            held,
            shot == 0 ? 0.0f : limitedDefinition.fireIntervalSeconds);
    }
    limitedController.Update(limitedState, reload, 0.0f);
    limitedController.Update(limitedState, {}, limitedDefinition.reloadSeconds);
    context.Expect(
        limitedState.GetMagazineAmmo() == 2 &&
            limitedState.GetReserveAmmo() == 0 && !limitedState.IsReloading(),
        "reload transfers all remaining reserve when it cannot fill the magazine");
}

void TestRecoilAccumulationAndRecovery(TestContext& context) {
    WeaponControllerSettings settings{};
    settings.recoilRecoveryDegreesPerSecond = 10.0f;
    settings.maximumAccumulatedRecoilDegrees = 3.0f;

    WeaponController controller;
    WeaponState state;
    ConfigureAndInitialize(context, controller, state, MakeDefinition(), settings);

    InputState fire{};
    fire.mouse.leftPressed = true;
    controller.Update(state, fire, 0.0f);
    const WeaponHudSnapshot expanded = controller.MakeHudSnapshot(state);
    context.Expect(
        NearlyEqual(expanded.recoilDegrees, 2.0f) &&
            NearlyEqual(expanded.crosshairExpansion, 2.0f / 3.0f),
        "shot recoil expands the HUD crosshair snapshot");

    controller.Update(state, {}, 0.1f);
    const WeaponHudSnapshot recovering = controller.MakeHudSnapshot(state);
    context.Expect(
        NearlyEqual(recovering.recoilDegrees, 1.0f) &&
            recovering.crosshairExpansion < expanded.crosshairExpansion,
        "recoil and crosshair expansion recover while shooting stops");

    controller.Update(state, {}, 1.0f);
    const WeaponHudSnapshot recovered = controller.MakeHudSnapshot(state);
    context.Expect(
        NearlyEqual(recovered.recoilDegrees, 0.0f) &&
            NearlyEqual(recovered.crosshairExpansion, 0.0f),
        "recoil recovery clamps cleanly at rest");
}

void TestRoomTransitionVisualReset(TestContext& context) {
    WeaponController controller;
    WeaponState state;
    ConfigureAndInitialize(context, controller, state, MakeDefinition());

    InputState fire{};
    fire.mouse.leftPressed = true;
    controller.Update(state, fire, 0.0f);
    InputState reload{};
    reload.keyboard.rPressed = true;
    controller.Update(state, reload, 0.0f);
    controller.ResetVisualFeedback(state);

    context.Expect(
        NearlyEqual(state.GetRecoilDegrees(), 0.0f) && state.IsReloading() &&
            NearlyEqual(state.GetFireCooldownSeconds(), 0.1f) &&
            state.GetMagazineAmmo() == 4 && state.GetReserveAmmo() == 7,
        "room transition clears visual recoil while preserving ammo, reload, and cooldown");
}

void TestValidationAndInvalidDelta(TestContext& context) {
    WeaponState state;
    WeaponController unconfigured;
    std::string error;
    context.Expect(
        !unconfigured.Initialize(state, error) && !error.empty(),
        "unconfigured weapon controller cannot initialize state");

    const auto expectInvalidDefinition = [&context](
                                             WeaponDefinition definition,
                                             const std::string_view description) {
        WeaponController controller;
        std::string configureError;
        context.Expect(
            !controller.Configure(definition, configureError) &&
                !configureError.empty(),
            description);
    };

    WeaponDefinition definition = MakeDefinition();
    definition.id.clear();
    expectInvalidDefinition(definition, "empty weapon ID is rejected");
    definition = MakeDefinition();
    definition.damage = 0.0f;
    expectInvalidDefinition(definition, "non-positive weapon damage is rejected");
    definition = MakeDefinition();
    definition.magazineCapacity = 0;
    expectInvalidDefinition(definition, "zero magazine capacity is rejected");
    definition = MakeDefinition();
    definition.recoilDegrees = -1.0f;
    expectInvalidDefinition(definition, "negative weapon recoil is rejected");
    definition = MakeDefinition();
    definition.fireIntervalSeconds = 0.0f;
    expectInvalidDefinition(definition, "zero fire interval is rejected");
    definition = MakeDefinition();
    definition.reloadSeconds = (std::numeric_limits<float>::quiet_NaN)();
    expectInvalidDefinition(definition, "non-finite reload duration is rejected");

    WeaponControllerSettings invalidSettings{};
    invalidSettings.recoilRecoveryDegreesPerSecond = 0.0f;
    context.Expect(
        !ValidateWeaponControllerSettings(invalidSettings, error) && !error.empty(),
        "zero recoil recovery is rejected");

    WeaponController controller;
    ConfigureAndInitialize(context, controller, state, MakeDefinition());
    InputState reload{};
    reload.keyboard.rPressed = true;
    InputState fire{};
    fire.mouse.leftPressed = true;
    controller.Update(state, fire, 0.0f);
    controller.Update(state, reload, 0.0f);
    const WeaponHudSnapshot beforeInvalidDelta = controller.MakeHudSnapshot(state);
    controller.Update(
        state, {}, (std::numeric_limits<float>::quiet_NaN)());
    const WeaponHudSnapshot afterInvalidDelta = controller.MakeHudSnapshot(state);
    context.Expect(
        NearlyEqual(
            beforeInvalidDelta.reloadProgress,
            afterInvalidDelta.reloadProgress) &&
            beforeInvalidDelta.magazineAmmo == afterInvalidDelta.magazineAmmo &&
            afterInvalidDelta.reloading && controller.GetShotEvents().empty(),
        "invalid delta clears frame events but freezes weapon state");
}

} // namespace

void RunWeaponControllerTests(TestContext& context) {
    TestInitialStateAndSemiAutomaticFire(context);
    TestAutomaticFireAndNoAutomaticReload(context);
    TestManualReloadAndAmmoTransfer(context);
    TestRecoilAccumulationAndRecovery(context);
    TestRoomTransitionVisualReset(context);
    TestValidationAndInvalidDelta(context);
}

} // namespace fps::tests
