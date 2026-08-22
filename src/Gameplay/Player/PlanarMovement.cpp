#include "RetroFPS/Gameplay/Player/PlanarMovement.hpp"

#include <algorithm>
#include <cmath>

namespace fps {

Float2 ComputePlanarInput(
    const float forwardAxis,
    const float rightAxis,
    const float yawRadians,
    const float pitchRadians) noexcept {
    static_cast<void>(pitchRadians);
    const float clampedForward = std::clamp(forwardAxis, -1.0f, 1.0f);
    const float clampedRight = std::clamp(rightAxis, -1.0f, 1.0f);
    const float sinYaw = std::sin(yawRadians);
    const float cosYaw = std::cos(yawRadians);

    Float2 movement{
        sinYaw * clampedForward + cosYaw * clampedRight,
        cosYaw * clampedForward - sinYaw * clampedRight,
    };

    const float lengthSquared = movement.x * movement.x + movement.z * movement.z;
    if (lengthSquared > 1.0f) {
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        movement.x *= inverseLength;
        movement.z *= inverseLength;
    }

    return movement;
}

} // namespace fps
