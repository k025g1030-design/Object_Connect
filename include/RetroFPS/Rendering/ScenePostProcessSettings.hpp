#pragma once

#include <algorithm>
#include <cmath>

namespace fps {

struct ScenePostProcessSettings final {
    float brightness = 1.25f;
    float gamma = 2.2f;
};

[[nodiscard]] inline bool IsValidScenePostProcessSettings(
    const ScenePostProcessSettings& settings) noexcept {
    return std::isfinite(settings.brightness) && settings.brightness > 0.0f &&
           std::isfinite(settings.gamma) && settings.gamma > 0.0f;
}

[[nodiscard]] inline float SceneGammaExponent(
    const ScenePostProcessSettings& settings) noexcept {
    return 2.2f / settings.gamma;
}

// Mirrors the pixel shader for deterministic parameter and curve tests.
[[nodiscard]] inline float ApplyScenePostProcessChannel(
    const float linearChannel,
    const ScenePostProcessSettings& settings) noexcept {
    const float brightened =
        std::clamp(linearChannel * settings.brightness, 0.0f, 1.0f);
    return std::pow(brightened, SceneGammaExponent(settings));
}

} // namespace fps
