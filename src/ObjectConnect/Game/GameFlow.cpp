#include "ObjectConnect/Game/GameFlow.hpp"

namespace object_connect {

GameFlowResult GameFlow::Update(const GameFlowInput& input,
                                const std::size_t puzzleCount,
                                const bool hasNextPuzzle) noexcept {
    if (screen_ == GameScreen::Playing) {
        if (input.escapePressed || input.focusLost) {
            EnterPaused();
            return {GameCommand::None, std::nullopt, true, false};
        }
        return {GameCommand::None, std::nullopt, false, true};
    }

    if (screen_ == GameScreen::Paused && input.escapePressed) {
        EnterPlaying();
        return {GameCommand::None, std::nullopt, true, false};
    }

    if (screen_ == GameScreen::LevelSelect && input.escapePressed) {
        return {GameCommand::ReturnToMainMenu, std::nullopt, false, false};
    }

    if (screen_ == GameScreen::Solved && input.escapePressed) {
        return {GameCommand::OpenLevelSelect, std::nullopt, false, false};
    }

    const std::size_t itemCount = GetItemCount(puzzleCount, hasNextPuzzle);
    ApplyNavigation(input, itemCount);

    const bool clickedItem = input.mousePrimaryPressed && input.hoveredItem.has_value() &&
                             *input.hoveredItem < itemCount;
    if (clickedItem) {
        selectedItem_ = *input.hoveredItem;
    }

    if (!input.confirmPressed && !clickedItem) {
        return {};
    }
    return ActivateSelected(puzzleCount, hasNextPuzzle);
}

void GameFlow::EnterLevelSelect() noexcept {
    screen_ = GameScreen::LevelSelect;
    selectedItem_ = 0;
}

void GameFlow::EnterPlaying() noexcept {
    screen_ = GameScreen::Playing;
    selectedItem_ = 0;
}

void GameFlow::EnterPaused() noexcept {
    screen_ = GameScreen::Paused;
    selectedItem_ = 0;
}

void GameFlow::EnterSolved() noexcept {
    screen_ = GameScreen::Solved;
    selectedItem_ = 0;
}

void GameFlow::ReturnToMainMenu() noexcept {
    screen_ = GameScreen::MainMenu;
    selectedItem_ = 0;
}

std::size_t GameFlow::GetItemCount(const std::size_t puzzleCount,
                                   const bool hasNextPuzzle) const noexcept {
    switch (screen_) {
    case GameScreen::MainMenu:
        return 2;
    case GameScreen::LevelSelect:
        return puzzleCount + 1;
    case GameScreen::Playing:
        return 0;
    case GameScreen::Paused:
        return 4;
    case GameScreen::Solved:
        return hasNextPuzzle ? 3 : 2;
    }
    return 0;
}

void GameFlow::ApplyNavigation(const GameFlowInput& input,
                               const std::size_t itemCount) noexcept {
    if (itemCount == 0) {
        selectedItem_ = 0;
        return;
    }

    if (selectedItem_ >= itemCount) {
        selectedItem_ = 0;
    }
    if (input.hoveredItem.has_value() && *input.hoveredItem < itemCount) {
        selectedItem_ = *input.hoveredItem;
    }

    if (input.previousPressed == input.nextPressed) {
        return;
    }
    if (input.previousPressed) {
        selectedItem_ = selectedItem_ == 0 ? itemCount - 1 : selectedItem_ - 1;
        return;
    }
    selectedItem_ = (selectedItem_ + 1) % itemCount;
}

GameFlowResult GameFlow::ActivateSelected(const std::size_t puzzleCount,
                                          const bool hasNextPuzzle) noexcept {
    switch (screen_) {
    case GameScreen::MainMenu:
        return selectedItem_ == 0
                   ? GameFlowResult{GameCommand::OpenLevelSelect, std::nullopt, false, false}
                   : GameFlowResult{GameCommand::QuitGame, std::nullopt, false, false};

    case GameScreen::LevelSelect:
        if (selectedItem_ < puzzleCount) {
            return {GameCommand::StartPuzzle, selectedItem_, false, false};
        }
        return {GameCommand::ReturnToMainMenu, std::nullopt, false, false};

    case GameScreen::Playing:
        return {};

    case GameScreen::Paused:
        switch (selectedItem_) {
        case 0:
            EnterPlaying();
            return {GameCommand::None, std::nullopt, true, false};
        case 1:
            return {GameCommand::OpenLevelSelect, std::nullopt, false, false};
        case 2:
            return {GameCommand::ReturnToMainMenu, std::nullopt, false, false};
        case 3:
            return {GameCommand::QuitGame, std::nullopt, false, false};
        default:
            return {};
        }

    case GameScreen::Solved:
        if (!hasNextPuzzle) {
            return selectedItem_ == 0
                       ? GameFlowResult{GameCommand::OpenLevelSelect, std::nullopt, false, false}
                       : GameFlowResult{GameCommand::RetryPuzzle, std::nullopt, false, false};
        }
        switch (selectedItem_) {
        case 0:
            return {GameCommand::NextPuzzle, std::nullopt, false, false};
        case 1:
            return {GameCommand::OpenLevelSelect, std::nullopt, false, false};
        case 2:
            return {GameCommand::RetryPuzzle, std::nullopt, false, false};
        default:
            return {};
        }
    }
    return {};
}

} // namespace object_connect
