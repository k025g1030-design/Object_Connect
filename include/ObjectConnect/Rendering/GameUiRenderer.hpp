#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Game/GameFlow.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace object_connect {

struct UiPoint final {
    float x = 0.0f;
    float y = 0.0f;
};

class GameUiRenderer final {
public:
    GameUiRenderer() noexcept;
    ~GameUiRenderer();

    GameUiRenderer(const GameUiRenderer&) = delete;
    GameUiRenderer& operator=(const GameUiRenderer&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    void Draw(GameScreen screen, std::size_t selectedItem,
              const PuzzleCatalog& catalog,
              std::optional<std::size_t> currentPuzzleIndex,
              const PuzzleBoardSnapshot* board,
              bool solvedMenuReady);
    [[nodiscard]] std::optional<std::size_t> HitTest(
        GameScreen screen, UiPoint point, std::size_t puzzleCount,
        bool currentPuzzleIsLast, bool solvedMenuReady) const noexcept;
    void Finalize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace object_connect
