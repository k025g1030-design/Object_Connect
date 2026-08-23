#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"

#include <cmath>

namespace fps {

bool ValidatePlayerSettings(
    const PlayerSettings& settings, std::string& error) {
    error.clear();

    if (!std::isfinite(settings.eyeHeight) || settings.eyeHeight < 0.0f) {
        error = "player eye height must be finite and non-negative";
        return false;
    }
    if (!std::isfinite(settings.bodyHeight) || settings.bodyHeight <= 0.0f) {
        error = "player body height must be finite and greater than zero";
        return false;
    }
    if (!std::isfinite(settings.collisionRadius) || settings.collisionRadius <= 0.0f) {
        error = "player collision radius must be finite and greater than zero";
        return false;
    }
    if (settings.bodyHeight < settings.collisionRadius * 2.0f) {
        error = "player body height must not be smaller than the collision diameter";
        return false;
    }
    if (settings.eyeHeight > settings.bodyHeight) {
        error = "player eye height must not exceed the body height";
        return false;
    }
    if (!std::isfinite(settings.movementSpeed) || settings.movementSpeed < 0.0f) {
        error = "player movement speed must be finite and non-negative";
        return false;
    }
    if (!std::isfinite(settings.mouseSensitivity) || settings.mouseSensitivity < 0.0f) {
        error = "player mouse sensitivity must be finite and non-negative";
        return false;
    }
    if (!std::isfinite(settings.maxPitchDegrees) ||
        settings.maxPitchDegrees < 0.0f || settings.maxPitchDegrees >= 90.0f) {
        error = "player maximum pitch must be finite and in [0, 90) degrees";
        return false;
    }

    return true;
}

} // namespace fps
