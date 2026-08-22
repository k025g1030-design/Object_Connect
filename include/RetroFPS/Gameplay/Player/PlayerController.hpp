#pragma once

#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"

#include <string>

namespace fps {

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
        const WorldSettings& worldSettings) const;

    [[nodiscard]] const PlayerSettings& GetSettings() const noexcept { return settings_; }

private:
    PlayerSettings settings_{};
};

} // namespace fps
