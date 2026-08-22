#pragma once

namespace fps {

struct CameraSettings {
    float fieldOfViewDegrees = 60.0f;
    float nearClip = 0.05f;
    float farClip = 100.0f;
};

} // namespace fps
