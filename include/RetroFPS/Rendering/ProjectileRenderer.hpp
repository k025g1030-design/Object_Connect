#pragma once

#include "RetroFPS/Gameplay/Combat/ProjectileSystem.hpp"

#include <memory>
#include <span>
#include <string>

namespace KamataEngine {
class Camera;
}

namespace fps {

class ProjectileRenderer final {
public:
    ProjectileRenderer() noexcept;
    ~ProjectileRenderer();

    ProjectileRenderer(const ProjectileRenderer&) = delete;
    ProjectileRenderer& operator=(const ProjectileRenderer&) = delete;
    ProjectileRenderer(ProjectileRenderer&&) = delete;
    ProjectileRenderer& operator=(ProjectileRenderer&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    void Sync(std::span<const ProjectileSnapshot> snapshots);
    void Draw(const KamataEngine::Camera& camera) const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
