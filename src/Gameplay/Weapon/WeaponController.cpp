#include "RetroFPS/Gameplay/Weapon/WeaponController.hpp"

#include "RetroFPS/Input/InputState.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fps {
namespace {

[[nodiscard]] bool ValidateDefinition(
    const WeaponDefinition& definition, std::string& error) {
    if (definition.id.empty()) {
        error = "weapon ID must not be empty";
        return false;
    }
    if (!std::isfinite(definition.damage) || definition.damage <= 0.0f) {
        error = "weapon damage must be finite and greater than zero";
        return false;
    }
    if (definition.magazineCapacity == 0) {
        error = "weapon magazine capacity must be greater than zero";
        return false;
    }
    if (!std::isfinite(definition.recoilDegrees) || definition.recoilDegrees < 0.0f) {
        error = "weapon recoil must be finite and non-negative";
        return false;
    }
    if (!std::isfinite(definition.fireIntervalSeconds) ||
        definition.fireIntervalSeconds <= 0.0f) {
        error = "weapon fire interval must be finite and greater than zero";
        return false;
    }
    if (!std::isfinite(definition.reloadSeconds) || definition.reloadSeconds <= 0.0f) {
        error = "weapon reload duration must be finite and greater than zero";
        return false;
    }
    return true;
}

} // namespace

bool ValidateWeaponControllerSettings(
    const WeaponControllerSettings& settings, std::string& error) {
    error.clear();
    if (!std::isfinite(settings.recoilRecoveryDegreesPerSecond) ||
        settings.recoilRecoveryDegreesPerSecond <= 0.0f) {
        error = "weapon recoil recovery must be finite and greater than zero";
        return false;
    }
    if (!std::isfinite(settings.maximumAccumulatedRecoilDegrees) ||
        settings.maximumAccumulatedRecoilDegrees <= 0.0f) {
        error = "weapon maximum accumulated recoil must be finite and greater than zero";
        return false;
    }
    return true;
}

bool WeaponController::Configure(
    WeaponDefinition definition,
    WeaponControllerSettings settings,
    std::string& error) {
    error.clear();
    if (!ValidateDefinition(definition, error) ||
        !ValidateWeaponControllerSettings(settings, error)) {
        return false;
    }

    definition_ = std::move(definition);
    settings_ = settings;
    configured_ = true;
    shotEvents_.clear();
    shotEvents_.reserve(1);
    return true;
}

bool WeaponController::Configure(
    WeaponDefinition definition, std::string& error) {
    return Configure(std::move(definition), WeaponControllerSettings{}, error);
}

bool WeaponController::Initialize(
    WeaponState& state, std::string& error) {
    error.clear();
    if (!configured_) {
        error = "weapon controller must be configured before state initialization";
        return false;
    }

    state = WeaponState{};
    state.weaponId_ = definition_.id;
    state.magazineAmmo_ = definition_.magazineCapacity;
    state.reserveAmmo_ = definition_.reserveAmmo;
    state.initialized_ = true;
    shotEvents_.clear();
    return true;
}

void WeaponController::ResetVisualFeedback(WeaponState& state) const noexcept {
    if (configured_ && state.initialized_ && state.weaponId_ == definition_.id) {
        state.recoilDegrees_ = 0.0f;
    }
}

void WeaponController::Update(
    WeaponState& state, const InputState& input, const float deltaSeconds) {
    shotEvents_.clear();
    if (!configured_ || !state.initialized_ || state.weaponId_ != definition_.id ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
        return;
    }

    state.fireCooldownSeconds_ =
        (std::max)(0.0f, state.fireCooldownSeconds_ - deltaSeconds);
    state.recoilDegrees_ = (std::max)(
        0.0f,
        state.recoilDegrees_ -
            settings_.recoilRecoveryDegreesPerSecond * deltaSeconds);

    if (state.reloading_) {
        state.reloadElapsedSeconds_ = (std::min)(
            definition_.reloadSeconds,
            state.reloadElapsedSeconds_ + deltaSeconds);
        if (state.reloadElapsedSeconds_ >= definition_.reloadSeconds) {
            const std::uint32_t missingAmmo =
                definition_.magazineCapacity - state.magazineAmmo_;
            const std::uint32_t transferredAmmo =
                (std::min)(missingAmmo, state.reserveAmmo_);
            state.magazineAmmo_ += transferredAmmo;
            state.reserveAmmo_ -= transferredAmmo;
            state.reloadElapsedSeconds_ = 0.0f;
            state.reloading_ = false;
        }
        return;
    }

    if (input.keyboard.rPressed &&
        state.magazineAmmo_ < definition_.magazineCapacity &&
        state.reserveAmmo_ > 0) {
        state.reloading_ = true;
        state.reloadElapsedSeconds_ = 0.0f;
        return;
    }

    const bool fireRequested = definition_.automatic
                                   ? input.mouse.leftHeld
                                   : input.mouse.leftPressed;
    if (!fireRequested || state.fireCooldownSeconds_ > 0.0f ||
        state.magazineAmmo_ == 0) {
        return;
    }

    --state.magazineAmmo_;
    state.fireCooldownSeconds_ = definition_.fireIntervalSeconds;
    state.recoilDegrees_ = (std::min)(
        settings_.maximumAccumulatedRecoilDegrees,
        state.recoilDegrees_ + definition_.recoilDegrees);
    shotEvents_.push_back({
        definition_.id,
        definition_.damage,
        definition_.recoilDegrees,
        state.magazineAmmo_,
    });
}

WeaponHudSnapshot WeaponController::MakeHudSnapshot(
    const WeaponState& state) const {
    if (!configured_ || !state.initialized_ || state.weaponId_ != definition_.id) {
        return {};
    }

    const float reloadProgress = state.reloading_
                                     ? std::clamp(
                                           state.reloadElapsedSeconds_ /
                                               definition_.reloadSeconds,
                                           0.0f,
                                           1.0f)
                                     : 0.0f;
    return {
        state.weaponId_,
        state.magazineAmmo_,
        state.reserveAmmo_,
        state.reloading_,
        reloadProgress,
        state.recoilDegrees_,
        std::clamp(
            state.recoilDegrees_ /
                settings_.maximumAccumulatedRecoilDegrees,
            0.0f,
            1.0f),
    };
}

} // namespace fps
