#pragma once

#include "RetroFPS/Math/Vector.hpp"

namespace fps {

class PlayerController;

class Player final {
public:
    Player() noexcept = default;

    [[nodiscard]] Float2 GetPositionXZ() const noexcept;
    [[nodiscard]] float GetYawRadians() const noexcept;
    [[nodiscard]] float GetPitchRadians() const noexcept;
    [[nodiscard]] float GetRecoilDegrees() const noexcept;

private:
    friend class PlayerController;

    void Reset(Float2 spawnPosition, float yawRadians, float pitchRadians) noexcept;
    void SetPositionXZ(Float2 position) noexcept;
    void SetLookAngles(float yawRadians, float pitchRadians) noexcept;
    void SetRecoilPitchRadians(float recoilPitchRadians) noexcept;

    [[nodiscard]] float GetAimPitchRadians() const noexcept;

    Float2 positionXZ_{};
    float yawRadians_ = 0.0f;
    float pitchRadians_ = 0.0f;
    float recoilPitchRadians_ = 0.0f;
};

} // namespace fps
