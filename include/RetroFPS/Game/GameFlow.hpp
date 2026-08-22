#pragma once

#include <cstddef>
#include <optional>

namespace fps {

enum class GameScreen {
    MainMenu,
    Controls,
    Playing,
    Paused,
    Results,
};

enum class GameFlowAction {
    None,
    RequestStartGame,
    RequestMainMenu,
    QuitGame,
};

struct GameFlowInput final {
    bool previousPressed = false;
    bool nextPressed = false;
    bool confirmPressed = false;
    bool escapePressed = false;
    bool focusLost = false;
    bool mousePrimaryPressed = false;
    std::optional<std::size_t> hoveredItem;
};

struct GameFlowResult final {
    GameFlowAction action = GameFlowAction::None;
    bool screenChanged = false;
    bool simulateGameplay = false;
};

// Engine-independent menu and pause state. Platform input is translated into
// GameFlowInput by Game so this behavior can be covered by headless tests.
class GameFlow final {
public:
    [[nodiscard]] GameFlowResult Update(const GameFlowInput& input) noexcept;
    void EnterPlaying() noexcept;
    void EnterPaused() noexcept;
    void EnterResults() noexcept;
    void ReturnToMainMenu() noexcept;

    [[nodiscard]] GameScreen GetScreen() const noexcept { return screen_; }
    [[nodiscard]] std::size_t GetSelectedItem() const noexcept { return selectedItem_; }

private:
    [[nodiscard]] std::size_t GetMenuItemCount() const noexcept;
    [[nodiscard]] GameFlowAction ActivateSelectedItem() noexcept;
    void ApplyNavigation(const GameFlowInput& input) noexcept;

    GameScreen screen_ = GameScreen::MainMenu;
    std::size_t selectedItem_ = 0;
};

} // namespace fps
