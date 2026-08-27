#pragma once

#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/Math/Vector.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <string>

namespace fps {

struct EnemyRenderSettings final {
    std::string billboardModelName{"map_wall"};
    float hitFlashBrightness = 1.5f;
};

struct EnemyAtlasUvTransform final {
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scaleX = 0.0f;
    float scaleY = 0.0f;
};

struct EnemyBillboardPose final {
    float width = 0.0f;
    float height = 0.0f;
    float centerY = 0.0f;
    float yawRadians = 0.0f;
};

[[nodiscard]] inline const EnemyAnimationClipDefinition& GetEnemyAnimationClip(
    const EnemyDefinition& definition,
    const EnemyState state) noexcept {
    switch (state) {
    case EnemyState::Idle:
        return definition.animations.idle;
    case EnemyState::Moving:
        return definition.animations.moving;
    case EnemyState::Attacking:
        return definition.animations.attacking;
    case EnemyState::Dead:
        return definition.animations.dead;
    }
    return definition.animations.idle;
}

[[nodiscard]] inline bool IsLoopingEnemyAnimation(const EnemyState state) noexcept {
    return state == EnemyState::Idle || state == EnemyState::Moving;
}

// Invalid clip timing is rejected during data loading and renderer initialization.
// The defensive frame-zero result keeps this pure presentation helper total.
[[nodiscard]] inline std::optional<std::size_t> ResolveEnemyAnimationFrame(
    const EnemyAnimationClipDefinition& clip,
    const EnemyState state,
    const float stateElapsedSeconds) noexcept {
    if (clip.frameCount == 0) {
        return std::nullopt;
    }
    if (!std::isfinite(stateElapsedSeconds) || stateElapsedSeconds <= 0.0f ||
        !std::isfinite(clip.secondsPerFrame) || clip.secondsPerFrame <= 0.0f) {
        return std::size_t{0};
    }

    const double rawFrame = std::floor(
        static_cast<double>(stateElapsedSeconds) /
        static_cast<double>(clip.secondsPerFrame));
    if (IsLoopingEnemyAnimation(state)) {
        return static_cast<std::size_t>(
            std::fmod(rawFrame, static_cast<double>(clip.frameCount)));
    }
    if (rawFrame >= static_cast<double>(clip.frameCount - 1)) {
        return static_cast<std::size_t>(clip.frameCount - 1);
    }
    return static_cast<std::size_t>(rawFrame);
}

// Computes an atlas transform whose endpoints address pixel centers rather than
// neighboring cells. All arithmetic used for bounds checking is widened first.
[[nodiscard]] inline std::optional<EnemyAtlasUvTransform> ResolveEnemyAtlasUv(
    const EnemyAnimationClipDefinition& clip,
    const std::uint32_t frameWidthPixels,
    const std::uint32_t frameHeightPixels,
    const std::size_t frameIndex,
    const std::uint64_t sheetWidthPixels,
    const std::uint32_t sheetHeightPixels) noexcept {
    if (frameWidthPixels == 0 || frameHeightPixels == 0 || sheetWidthPixels == 0 ||
        sheetHeightPixels == 0 || frameIndex >= clip.frameCount) {
        return std::nullopt;
    }

    const std::uint64_t left =
        static_cast<std::uint64_t>(clip.originXpx) +
        static_cast<std::uint64_t>(frameWidthPixels) * frameIndex;
    const std::uint64_t top = clip.originYpx;
    const std::uint64_t right = left + frameWidthPixels;
    const std::uint64_t bottom = top + frameHeightPixels;
    if (right > sheetWidthPixels || bottom > sheetHeightPixels) {
        return std::nullopt;
    }

    const double inverseWidth = 1.0 / static_cast<double>(sheetWidthPixels);
    const double inverseHeight = 1.0 / static_cast<double>(sheetHeightPixels);
    return EnemyAtlasUvTransform{
        static_cast<float>((static_cast<double>(left) + 0.5) * inverseWidth),
        // KamataEngine's OBJ loader converts OBJ V to DirectX V, so the
        // map_wall quad arrives with V=1 at its top edge. Start at the frame's
        // bottom pixel center and use a negative scale to keep atlas art upright.
        static_cast<float>((static_cast<double>(bottom) - 0.5) * inverseHeight),
        static_cast<float>(
            static_cast<double>(frameWidthPixels - 1) * inverseWidth),
        static_cast<float>(
            -static_cast<double>(frameHeightPixels - 1) * inverseHeight),
    };
}

[[nodiscard]] inline EnemyBillboardPose ResolveEnemyBillboardPose(
    const EnemyDefinition& definition,
    const Float2 enemyPosition,
    const Float2 viewerPosition,
    const float previousYawRadians = 0.0f) noexcept {
    float yawRadians = previousYawRadians;
    const float deltaX = viewerPosition.x - enemyPosition.x;
    const float deltaZ = viewerPosition.z - enemyPosition.z;
    if (std::isfinite(deltaX) && std::isfinite(deltaZ) &&
        (deltaX != 0.0f || deltaZ != 0.0f)) {
        // The shared map_wall quad has U=0 on local -X. Pointing its +Z face
        // at the viewer mirrors U on screen. Render the opposite face (culling
        // is disabled) so atlas left/right stays correct from every bearing.
        yawRadians = std::remainder(
            std::atan2(deltaX, deltaZ) + std::numbers::pi_v<float>,
            2.0f * std::numbers::pi_v<float>);
    }
    return {
        definition.renderWidth,
        definition.renderHeight,
        definition.renderHeight * 0.5f,
        yawRadians,
    };
}

} // namespace fps
