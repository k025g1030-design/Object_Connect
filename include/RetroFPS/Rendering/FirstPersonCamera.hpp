#pragma once

#include "RetroFPS/Math/Vector.hpp"
#include "RetroFPS/Rendering/CameraSettings.hpp"

#include <memory>
#include <string>

namespace KamataEngine {
class Camera;
}

namespace fps {

class FirstPersonCamera final {
public:
    FirstPersonCamera() noexcept;
    ~FirstPersonCamera();

    FirstPersonCamera(const FirstPersonCamera&) = delete;
    FirstPersonCamera& operator=(const FirstPersonCamera&) = delete;
    FirstPersonCamera(FirstPersonCamera&&) = delete;
    FirstPersonCamera& operator=(FirstPersonCamera&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    [[nodiscard]] bool Initialize(const CameraSettings& settings, std::string& error);
    void Sync(Float3 position, float yawRadians, float pitchRadians);
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const KamataEngine::Camera& GetNativeCamera() const;
    [[nodiscard]] KamataEngine::Camera& GetNativeCamera();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
