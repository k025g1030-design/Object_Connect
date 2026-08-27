#pragma once

#include "RetroFPS/Math/Vector.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace KamataEngine {
class Camera;
}

namespace fps {

struct SkyRenderSettings final {
    std::string texturePath{"assets/textures/sky/sky_sphere.png"};
    float radius = 50.0f;
    std::uint32_t verticalSegments = 32;
    std::uint32_t horizontalSegments = 64;
};

class SkySphereRenderer final {
public:
    SkySphereRenderer() noexcept;
    ~SkySphereRenderer();

    SkySphereRenderer(const SkySphereRenderer&) = delete;
    SkySphereRenderer& operator=(const SkySphereRenderer&) = delete;
    SkySphereRenderer(SkySphereRenderer&&) = delete;
    SkySphereRenderer& operator=(SkySphereRenderer&&) = delete;

    [[nodiscard]] bool Initialize(
        const SkyRenderSettings& settings,
        float cameraFarClip,
        std::string& error);
    void Sync(Float3 cameraPosition);
    void Draw(const KamataEngine::Camera& camera) const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
