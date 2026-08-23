#pragma once

#include <memory>
#include <string>

namespace fps {

struct GameHudState final {
    int hitPoints = 100;
    int magazineAmmo = 0;
    int reserveAmmo = 0;
    bool reloading = false;
    float reloadProgress = 0.0f;
    float crosshairGapPixels = 8.0f;
    float weaponKickPixels = 0.0f;
};

class GameHudRenderer final {
public:
    GameHudRenderer() noexcept;
    ~GameHudRenderer();

    GameHudRenderer(const GameHudRenderer&) = delete;
    GameHudRenderer& operator=(const GameHudRenderer&) = delete;
    GameHudRenderer(GameHudRenderer&&) = delete;
    GameHudRenderer& operator=(GameHudRenderer&&) = delete;

    [[nodiscard]] bool Initialize(const std::string& weaponTexturePath, std::string& error);
    void Draw(const GameHudState& state) const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
