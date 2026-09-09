#pragma once

#include "ObjectConnect/Game/GameFlow.hpp"
#include "ObjectConnect/Input/InputState.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace object_connect {

enum class MouseCursorIcon : std::uint8_t {
    Normal,
    Interactive,
    Holding,
};

[[nodiscard]] constexpr MouseCursorIcon ResolveMouseCursorIcon(
    const GameScreen screen, const bool isDragging,
    const bool pointerOverAction) noexcept {
    if (screen == GameScreen::Playing) {
        return isDragging ? MouseCursorIcon::Holding
                          : MouseCursorIcon::Normal;
    }
    return pointerOverAction ? MouseCursorIcon::Interactive
                             : MouseCursorIcon::Normal;
}

[[nodiscard]] constexpr bool ShouldUseCustomMouseCursor(
    const bool windowFocused, const bool insideClient) noexcept {
    return windowFocused && insideClient;
}

class MouseCursorRenderer final {
public:
    MouseCursorRenderer() noexcept;
    ~MouseCursorRenderer();

    MouseCursorRenderer(const MouseCursorRenderer&) = delete;
    MouseCursorRenderer& operator=(const MouseCursorRenderer&) = delete;

    // Texture acquisition and sprite creation are transactional: a failure
    // leaves this renderer uninitialized and releases every acquired handle.
    [[nodiscard]] bool Initialize(std::string& error);

    // Call once per game update after the desired icon has been resolved.
    // The OS cursor is hidden only while this renderer can draw a replacement
    // inside the focused game client area.
    void Update(const InputState& input, MouseCursorIcon icon) noexcept;

    // Call after all other sprite and text passes so the cursor stays on top.
    void Draw();

    // Safe to call repeatedly. Any ShowCursor calls owned by this renderer are
    // balanced before its sprites and texture handles are released.
    void Finalize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace object_connect
