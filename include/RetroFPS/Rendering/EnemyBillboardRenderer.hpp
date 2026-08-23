#pragma once

#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/Math/Vector.hpp"

#include <memory>
#include <span>
#include <string>

namespace KamataEngine {
class Camera;
}

namespace fps {

struct EnemyRenderSettings;

// Renders EnemySystem snapshots as upright world-space billboards. Animation
// selection is presentation-only and is driven by EnemyState plus its elapsed
// time; gameplay never depends on texture frame data.
class EnemyBillboardRenderer final {
public:
    EnemyBillboardRenderer() noexcept;
    ~EnemyBillboardRenderer();

    EnemyBillboardRenderer(const EnemyBillboardRenderer&) = delete;
    EnemyBillboardRenderer& operator=(const EnemyBillboardRenderer&) = delete;
    EnemyBillboardRenderer(EnemyBillboardRenderer&&) = delete;
    EnemyBillboardRenderer& operator=(EnemyBillboardRenderer&&) = delete;

    [[nodiscard]] bool Initialize(
        std::span<const EnemySnapshot> snapshots,
        const EnemyRenderSettings& settings,
        std::string& error);
    void Sync(std::span<const EnemySnapshot> snapshots, Float2 viewerPosition);
    void Draw(const KamataEngine::Camera& camera) const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
