#include "ObjectConnect/Rendering/GameUiRenderer.hpp"

#include <2d/DebugText.h>
#include <2d/Sprite.h>
#include <base/DirectXCommon.h>
#include <base/TextureManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace object_connect {
namespace {

struct UiRect final {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] bool Contains(const UiPoint point) const noexcept {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
               point.x >= left && point.x < left + width &&
               point.y >= top && point.y < top + height;
    }
};

struct MenuLayout final {
    std::vector<std::string> labels;
    std::vector<UiRect> bounds;
    std::vector<std::size_t> itemIndices;
    std::size_t pageIndex = 0;
    std::size_t pageCount = 1;
};

constexpr float kMenuLeft = 390.0f;
constexpr float kMenuWidth = 500.0f;
constexpr float kItemHeight = 54.0f;
constexpr float kItemGap = 16.0f;
constexpr float kTextScale = 1.55f;
constexpr std::size_t kLevelItemsPerPage = 7;

[[nodiscard]] MenuLayout MakeMenuLayout(const GameScreen screen,
                                        const PuzzleCatalog& catalog,
                                        const bool currentPuzzleIsLast,
                                        const std::size_t selectedItem) {
    MenuLayout layout;
    float top = 280.0f;
    switch (screen) {
    case GameScreen::MainMenu:
        layout.labels = {"PLAY", "EXIT"};
        break;
    case GameScreen::LevelSelect:
        top = 170.0f;
        {
            const std::size_t itemCount = catalog.GetPuzzles().size() + 1;
            const std::size_t safeSelection =
                itemCount == 0 ? 0 : (std::min)(selectedItem, itemCount - 1);
            layout.pageCount =
                (itemCount + kLevelItemsPerPage - 1) / kLevelItemsPerPage;
            layout.pageIndex = safeSelection / kLevelItemsPerPage;
            const std::size_t first = layout.pageIndex * kLevelItemsPerPage;
            const std::size_t last =
                (std::min)(itemCount, first + kLevelItemsPerPage);
            for (std::size_t item = first; item < last; ++item) {
                layout.itemIndices.push_back(item);
                if (item < catalog.GetPuzzles().size()) {
                    layout.labels.push_back(catalog.GetPuzzles()[item].title);
                } else {
                    layout.labels.push_back("BACK");
                }
            }
        }
        break;
    case GameScreen::Paused:
        top = 220.0f;
        layout.labels = {"RESUME", "LEVEL SELECT", "MAIN MENU", "EXIT GAME"};
        break;
    case GameScreen::Solved:
        top = 280.0f;
        if (!currentPuzzleIsLast) {
            layout.labels.push_back("NEXT PUZZLE");
        }
        layout.labels.push_back("LEVEL SELECT");
        layout.labels.push_back("RETRY");
        break;
    case GameScreen::Playing:
        break;
    }

    if (layout.itemIndices.empty()) {
        layout.itemIndices.resize(layout.labels.size());
        for (std::size_t index = 0; index < layout.itemIndices.size(); ++index) {
            layout.itemIndices[index] = index;
        }
    }

    layout.bounds.reserve(layout.labels.size());
    for (std::size_t index = 0; index < layout.labels.size(); ++index) {
        layout.bounds.push_back({
            kMenuLeft,
            top + static_cast<float>(index) * (kItemHeight + kItemGap),
            kMenuWidth,
            kItemHeight,
        });
    }
    return layout;
}

void Print(KamataEngine::DebugText& debugText, const std::string_view text,
           const float x, const float y, const float scale) {
    debugText.Print(std::string{text}, x, y, scale);
}

[[nodiscard]] std::string LengthText(const float value) {
    const long rounded = std::lround((std::max)(0.0f, value));
    return std::to_string(rounded);
}

} // namespace

struct GameUiRenderer::Impl final {
    KamataEngine::DebugText* debugText = nullptr;
    std::unique_ptr<KamataEngine::Sprite> background;
    std::unique_ptr<KamataEngine::Sprite> selection;
    std::unique_ptr<KamataEngine::Sprite> hudWarning;
    std::unique_ptr<KamataEngine::Sprite> messageWarning;
};

GameUiRenderer::GameUiRenderer() noexcept = default;
GameUiRenderer::~GameUiRenderer() { Finalize(); }

bool GameUiRenderer::Initialize(std::string& error) {
    Finalize();
    error.clear();
    try {
        auto next = std::make_unique<Impl>();
        next->debugText = KamataEngine::DebugText::GetInstance();
        const std::uint32_t white = KamataEngine::TextureManager::Load("white1x1.png");
        next->background.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.03f, 0.01f, 0.02f, 0.94f}));
        next->selection.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.65f, 0.12f, 0.20f, 0.48f}));
        next->hudWarning.reset(KamataEngine::Sprite::Create(
            white, {18.0f, 14.0f}, {0.75f, 0.02f, 0.06f, 0.72f}));
        next->messageWarning.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.75f, 0.02f, 0.06f, 0.72f}));
        if (next->debugText == nullptr || !next->background || !next->selection ||
            !next->hudWarning || !next->messageWarning) {
            error = "KamataEngine failed to create the UI resources.";
            return false;
        }
        next->background->SetSize({1280.0f, 720.0f});
        next->hudWarning->SetSize({390.0f, 46.0f});
        next->messageWarning->SetPosition({468.0f, 65.0f});
        next->messageWarning->SetSize({344.0f, 43.0f});
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize GameUiRenderer: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize GameUiRenderer because of an unknown error.";
    }
    Finalize();
    return false;
}

void GameUiRenderer::Draw(const GameScreen screen, const std::size_t selectedItem,
                          const PuzzleCatalog& catalog,
                          const std::optional<std::size_t> currentPuzzleIndex,
                          const PuzzleBoardSnapshot* const board,
                          const bool solvedMenuReady) {
    if (!impl_) {
        return;
    }

    const bool currentIsLast = currentPuzzleIndex.has_value() &&
                               *currentPuzzleIndex + 1 >= catalog.GetPuzzles().size();
    const MenuLayout layout =
        MakeMenuLayout(screen, catalog, currentIsLast, selectedItem);
    const bool showFullBackground = screen == GameScreen::MainMenu ||
                                    screen == GameScreen::LevelSelect;
    const bool showOverlay = screen == GameScreen::Paused ||
                             (screen == GameScreen::Solved && solvedMenuReady);
    if (showFullBackground) {
        impl_->background->SetColor({0.03f, 0.01f, 0.02f, 0.96f});
    } else if (showOverlay) {
        impl_->background->SetColor({0.03f, 0.01f, 0.02f, 0.72f});
    }

    const bool menuVisible = screen != GameScreen::Playing &&
                             (screen != GameScreen::Solved || solvedMenuReady);
    const bool showBoardWarning = board != nullptr &&
                                  (board->lengthExhausted || board->routeBlocked) &&
                                  !board->solved;
    const auto selectedVisible = std::find(
        layout.itemIndices.begin(), layout.itemIndices.end(), selectedItem);
    const bool hasSelection = menuVisible &&
                              selectedVisible != layout.itemIndices.end();
    if (hasSelection) {
        const std::size_t visibleIndex = static_cast<std::size_t>(
            selectedVisible - layout.itemIndices.begin());
        const UiRect& bounds = layout.bounds[visibleIndex];
        impl_->selection->SetPosition({bounds.left, bounds.top});
        impl_->selection->SetSize({bounds.width, bounds.height});
    }

    if (board != nullptr && currentPuzzleIndex.has_value() &&
        *currentPuzzleIndex < catalog.GetPuzzles().size()) {
        const PuzzleDefinition& puzzle = catalog.GetPuzzles()[*currentPuzzleIndex];
        for (const NodeDefinition& node : puzzle.nodes) {
            const float labelWidth = static_cast<float>(node.displayName.size()) *
                                     KamataEngine::DebugText::kFontWidth;
            const float centerX =
                (static_cast<float>(node.bounds.column) +
                 static_cast<float>(node.bounds.columns) * 0.5f) *
                static_cast<float>(kPuzzleTileSize);
            const float centerY =
                (static_cast<float>(node.bounds.row) +
                 static_cast<float>(node.bounds.rows) * 0.5f) *
                static_cast<float>(kPuzzleTileSize);
            Print(*impl_->debugText, node.displayName,
                  centerX - labelWidth * 0.5f,
                  centerY - KamataEngine::DebugText::kFontHeight * 0.5f,
                  1.0f);
        }
        Print(*impl_->debugText,
              "REMAINING " + LengthText(board->remainingLength) + " / " +
                  LengthText(board->totalLength),
              28.0f, 24.0f, 1.35f);
        Print(*impl_->debugText, "R - RETRY", 1060.0f, 28.0f, 1.0f);
        if (board->lengthExhausted && !board->solved) {
            Print(*impl_->debugText, "NOT ENOUGH LENGTH", 500.0f, 74.0f, 1.4f);
        } else if (board->routeBlocked && !board->solved) {
            Print(*impl_->debugText, "NO VALID CONNECTIONS", 488.0f, 74.0f, 1.4f);
        }
    }

    switch (screen) {
    case GameScreen::MainMenu:
        Print(*impl_->debugText, "OBJECT CONNECT", 442.0f, 118.0f, 2.5f);
        Print(*impl_->debugText, "RESTORE THE FLOW", 484.0f, 180.0f, 1.3f);
        break;
    case GameScreen::LevelSelect:
        Print(*impl_->debugText, "SELECT PUZZLE", 460.0f, 72.0f, 2.25f);
        if (layout.pageCount > 1) {
            Print(*impl_->debugText,
                  "PAGE " + std::to_string(layout.pageIndex + 1) + " / " +
                      std::to_string(layout.pageCount),
                  558.0f, 126.0f, 0.9f);
        }
        break;
    case GameScreen::Paused:
        Print(*impl_->debugText, "PAUSED", 535.0f, 100.0f, 2.5f);
        break;
    case GameScreen::Solved:
        if (solvedMenuReady) {
            Print(*impl_->debugText, "FLOW RESTORED", 455.0f, 112.0f, 2.3f);
        }
        break;
    case GameScreen::Playing:
        break;
    }

    if (menuVisible) {
        for (std::size_t index = 0; index < layout.labels.size(); ++index) {
            const UiRect& bounds = layout.bounds[index];
            if (layout.itemIndices[index] == selectedItem) {
                Print(*impl_->debugText, ">", bounds.left + 18.0f,
                      bounds.top + 10.0f, kTextScale);
            }
            Print(*impl_->debugText, layout.labels[index], bounds.left + 58.0f,
                  bounds.top + 10.0f, kTextScale);
        }
        Print(*impl_->debugText,
              "W/S OR UP/DOWN - SELECT    ENTER/LEFT CLICK - CONFIRM",
              300.0f, 662.0f, 0.95f);
    }

    KamataEngine::Sprite::PreDraw(
        KamataEngine::DirectXCommon::GetInstance()->GetCommandList(),
        KamataEngine::Sprite::BlendMode::kNormal);
    if (showFullBackground || showOverlay) {
        impl_->background->Draw();
    }
    if (hasSelection) {
        impl_->selection->Draw();
    }
    if (showBoardWarning) {
        impl_->hudWarning->Draw();
        impl_->messageWarning->Draw();
    }
    impl_->debugText->DrawAll();
    KamataEngine::Sprite::PostDraw();
}

std::optional<std::size_t> GameUiRenderer::HitTest(
    const GameScreen screen, const UiPoint point, const std::size_t puzzleCount,
    const bool currentPuzzleIsLast, const bool solvedMenuReady,
    const std::size_t selectedItem) const noexcept {
    if (!impl_ || screen == GameScreen::Playing ||
        (screen == GameScreen::Solved && !solvedMenuReady)) {
        return std::nullopt;
    }

    PuzzleCatalog placeholder;
    std::vector<PuzzleDefinition> puzzles(puzzleCount);
    for (std::size_t index = 0; index < puzzles.size(); ++index) {
        puzzles[index].title = "PUZZLE " + std::to_string(index + 1);
    }
    placeholder = PuzzleCatalog(std::move(puzzles));
    const MenuLayout layout =
        MakeMenuLayout(screen, placeholder, currentPuzzleIsLast, selectedItem);
    for (std::size_t index = 0; index < layout.bounds.size(); ++index) {
        if (layout.bounds[index].Contains(point)) {
            return layout.itemIndices[index];
        }
    }
    return std::nullopt;
}

void GameUiRenderer::Finalize() noexcept { impl_.reset(); }
bool GameUiRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace object_connect
