#include "../TestSupport.hpp"

#include "RetroFPS/Collision/GridCollision.hpp"
#include "RetroFPS/Gameplay/Player/PlanarMovement.hpp"
#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Gameplay/Player/PlayerController.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseValidMap(
    TestContext& context, const std::string_view text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "valid gameplay map should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error("valid gameplay test map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

void ExpectPlayerInitialized(
    TestContext& context,
    const PlayerController& controller,
    Player& player,
    const GridMap& map,
    const WorldSettings& settings) {
    std::string error;
    const bool initialized = controller.Initialize(player, map, settings, error);
    context.Expect(initialized, "PlayerController initializes a player on a valid spawn");
    context.Expect(error.empty(), "successful player initialization has no error");
    if (!initialized) {
        throw std::runtime_error("player initialization failed: " + error);
    }
}

[[nodiscard]] GridMap MakeOpenMovementMap(TestContext& context) {
    return ParseValidMap(
        context,
        "#########\n"
        "#D......#\n"
        "#.......#\n"
        "#...P...#\n"
        "#.......#\n"
        "#.......#\n"
        "#########");
}

void TestPlanarMovement(TestContext& context) {
    const Float2 forward = ComputePlanarInput(1.0f, 0.0f, 0.0f);
    context.Expect(
        NearlyEqual(forward.x, 0.0f) && NearlyEqual(forward.z, 1.0f),
        "yaw zero faces +Z");

    const Float2 right = ComputePlanarInput(0.0f, 1.0f, 0.0f);
    context.Expect(
        NearlyEqual(right.x, 1.0f) && NearlyEqual(right.z, 0.0f),
        "right input faces +X");

    const Float2 turned =
        ComputePlanarInput(1.0f, 0.0f, std::numbers::pi_v<float> * 0.5f);
    context.Expect(
        NearlyEqual(turned.x, 1.0f) && NearlyEqual(turned.z, 0.0f),
        "yaw rotates planar input");

    const Float2 diagonal = ComputePlanarInput(1.0f, 1.0f, 0.0f);
    context.Expect(
        NearlyEqual(std::hypot(diagonal.x, diagonal.z), 1.0f),
        "diagonal planar input is normalized");

    const Float2 pitchedUp = ComputePlanarInput(1.0f, 0.5f, 0.7f, 1.2f);
    const Float2 pitchedDown = ComputePlanarInput(1.0f, 0.5f, 0.7f, -1.2f);
    context.Expect(
        NearlyEqual(pitchedUp.x, pitchedDown.x) &&
            NearlyEqual(pitchedUp.z, pitchedDown.z),
        "pitch does not affect planar movement");
}

void TestRawInputAndWasd(TestContext& context) {
    const GridMap map = MakeOpenMovementMap(context);
    const WorldSettings worldSettings{};
    PlayerController controller;
    std::string configureError;
    context.Expect(
        controller.Configure({}, configureError),
        "default PlayerSettings configure successfully");
    context.Expect(configureError.empty(), "valid PlayerSettings leave no configuration error");

    const Float2 spawn = map.GetSpawnPosition(worldSettings.cellSize);
    const auto runInput = [&](const InputState& input) {
        Player player;
        ExpectPlayerInitialized(context, controller, player, map, worldSettings);
        controller.Update(player, input, 0.1f, map, worldSettings);
        return player.GetPositionXZ();
    };

    InputState input{};
    input.keyboard.w = true;
    const Float2 forward = runInput(input);
    context.Expect(NearlyEqual(forward.x, spawn.x), "W does not strafe on yaw zero");
    context.Expect(NearlyEqual(forward.z, spawn.z + 0.3f), "W moves toward +Z");

    input = {};
    input.keyboard.s = true;
    const Float2 backward = runInput(input);
    context.Expect(NearlyEqual(backward.z, spawn.z - 0.3f), "S moves toward -Z");

    input = {};
    input.keyboard.a = true;
    const Float2 left = runInput(input);
    context.Expect(NearlyEqual(left.x, spawn.x - 0.3f), "A moves toward -X");

    input = {};
    input.keyboard.d = true;
    const Float2 right = runInput(input);
    context.Expect(NearlyEqual(right.x, spawn.x + 0.3f), "D moves toward +X");

    input = {};
    input.keyboard.w = true;
    input.keyboard.d = true;
    const Float2 diagonal = runInput(input);
    context.Expect(
        NearlyEqual(std::hypot(diagonal.x - spawn.x, diagonal.z - spawn.z), 0.3f),
        "raw diagonal WASD input keeps configured movement speed");
}

void TestYawAndPitch(TestContext& context) {
    const GridMap map = MakeOpenMovementMap(context);
    const WorldSettings worldSettings{};
    PlayerController controller;
    const PlayerSettings settings = controller.GetSettings();

    Player turnedPlayer;
    ExpectPlayerInitialized(context, controller, turnedPlayer, map, worldSettings);
    const Float2 spawn = turnedPlayer.GetPositionXZ();

    InputState lookRight{};
    lookRight.mouse.captured = true;
    lookRight.mouse.deltaX =
        (std::numbers::pi_v<float> * 0.5f) / settings.mouseSensitivity;
    controller.Update(turnedPlayer, lookRight, 0.0f, map, worldSettings);
    context.Expect(
        NearlyEqual(turnedPlayer.GetYawRadians(), std::numbers::pi_v<float> * 0.5f),
        "captured raw mouse X updates yaw");
    context.Expect(
        NearlyEqual(turnedPlayer.GetPositionXZ().x, spawn.x) &&
            NearlyEqual(turnedPlayer.GetPositionXZ().z, spawn.z),
        "zero delta time applies look without movement");

    InputState forward{};
    forward.keyboard.w = true;
    controller.Update(turnedPlayer, forward, 0.1f, map, worldSettings);
    context.Expect(
        NearlyEqual(turnedPlayer.GetPositionXZ().x, spawn.x + 0.3f),
        "yaw rotates W movement toward +X");
    context.Expect(
        NearlyEqual(turnedPlayer.GetPositionXZ().z, spawn.z),
        "yaw-rotated W movement has no +Z component at ninety degrees");

    Player pitchedUp;
    Player pitchedDown;
    ExpectPlayerInitialized(context, controller, pitchedUp, map, worldSettings);
    ExpectPlayerInitialized(context, controller, pitchedDown, map, worldSettings);

    InputState lookUp{};
    lookUp.mouse.captured = true;
    lookUp.mouse.deltaY = 100000.0f;
    controller.Update(pitchedUp, lookUp, 0.0f, map, worldSettings);

    InputState lookDown{};
    lookDown.mouse.captured = true;
    lookDown.mouse.deltaY = -100000.0f;
    controller.Update(pitchedDown, lookDown, 0.0f, map, worldSettings);

    const float maximumPitch =
        settings.maxPitchDegrees * std::numbers::pi_v<float> / 180.0f;
    context.Expect(
        NearlyEqual(pitchedUp.GetPitchRadians(), maximumPitch),
        "positive mouse Y is clamped to maximum pitch");
    context.Expect(
        NearlyEqual(pitchedDown.GetPitchRadians(), -maximumPitch),
        "negative mouse Y is clamped to minimum pitch");

    controller.Update(pitchedUp, forward, 0.1f, map, worldSettings);
    controller.Update(pitchedDown, forward, 0.1f, map, worldSettings);
    context.Expect(
        NearlyEqual(pitchedUp.GetPositionXZ().x, pitchedDown.GetPositionXZ().x) &&
            NearlyEqual(pitchedUp.GetPositionXZ().z, pitchedDown.GetPositionXZ().z),
        "opposite pitch values do not change horizontal movement");
}

void TestPlayerCollision(TestContext& context) {
    const GridMap map = ParseValidMap(
        context,
        "#####\n"
        "#P..#\n"
        "##..#\n"
        "#..D#\n"
        "#####");
    const WorldSettings worldSettings{};
    PlayerController controller;
    Player player;
    ExpectPlayerInitialized(context, controller, player, map, worldSettings);

    InputState input{};
    input.keyboard.w = true;
    controller.Update(player, input, 1.0f, map, worldSettings);

    const Float2 position = player.GetPositionXZ();
    context.Expect(position.z <= 1.7501f, "PlayerController stops movement at a wall");
    context.Expect(
        !GridCollision::OverlapsSolid(
            map,
            position,
            controller.GetSettings().collisionRadius,
            worldSettings.cellSize),
        "PlayerController collision result never penetrates a solid cell");
}

void TestInvalidPlayerSettings(TestContext& context) {
    const auto expectInvalid = [&context](
                                   const PlayerSettings settings,
                                   const std::string_view description) {
        PlayerController controller;
        std::string error = "stale";
        context.Expect(!controller.Configure(settings, error), description);
        context.Expect(!error.empty(), "invalid PlayerSettings produce an error message");
    };

    PlayerSettings settings{};
    settings.eyeHeight = -1.0f;
    expectInvalid(settings, "controller rejects negative eye height");

    settings = {};
    settings.collisionRadius = 0.0f;
    expectInvalid(settings, "controller rejects zero collision radius");

    settings = {};
    settings.movementSpeed = -1.0f;
    expectInvalid(settings, "controller rejects negative movement speed");

    settings = {};
    settings.mouseSensitivity = -1.0f;
    expectInvalid(settings, "controller rejects negative mouse sensitivity");

    settings = {};
    settings.maxPitchDegrees = 90.0f;
    expectInvalid(settings, "controller rejects a ninety-degree pitch limit");

    settings = {};
    settings.movementSpeed = (std::numeric_limits<float>::quiet_NaN)();
    expectInvalid(settings, "controller rejects non-finite settings");

    PlayerController largePlayerController;
    settings = {};
    settings.collisionRadius = 0.6f;
    std::string error;
    context.Expect(
        largePlayerController.Configure(settings, error),
        "large finite collision radius is a valid setting by itself");

    const GridMap map = ParseValidMap(context, "PD");
    Player player;
    context.Expect(
        !largePlayerController.Initialize(player, map, {}, error),
        "controller rejects a configured player that overlaps the spawn walls");
    context.Expect(!error.empty(), "spawn collision initialization reports an error");
}

} // namespace

void RunPlayerControllerTests(TestContext& context) {
    TestPlanarMovement(context);
    TestRawInputAndWasd(context);
    TestYawAndPitch(context);
    TestPlayerCollision(context);
    TestInvalidPlayerSettings(context);
}

} // namespace fps::tests
