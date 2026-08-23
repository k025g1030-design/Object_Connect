#pragma once

#include "RetroFPS/Gameplay/Weapon/WeaponState.hpp"

#include <span>
#include <string>
#include <vector>

namespace fps {

struct InputState;

[[nodiscard]] bool ValidateWeaponControllerSettings(
    const WeaponControllerSettings& settings, std::string& error);

class WeaponController final {
public:
    WeaponController() = default;

    [[nodiscard]] bool Configure(
        WeaponDefinition definition,
        WeaponControllerSettings settings,
        std::string& error);
    [[nodiscard]] bool Configure(WeaponDefinition definition, std::string& error);
    [[nodiscard]] bool Initialize(WeaponState& state, std::string& error);
    // Clears camera/HUD kick without changing ammo, reload, or fire cooldown.
    void ResetVisualFeedback(WeaponState& state) const noexcept;

    // Input is sampled only when the caller advances gameplay. Not calling
    // Update while paused freezes reload, cooldown, and recoil recovery.
    void Update(WeaponState& state, const InputState& input, float deltaSeconds);

    [[nodiscard]] std::span<const ShotEvent> GetShotEvents() const noexcept {
        return shotEvents_;
    }
    [[nodiscard]] WeaponHudSnapshot MakeHudSnapshot(
        const WeaponState& state) const;
    [[nodiscard]] const WeaponDefinition& GetDefinition() const noexcept {
        return definition_;
    }
    [[nodiscard]] const WeaponControllerSettings& GetSettings() const noexcept {
        return settings_;
    }
    [[nodiscard]] bool IsConfigured() const noexcept { return configured_; }

private:
    WeaponDefinition definition_{};
    WeaponControllerSettings settings_{};
    bool configured_ = false;
    std::vector<ShotEvent> shotEvents_;
};

} // namespace fps
