#include "RetroFPS/Gameplay/Player/Player.hpp"

#include <numbers>

namespace fps {

Float2 Player::GetPositionXZ() const noexcept { return positionXZ_; }

float Player::GetYawRadians() const noexcept { return yawRadians_; }

float Player::GetPitchRadians() const noexcept {
    return pitchRadians_ + recoilPitchRadians_;
}

float Player::GetRecoilDegrees() const noexcept {
    constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
    return -recoilPitchRadians_ * kRadiansToDegrees;
}

void Player::Reset(
    const Float2 spawnPosition,
    const float yawRadians,
    const float pitchRadians) noexcept {
    positionXZ_ = spawnPosition;
    yawRadians_ = yawRadians;
    pitchRadians_ = pitchRadians;
    recoilPitchRadians_ = 0.0f;
}

void Player::SetPositionXZ(const Float2 position) noexcept { positionXZ_ = position; }

void Player::SetLookAngles(
    const float yawRadians, const float pitchRadians) noexcept {
    yawRadians_ = yawRadians;
    pitchRadians_ = pitchRadians;
}

void Player::SetRecoilPitchRadians(const float recoilPitchRadians) noexcept {
    recoilPitchRadians_ = recoilPitchRadians;
}

float Player::GetAimPitchRadians() const noexcept { return pitchRadians_; }

} // namespace fps
