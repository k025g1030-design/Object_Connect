#pragma once

#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/Math/Vector.hpp"

#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fps {

struct EnemyTint final {
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
};

struct EnemyAnimationClipSettings final {
    std::vector<std::string> frameTexturePaths;
    float secondsPerFrame = 0.1f;
    bool loop = true;
};

struct EnemyAnimationSetSettings final {
    EnemyAnimationClipSettings idle{};
    EnemyAnimationClipSettings moving{};
    EnemyAnimationClipSettings attacking{{}, 0.1f, false};
    EnemyAnimationClipSettings dead{{}, 0.1f, false};
};

struct EnemyRenderSettings final {
    std::string billboardModelName{"map_wall"};
    std::string fallbackTexturePath{"white1x1.png"};
    float billboardWidth = 0.7f;
    float rangedHeight = 1.6f;
    float meleeHeightScale = 0.5f;
    EnemyTint meleeTint{1.0f, 0.0f, 0.0f, 1.0f};
    EnemyTint rangedTint{0.0f, 0.0f, 1.0f, 1.0f};
    EnemyAnimationSetSettings meleeAnimations{};
    EnemyAnimationSetSettings rangedAnimations{};
};

struct EnemyBillboardPose final {
    float width = 0.0f;
    float height = 0.0f;
    float centerY = 0.0f;
    float yawRadians = 0.0f;
};

[[nodiscard]] inline const EnemyAnimationClipSettings& GetEnemyAnimationClip(
    const EnemyRenderSettings& settings,
    const EnemyKind kind,
    const EnemyState state) noexcept {
    const EnemyAnimationSetSettings& set =
        kind == EnemyKind::Melee ? settings.meleeAnimations : settings.rangedAnimations;
    switch (state) {
    case EnemyState::Idle:
        return set.idle;
    case EnemyState::Moving:
        return set.moving;
    case EnemyState::Attacking:
        return set.attacking;
    case EnemyState::Dead:
        return set.dead;
    }
    return set.idle;
}

// An empty clip deliberately resolves to nullopt so the renderer uses its
// configured fallback rectangle. Invalid timing is rejected at Initialize;
// resolving it defensively selects frame zero.
[[nodiscard]] inline std::optional<std::size_t> ResolveEnemyAnimationFrame(
    const EnemyAnimationClipSettings& clip,
    const float stateElapsedSeconds,
    const std::size_t frameCount) noexcept {
    if (frameCount == 0) {
        return std::nullopt;
    }
    if (!std::isfinite(stateElapsedSeconds) || stateElapsedSeconds <= 0.0f ||
        !std::isfinite(clip.secondsPerFrame) || clip.secondsPerFrame <= 0.0f) {
        return std::size_t{0};
    }

    const double rawFrame = std::floor(
        static_cast<double>(stateElapsedSeconds) /
        static_cast<double>(clip.secondsPerFrame));
    if (clip.loop) {
        return static_cast<std::size_t>(
            std::fmod(rawFrame, static_cast<double>(frameCount)));
    }
    if (rawFrame >= static_cast<double>(frameCount - 1)) {
        return frameCount - 1;
    }
    return static_cast<std::size_t>(rawFrame);
}

[[nodiscard]] inline EnemyBillboardPose ResolveEnemyBillboardPose(
    const EnemyRenderSettings& settings,
    const EnemyKind kind,
    const Float2 enemyPosition,
    const Float2 viewerPosition,
    const float previousYawRadians = 0.0f) noexcept {
    const float height = kind == EnemyKind::Melee
                             ? settings.rangedHeight * settings.meleeHeightScale
                             : settings.rangedHeight;
    float yawRadians = previousYawRadians;
    const float deltaX = viewerPosition.x - enemyPosition.x;
    const float deltaZ = viewerPosition.z - enemyPosition.z;
    if (std::isfinite(deltaX) && std::isfinite(deltaZ) &&
        (deltaX != 0.0f || deltaZ != 0.0f)) {
        yawRadians = std::atan2(deltaX, deltaZ);
    }
    return {
        settings.billboardWidth,
        height,
        height * 0.5f,
        yawRadians,
    };
}

} // namespace fps
