#include "RetroFPS/Gameplay/Player/Player.hpp"

namespace fps {

Float2 Player::GetPositionXZ() const noexcept { return positionXZ_; }

float Player::GetYawRadians() const noexcept { return yawRadians_; }

float Player::GetPitchRadians() const noexcept { return pitchRadians_; }

void Player::Reset(
    const Float2 spawnPosition,
    const float yawRadians,
    const float pitchRadians) noexcept {
    positionXZ_ = spawnPosition;
    yawRadians_ = yawRadians;
    pitchRadians_ = pitchRadians;
}

void Player::SetPositionXZ(const Float2 position) noexcept { positionXZ_ = position; }

void Player::SetLookAngles(
    const float yawRadians, const float pitchRadians) noexcept {
    yawRadians_ = yawRadians;
    pitchRadians_ = pitchRadians;
}

} // namespace fps
