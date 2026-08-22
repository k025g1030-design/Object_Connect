#include "RetroFPS/Game/GameFlow.hpp"

namespace fps {

std::optional<std::size_t> TryGetNextMapIndex(
    const std::size_t currentMapIndex, const std::size_t mapCount) noexcept {
    if (currentMapIndex >= mapCount || currentMapIndex + 1 >= mapCount) {
        return std::nullopt;
    }
    return currentMapIndex + 1;
}

GameFlowResult GameFlow::Update(const GameFlowInput& input) noexcept {
    const GameScreen previousScreen = screen_;
    GameFlowAction action = GameFlowAction::None;

    if (screen_ == GameScreen::Playing) {
        if (input.escapePressed || input.focusLost) {
            screen_ = GameScreen::Paused;
            selectedItem_ = 0;
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
            screen_ = GameScreen::MainMenu;
            selectedItem_ = 0;
        }
        return {action, screen_ != previousScreen, false};
    }

    if (screen_ == GameScreen::Paused && input.escapePressed) {
        screen_ = GameScreen::Playing;
        selectedItem_ = 0;
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
            screen_ = GameScreen::Playing;
            selectedItem_ = 0;
            return GameFlowAction::StartGame;
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
            screen_ = GameScreen::Playing;
            selectedItem_ = 0;
            return GameFlowAction::None;
        case 1:
            screen_ = GameScreen::MainMenu;
            selectedItem_ = 0;
            return GameFlowAction::ResetToMainMenu;
        case 2:
            return GameFlowAction::QuitGame;
        default:
            return GameFlowAction::None;
        }
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
