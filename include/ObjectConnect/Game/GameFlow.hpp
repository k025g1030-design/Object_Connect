#pragma once

#include <cstddef>
#include <optional>

namespace object_connect {

enum class GameScreen {
    MainMenu,
    LevelSelect,
    Playing,
    Paused,
    Solved,
};

enum class GameCommand {
    None,
    OpenLevelSelect,
    StartPuzzle,
    RetryPuzzle,
    NextPuzzle,
    ReturnToMainMenu,
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
    GameCommand command = GameCommand::None;
    std::optional<std::size_t> puzzleIndex;
    bool screenChanged = false;
    bool simulatePuzzle = false;
};

class GameFlow final {
public:
    [[nodiscard]] GameFlowResult Update(const GameFlowInput& input,
                                        std::size_t puzzleCount,
                                        bool hasNextPuzzle) noexcept;

    void EnterLevelSelect() noexcept;
    void EnterPlaying() noexcept;
    void EnterPaused() noexcept;
    void EnterSolved() noexcept;
    void ReturnToMainMenu() noexcept;

    [[nodiscard]] GameScreen GetScreen() const noexcept { return screen_; }
    [[nodiscard]] std::size_t GetSelectedItem() const noexcept { return selectedItem_; }
    [[nodiscard]] std::size_t GetItemCount(std::size_t puzzleCount,
                                           bool hasNextPuzzle) const noexcept;

private:
    void ApplyNavigation(const GameFlowInput& input, std::size_t itemCount) noexcept;
    [[nodiscard]] GameFlowResult ActivateSelected(std::size_t puzzleCount,
                                                  bool hasNextPuzzle) noexcept;

    GameScreen screen_ = GameScreen::MainMenu;
    std::size_t selectedItem_ = 0;
};

} // namespace object_connect
