#include "RetroFPS/Gameplay/Player/PlayerController.hpp"

#include "RetroFPS/Collision/GridCollision.hpp"
#include "RetroFPS/Gameplay/Player/PlanarMovement.hpp"
#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/World/GridMap.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>
#include <utility>

namespace fps {
namespace {

constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0f;

[[nodiscard]] float ToAxis(const bool positive, const bool negative) noexcept {
    return static_cast<float>(positive) - static_cast<float>(negative);
}

} // namespace

bool PlayerController::Configure(
    PlayerSettings settings, std::string& error) {
    if (!ValidatePlayerSettings(settings, error)) {
        return false;
    }

    settings_ = std::move(settings);
    return true;
}

bool PlayerController::Initialize(
    Player& player,
    const GridMap& map,
    const WorldSettings& worldSettings,
    std::string& error) const {
    error.clear();

    std::string settingsError;
    if (!ValidatePlayerSettings(settings_, settingsError)) {
        error = "Invalid player settings: ";
        error += settingsError;
        return false;
    }

    try {
        const Float2 spawnPosition = map.GetSpawnPosition(worldSettings.cellSize);
        if (GridCollision::OverlapsSolid(
                map,
                spawnPosition,
                settings_.collisionRadius,
                worldSettings.cellSize)) {
            error = "Player spawn overlaps a solid map cell.";
            return false;
        }

        player.Reset(spawnPosition, 0.0f, 0.0f);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize the player: ";
        error += exception.what();
        return false;
    } catch (...) {
        error = "Failed to initialize the player because of an unknown error.";
        return false;
    }
}

void PlayerController::Update(
    Player& player,
    const InputState& input,
    const float deltaSeconds,
    const GridMap& map,
    const WorldSettings& worldSettings,
    const std::span<const CircleObstacle> dynamicBlockers) const {
    float yawRadians = player.GetYawRadians();
    float pitchRadians = player.GetAimPitchRadians();
    const float recoilDegrees = player.GetRecoilDegrees();

    if (input.mouse.captured) {
        if (std::isfinite(input.mouse.deltaX)) {
            yawRadians += input.mouse.deltaX * settings_.mouseSensitivity;
            yawRadians = std::remainder(
                yawRadians, 2.0f * std::numbers::pi_v<float>);
        }
        if (std::isfinite(input.mouse.deltaY)) {
            const float maxPitchRadians =
                settings_.maxPitchDegrees * kDegreesToRadians;
            pitchRadians = std::clamp(
                pitchRadians + input.mouse.deltaY * settings_.mouseSensitivity,
                -maxPitchRadians,
                maxPitchRadians);
        }
        player.SetLookAngles(yawRadians, pitchRadians);
        static_cast<void>(SetVerticalRecoilDegrees(player, recoilDegrees));
    }

    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return;
    }

    const float forwardAxis = ToAxis(input.keyboard.w, input.keyboard.s);
    const float rightAxis = ToAxis(input.keyboard.d, input.keyboard.a);
    const Float2 direction =
        ComputePlanarInput(forwardAxis, rightAxis, yawRadians, pitchRadians);
    const Float2 displacement{
        direction.x * settings_.movementSpeed * deltaSeconds,
        direction.z * settings_.movementSpeed * deltaSeconds,
    };

    player.SetPositionXZ(GridCollision::MoveCircle(
        map,
        player.GetPositionXZ(),
        displacement,
        settings_.collisionRadius,
        dynamicBlockers,
        worldSettings.cellSize));
}

bool PlayerController::SetVerticalRecoilDegrees(
    Player& player, const float recoilDegrees) const noexcept {
    if (!std::isfinite(recoilDegrees) || recoilDegrees < 0.0f) {
        return false;
    }

    const float maximumPitchRadians = settings_.maxPitchDegrees * kDegreesToRadians;
    const float requestedRecoilRadians = -recoilDegrees * kDegreesToRadians;
    const float aimPitchRadians = player.GetAimPitchRadians();
    const float effectivePitchRadians = std::clamp(
        aimPitchRadians + requestedRecoilRadians,
        -maximumPitchRadians,
        maximumPitchRadians);
    player.SetRecoilPitchRadians(effectivePitchRadians - aimPitchRadians);
    return true;
}

void PlayerController::ClearVerticalRecoil(Player& player) const noexcept {
    player.SetRecoilPitchRadians(0.0f);
}

} // namespace fps
