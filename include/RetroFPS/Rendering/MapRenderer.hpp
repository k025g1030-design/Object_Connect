#pragma once

#include <memory>
#include <string>

namespace KamataEngine {
class Camera;
}

namespace fps {

struct MapGeometry;
struct MapRenderAssets;

class MapRenderer final {
public:
    MapRenderer() noexcept;
    ~MapRenderer();

    MapRenderer(const MapRenderer&) = delete;
    MapRenderer& operator=(const MapRenderer&) = delete;
    MapRenderer(MapRenderer&&) = delete;
    MapRenderer& operator=(MapRenderer&&) = delete;

    [[nodiscard]] bool Initialize(
        const MapGeometry& geometry,
        const MapRenderAssets& assets,
        std::string& error);
    // Door instances use the configured model with a white texture override;
    // floor and wall instances use their model materials.
    void Draw(const KamataEngine::Camera& camera) const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
