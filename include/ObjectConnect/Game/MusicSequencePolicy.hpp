#pragma once

#include "ObjectConnect/Game/GameFlow.hpp"

#include <utility>

namespace object_connect {

// Engine-independent ownership rule for the global music sequence. Browsing
// menus never restarts music; returning to MainMenu consumes the restart only
// after at least one puzzle session was successfully entered.
class MusicSequencePolicy final {
public:
    void RecordPuzzleEntered() noexcept { enteredPuzzle_ = true; }

    [[nodiscard]] bool OnScreenEntered(const GameScreen screen) noexcept {
        if (screen != GameScreen::MainMenu) {
            return false;
        }
        return std::exchange(enteredPuzzle_, false);
    }

private:
    bool enteredPuzzle_ = false;
};

} // namespace object_connect
