#pragma once

#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"

#include <span>
#include <string>

namespace fps {

struct CircleObstacle;
class GridMap;
class Player;
struct InputState;
struct WorldSettings;

class PlayerController final {
public:
    PlayerController() noexcept = default;

    [[nodiscard]] bool Configure(PlayerSettings settings, std::string& error);
    [[nodiscard]] bool Initialize(
        Player& player,
        const GridMap& map,
        const WorldSettings& worldSettings,
        std::string& error) const;
    void Update(
        Player& player,
        const InputState& input,
        float deltaSeconds,
        const GridMap& map,
        const WorldSettings& worldSettings,
        std::span<const CircleObstacle> dynamicBlockers = {}) const;

    // WeaponState owns the temporal recoil/recovery curve. Call this once per
    // simulated gameplay frame with its current non-negative recoil amount.
    // Positive degrees kick the view upward; the result is clamped to the
    // configured pitch limit without changing the player's underlying aim.
    [[nodiscard]] bool SetVerticalRecoilDegrees(
        Player& player, float recoilDegrees) const noexcept;
    void ClearVerticalRecoil(Player& player) const noexcept;

    [[nodiscard]] const PlayerSettings& GetSettings() const noexcept { return settings_; }

private:
    PlayerSettings settings_{};
};

} // namespace fps
