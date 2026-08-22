#pragma once

#include <memory>
#include <string>

namespace fps {

// Draws a black full-screen overlay for presentation transitions. Fade timing
// remains owned by MapSceneManager and scene commits remain owned by Game.
class ScreenFadeRenderer final {
public:
    ScreenFadeRenderer() noexcept;
    ~ScreenFadeRenderer();

    ScreenFadeRenderer(const ScreenFadeRenderer&) = delete;
    ScreenFadeRenderer& operator=(const ScreenFadeRenderer&) = delete;
    ScreenFadeRenderer(ScreenFadeRenderer&&) = delete;
    ScreenFadeRenderer& operator=(ScreenFadeRenderer&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    void Draw(float alpha) const;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fps
