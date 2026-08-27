#pragma once

#include "RetroFPS/Rendering/ScenePostProcessSettings.hpp"

#include <memory>
#include <string>

namespace fps {

// Owns the world-scene render target and composites it to the sRGB backbuffer.
// BeginScene/Composite must be paired once per rendered world frame.
class ScenePostProcessRenderer final {
public:
    ScenePostProcessRenderer() noexcept;
    ~ScenePostProcessRenderer();

    ScenePostProcessRenderer(const ScenePostProcessRenderer&) = delete;
    ScenePostProcessRenderer& operator=(const ScenePostProcessRenderer&) = delete;
    ScenePostProcessRenderer(ScenePostProcessRenderer&&) = delete;
    ScenePostProcessRenderer& operator=(ScenePostProcessRenderer&&) = delete;

    [[nodiscard]] bool Initialize(
        const ScenePostProcessSettings& settings,
        std::string& error);
    void BeginScene() const;
    void Composite() const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
