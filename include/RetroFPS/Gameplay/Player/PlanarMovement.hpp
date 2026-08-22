#pragma once

#include "RetroFPS/Math/Vector.hpp"

namespace fps {

// 長さが1を超えないXZ移動ベクトルを返す。ヨーが0なら+Z方向を向く。
// ピッチは視線方向の一部として受け取るが、XZ移動には影響させない。
[[nodiscard]] Float2 ComputePlanarInput(
    float forwardAxis,
    float rightAxis,
    float yawRadians,
    float pitchRadians = 0.0f) noexcept;

} // namespace fps
