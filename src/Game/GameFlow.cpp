#include "RetroFPS/Game/GameFlow.hpp"

namespace fps {

GameFlowResult GameFlow::Update(const GameFlowInput& input) noexcept {
    const GameScreen previousScreen = screen_;
    GameFlowAction action = GameFlowAction::None;

    if (screen_ == GameScreen::Playing) {
        if (input.escapePressed || input.focusLost) {
            EnterPaused();
        }
        return {
            action,
            screen_ != previousScreen,
            screen_ == GameScreen::Playing && previousScreen == GameScreen::Playing,
        };
    }

    if (screen_ == GameScreen::Controls) {
        if (input.escapePressed || input.confirmPressed ||
            (input.mousePrimaryPressed && input.hoveredItem == 0)) {
            ReturnToMainMenu();
        }
        return {action, screen_ != previousScreen, false};
    }

    if (screen_ == GameScreen::Paused && input.escapePressed) {
        EnterPlaying();
        return {action, true, false};
    }

    ApplyNavigation(input);

    const bool clickedItem = input.mousePrimaryPressed && input.hoveredItem.has_value() &&
                             *input.hoveredItem < GetMenuItemCount();
    if (clickedItem) {
        selectedItem_ = *input.hoveredItem;
    }
    if (input.confirmPressed || clickedItem) {
        action = ActivateSelectedItem();
    }

    return {action, screen_ != previousScreen, false};
}

void GameFlow::EnterPlaying() noexcept {
    screen_ = GameScreen::Playing;
    selectedItem_ = 0;
}

void GameFlow::EnterPaused() noexcept {
    screen_ = GameScreen::Paused;
    selectedItem_ = 0;
}

void GameFlow::EnterResults() noexcept {
    screen_ = GameScreen::Results;
    selectedItem_ = 0;
}

void GameFlow::ReturnToMainMenu() noexcept {
    screen_ = GameScreen::MainMenu;
    selectedItem_ = 0;
}

std::size_t GameFlow::GetMenuItemCount() const noexcept {
    switch (screen_) {
    case GameScreen::MainMenu:
    case GameScreen::Paused:
        return 3;
    case GameScreen::Controls:
    case GameScreen::Results:
        return 1;
    case GameScreen::Playing:
        return 0;
    }
    return 0;
}

GameFlowAction GameFlow::ActivateSelectedItem() noexcept {
    if (screen_ == GameScreen::MainMenu) {
        switch (selectedItem_) {
        case 0:
            return GameFlowAction::RequestStartGame;
        case 1:
            screen_ = GameScreen::Controls;
            selectedItem_ = 0;
            return GameFlowAction::None;
        case 2:
            return GameFlowAction::QuitGame;
        default:
            return GameFlowAction::None;
        }
    }

    if (screen_ == GameScreen::Paused) {
        switch (selectedItem_) {
        case 0:
            EnterPlaying();
            return GameFlowAction::None;
        case 1:
            return GameFlowAction::RequestMainMenu;
        case 2:
            return GameFlowAction::QuitGame;
        default:
            return GameFlowAction::None;
        }
    }

    if (screen_ == GameScreen::Results) {
        return selectedItem_ == 0 ? GameFlowAction::RequestMainMenu : GameFlowAction::None;
    }

    return GameFlowAction::None;
}

void GameFlow::ApplyNavigation(const GameFlowInput& input) noexcept {
    const std::size_t itemCount = GetMenuItemCount();
    if (itemCount == 0) {
        selectedItem_ = 0;
        return;
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

} // namespace fps
