#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Game/GameFlow.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace object_connect {

class FontSystem;
struct FontHandle;

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

    [[nodiscard]] bool Initialize(FontSystem& fontSystem, FontHandle font,
                                  std::string& error);
    void Draw(GameScreen screen, std::size_t selectedItem,
              const PuzzleCatalog& catalog,
              std::optional<std::size_t> currentPuzzleIndex,
              const PuzzleBoardSnapshot* board,
              bool hasNextPuzzle,
              bool solvedMenuReady);
    [[nodiscard]] std::optional<std::size_t> HitTest(
        GameScreen screen, UiPoint point, std::size_t puzzleCount,
        bool hasNextPuzzle, bool solvedMenuReady) const noexcept;
    [[nodiscard]] bool IsPointerOverAction(
        GameScreen screen, UiPoint point, std::size_t puzzleCount,
        bool hasNextPuzzle, bool solvedMenuReady) const noexcept;
    // Call after hit-testing the currently drawn page and before flow
    // handling. Page controls never masquerade as menu item indices; when the
    // page changes, the first absolute puzzle index on that page is returned
    // so GameFlow can update its selection through its normal input path.
    [[nodiscard]] std::optional<std::size_t> ApplyLevelSelectNavigation(
        UiPoint point, bool mousePrimaryPressed, int wheelDelta,
        bool keyboardNavigated, bool activationRequested,
        GameScreen screen) noexcept;
    void Finalize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace object_connect
