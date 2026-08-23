#pragma once

#include <string>

namespace fps {

struct PlayerSettings final {
    float eyeHeight = 1.6f;
    float bodyHeight = 1.8f;
    float collisionRadius = 0.25f;
    float movementSpeed = 3.0f;
    float mouseSensitivity = 0.0025f;
    float maxPitchDegrees = 89.0f;
};

[[nodiscard]] bool ValidatePlayerSettings(
    const PlayerSettings& settings, std::string& error);

} // namespace fps
