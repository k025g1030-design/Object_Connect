#include "ObjectConnect/Rendering/GameUiRenderer.hpp"

#include "ObjectConnect/Text/FontSystem.hpp"

#include "TextureHandleRegistry.hpp"

#include <2d/Sprite.h>
#include <base/DirectXCommon.h>

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

// Windows.h aliases DrawText to DrawTextW. FontSystem deliberately uses the
// engine-facing DrawText name, so keep the Win32 macro out of this unit.
#ifdef DrawText
#undef DrawText
#endif

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
    enum class EntryVisual {
        Standard,
        LevelTile,
        LevelBack,
    };

    struct Entry final {
        std::size_t logicalItemIndex = 0;
        std::string label;
        UiRect bounds;
        EntryVisual visual = EntryVisual::Standard;
    };

    std::vector<Entry> entries;
    std::optional<UiRect> previousPage;
    std::optional<UiRect> nextPage;
    std::size_t levelPage = 0;
    std::size_t levelPageCount = 1;
};

constexpr float kMenuLeft = 390.0f;
constexpr float kMenuWidth = 500.0f;
constexpr float kItemHeight = 54.0f;
constexpr float kItemGap = 16.0f;
constexpr std::uint32_t kMenuTextPixelSize = 28;
constexpr std::size_t kLevelGridColumns = 5;
constexpr std::size_t kLevelGridRows = 2;
constexpr std::size_t kLevelsPerPage =
    kLevelGridColumns * kLevelGridRows;
constexpr float kLevelGridTop = 210.0f;
constexpr float kLevelTileSize = 96.0f;
constexpr float kLevelTileGap = 28.0f;
constexpr float kLevelBackSize = 88.0f;
constexpr float kLevelBackLeft = 596.0f;
constexpr float kLevelBackTop = 558.0f;
constexpr float kLevelNumberBackdropWidth = 72.0f;
constexpr float kLevelNumberBackdropHeight = 68.0f;
constexpr std::uint32_t kLevelNumberMaximumPixelSize = 32;
constexpr UiRect kPreviousPageBounds{126.0f, 284.0f, 88.0f, 88.0f};
constexpr UiRect kNextPageBounds{1066.0f, 284.0f, 88.0f, 88.0f};
constexpr float kUiWidth = 1280.0f;
constexpr std::int64_t kWheelDeltaPerPage = 120;

struct UiColor final {
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 1.0f;
};

struct DecorationSpec final {
    UiRect bounds;
    UiColor color;
};

constexpr std::array<DecorationSpec, 11> kLevelDecorationSpecs{{
    {{142.0f, 48.0f, 996.0f, 624.0f}, {0.53f, 0.45f, 0.39f, 1.0f}},
    {{154.0f, 60.0f, 972.0f, 600.0f}, {0.19f, 0.15f, 0.13f, 1.0f}},
    {{169.0f, 75.0f, 942.0f, 570.0f}, {0.39f, 0.32f, 0.28f, 1.0f}},
    {{182.0f, 88.0f, 916.0f, 544.0f}, {0.25f, 0.20f, 0.17f, 1.0f}},
    {{194.0f, 100.0f, 892.0f, 520.0f}, {0.32f, 0.26f, 0.23f, 1.0f}},
    {{244.0f, 174.0f, 792.0f, 314.0f}, {0.18f, 0.14f, 0.12f, 1.0f}},
    {{254.0f, 184.0f, 772.0f, 294.0f}, {0.43f, 0.36f, 0.31f, 1.0f}},
    {{265.0f, 195.0f, 750.0f, 272.0f}, {0.28f, 0.22f, 0.19f, 1.0f}},
    {{414.0f, 44.0f, 452.0f, 108.0f}, {0.55f, 0.47f, 0.40f, 1.0f}},
    {{425.0f, 55.0f, 430.0f, 86.0f}, {0.19f, 0.15f, 0.13f, 1.0f}},
    {{438.0f, 67.0f, 404.0f, 62.0f}, {0.38f, 0.31f, 0.27f, 1.0f}},
}};

[[nodiscard]] std::size_t GetLevelPageCount(
    const std::size_t puzzleCount) noexcept {
    if (puzzleCount == 0) {
        return 1;
    }
    return 1 + (puzzleCount - 1) / kLevelsPerPage;
}

[[nodiscard]] float GetCenteredRowLeft(const std::size_t itemCount) noexcept {
    if (itemCount == 0) {
        return kUiWidth * 0.5f;
    }
    const float width = static_cast<float>(itemCount) * kLevelTileSize +
                        static_cast<float>(itemCount - 1) * kLevelTileGap;
    return (kUiWidth - width) * 0.5f;
}

void AddVerticalEntries(MenuLayout& layout,
                        const std::span<const std::string_view> labels,
                        const float top) {
    layout.entries.reserve(layout.entries.size() + labels.size());
    for (const std::string_view label : labels) {
        const std::size_t index = layout.entries.size();
        layout.entries.push_back({
            index,
            std::string{label},
            {
                kMenuLeft,
                top + static_cast<float>(index) * (kItemHeight + kItemGap),
                kMenuWidth,
                kItemHeight,
            },
            MenuLayout::EntryVisual::Standard,
        });
    }
}

[[nodiscard]] MenuLayout MakeMenuLayout(const GameScreen screen,
                                        const std::size_t puzzleCount,
                                        const bool hasNextPuzzle,
                                        const std::size_t requestedLevelPage) {
    MenuLayout layout;
    switch (screen) {
    case GameScreen::MainMenu: {
        constexpr std::array labels = {
            std::string_view{"PLAY"}, std::string_view{"EXIT"}};
        AddVerticalEntries(layout, labels, 280.0f);
        break;
    }
    case GameScreen::LevelSelect: {
        layout.levelPageCount = GetLevelPageCount(puzzleCount);
        layout.levelPage = (std::min)(requestedLevelPage,
                                      layout.levelPageCount - 1);
        layout.previousPage = kPreviousPageBounds;
        layout.nextPage = kNextPageBounds;

        const std::size_t firstIndex = layout.levelPage * kLevelsPerPage;
        const std::size_t itemCount = firstIndex < puzzleCount
                                          ? (std::min)(kLevelsPerPage,
                                                       puzzleCount - firstIndex)
                                          : 0;
        layout.entries.reserve(itemCount + 1);
        const std::size_t visibleRows =
            itemCount / kLevelGridColumns +
            (itemCount % kLevelGridColumns == 0 ? 0 : 1);
        for (std::size_t row = 0; row < visibleRows; ++row) {
            const std::size_t rowFirstIndex =
                firstIndex + row * kLevelGridColumns;
            const std::size_t rowOffset = row * kLevelGridColumns;
            const std::size_t count = (std::min)(
                kLevelGridColumns, itemCount - rowOffset);
            const float left = GetCenteredRowLeft(count);
            const float top = kLevelGridTop +
                              static_cast<float>(row) *
                                  (kLevelTileSize + kLevelTileGap);
            for (std::size_t column = 0; column < count; ++column) {
                const std::size_t puzzleIndex = rowFirstIndex + column;
                layout.entries.push_back({
                    puzzleIndex,
                    std::to_string(puzzleIndex + 1),
                    {
                        left + static_cast<float>(column) *
                                   (kLevelTileSize + kLevelTileGap),
                        top,
                        kLevelTileSize,
                        kLevelTileSize,
                    },
                    MenuLayout::EntryVisual::LevelTile,
                });
            }
        }
        layout.entries.push_back({
            puzzleCount,
            "BACK",
            {kLevelBackLeft, kLevelBackTop, kLevelBackSize, kLevelBackSize},
            MenuLayout::EntryVisual::LevelBack,
        });
        break;
    }
    case GameScreen::Paused: {
        constexpr std::array labels = {
            std::string_view{"RESUME"}, std::string_view{"LEVEL SELECT"},
            std::string_view{"MAIN MENU"}, std::string_view{"EXIT GAME"}};
        AddVerticalEntries(layout, labels, 220.0f);
        break;
    }
    case GameScreen::Solved: {
        std::vector<std::string_view> labels;
        if (hasNextPuzzle) {
            labels.push_back("NEXT PUZZLE");
        }
        labels.push_back("LEVEL SELECT");
        labels.push_back("RETRY");
        AddVerticalEntries(layout, labels, 280.0f);
        break;
    }
    case GameScreen::Playing:
        break;
    }
    return layout;
}

void QueueText(
    FontSystem& fontSystem, const FontHandle font,
    const std::string_view text, const Vec2 position,
    const std::uint32_t pixelSize,
    const TextHorizontalAlignment horizontalAlignment =
        TextHorizontalAlignment::Left,
    const TextVerticalAlignment verticalAlignment =
        TextVerticalAlignment::Top,
    const Color color = {}) {
    static_cast<void>(fontSystem.DrawText(
        font, text,
        {
            .position = position,
            .pixelSize = pixelSize,
            .color = color,
            .horizontalAlignment = horizontalAlignment,
            .verticalAlignment = verticalAlignment,
        }));
}

[[nodiscard]] std::string LengthText(const float value) {
    const long rounded = std::lround((std::max)(0.0f, value));
    return std::to_string(rounded);
}

[[nodiscard]] std::uint32_t GetLevelNumberPixelSize(
    FontSystem& fontSystem, const FontHandle font,
    const std::string_view label) {
    if (label.empty()) {
        return kLevelNumberMaximumPixelSize;
    }
    constexpr float kMaximumTextWidth = kLevelNumberBackdropWidth - 10.0f;
    const TextMetrics metrics = fontSystem.MeasureText(
        font, kLevelNumberMaximumPixelSize, label);
    if (!(metrics.width > kMaximumTextWidth)) {
        return kLevelNumberMaximumPixelSize;
    }
    const float scaledSize =
        static_cast<float>(kLevelNumberMaximumPixelSize) *
        kMaximumTextWidth / metrics.width;
    return (std::max)(1u, static_cast<std::uint32_t>(std::floor(scaledSize)));
}

} // namespace

struct GameUiRenderer::Impl final {
    FontSystem* fontSystem = nullptr;
    FontHandle font{};
    std::unique_ptr<KamataEngine::Sprite> background;
    std::unique_ptr<KamataEngine::Sprite> selection;
    std::array<std::unique_ptr<KamataEngine::Sprite>,
               kLevelDecorationSpecs.size()>
        levelDecorations;
    std::array<std::unique_ptr<KamataEngine::Sprite>, kLevelsPerPage>
        levelTiles;
    std::array<std::unique_ptr<KamataEngine::Sprite>, kLevelsPerPage>
        levelNumberBackdrops;
    std::unique_ptr<KamataEngine::Sprite> levelBackTile;
    std::unique_ptr<KamataEngine::Sprite> levelBackBackdrop;
    std::array<std::unique_ptr<KamataEngine::Sprite>, 2> levelPageTiles;
    std::array<std::unique_ptr<KamataEngine::Sprite>, 2>
        levelPageBackdrops;
    std::unique_ptr<KamataEngine::Sprite> hudWarning;
    std::unique_ptr<KamataEngine::Sprite> messageWarning;
    std::string texturePath;
    bool ownsTexture = false;
    std::size_t levelSelectPage = 0;
    std::size_t lastLevelSelectPuzzleCount = 0;
    std::optional<std::size_t> lastLevelSelectSelection;
    std::optional<GameScreen> lastDrawnScreen;
    bool manualLevelPageChange = false;
    std::int64_t levelPageWheelRemainder = 0;

    ~Impl() {
        // Sprites must release their descriptor references before the shared
        // registry is allowed to unload the underlying texture.
        messageWarning.reset();
        hudWarning.reset();
        for (auto& sprite : levelPageBackdrops) {
            sprite.reset();
        }
        for (auto& sprite : levelPageTiles) {
            sprite.reset();
        }
        levelBackBackdrop.reset();
        levelBackTile.reset();
        for (auto& sprite : levelNumberBackdrops) {
            sprite.reset();
        }
        for (auto& sprite : levelTiles) {
            sprite.reset();
        }
        for (auto& sprite : levelDecorations) {
            sprite.reset();
        }
        selection.reset();
        background.reset();
        if (ownsTexture) {
            rendering_detail::TextureHandleRegistry::Release(texturePath);
        }
    }
};

GameUiRenderer::GameUiRenderer() noexcept = default;
GameUiRenderer::~GameUiRenderer() { Finalize(); }

bool GameUiRenderer::Initialize(FontSystem& fontSystem, const FontHandle font,
                                std::string& error) {
    Finalize();
    error.clear();
    if (!fontSystem.IsInitialized()) {
        error = "GameUiRenderer requires an initialized FontSystem.";
        return false;
    }
    if (!font) {
        error = "GameUiRenderer requires a valid UI font handle.";
        return false;
    }
    try {
        auto next = std::make_unique<Impl>();
        next->fontSystem = &fontSystem;
        next->font = font;
        next->texturePath = "white1x1.png";
        const std::uint32_t white =
            rendering_detail::TextureHandleRegistry::Acquire(next->texturePath);
        next->ownsTexture = true;
        next->background.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.03f, 0.01f, 0.02f, 0.94f}));
        next->selection.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.65f, 0.12f, 0.20f, 0.48f}));
        for (auto& sprite : next->levelDecorations) {
            sprite.reset(KamataEngine::Sprite::Create(
                white, {0.0f, 0.0f}, {0.25f, 0.19f, 0.16f, 1.0f}));
        }
        for (auto& sprite : next->levelTiles) {
            sprite.reset(KamataEngine::Sprite::Create(
                white, {0.0f, 0.0f}, {0.94f, 0.90f, 0.82f, 1.0f}));
        }
        for (auto& sprite : next->levelNumberBackdrops) {
            sprite.reset(KamataEngine::Sprite::Create(
                white, {0.0f, 0.0f}, {0.36f, 0.28f, 0.24f, 1.0f}));
        }
        next->levelBackTile.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.94f, 0.90f, 0.82f, 1.0f}));
        next->levelBackBackdrop.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.36f, 0.28f, 0.24f, 1.0f}));
        for (auto& sprite : next->levelPageTiles) {
            sprite.reset(KamataEngine::Sprite::Create(
                white, {0.0f, 0.0f}, {0.94f, 0.90f, 0.82f, 1.0f}));
        }
        for (auto& sprite : next->levelPageBackdrops) {
            sprite.reset(KamataEngine::Sprite::Create(
                white, {0.0f, 0.0f}, {0.36f, 0.28f, 0.24f, 1.0f}));
        }
        next->hudWarning.reset(KamataEngine::Sprite::Create(
            white, {18.0f, 14.0f}, {0.75f, 0.02f, 0.06f, 0.72f}));
        next->messageWarning.reset(KamataEngine::Sprite::Create(
            white, {0.0f, 0.0f}, {0.75f, 0.02f, 0.06f, 0.72f}));
        const auto allSpritesCreated = [](const auto& sprites) {
            return std::all_of(
                sprites.begin(), sprites.end(),
                [](const auto& sprite) { return sprite != nullptr; });
        };
        if (!next->background || !next->selection ||
            !allSpritesCreated(next->levelDecorations) ||
            !allSpritesCreated(next->levelTiles) ||
            !allSpritesCreated(next->levelNumberBackdrops) ||
            !next->levelBackTile || !next->levelBackBackdrop ||
            !allSpritesCreated(next->levelPageTiles) ||
            !allSpritesCreated(next->levelPageBackdrops) ||
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
                          const bool hasNextPuzzle,
                          const bool solvedMenuReady) {
    if (!impl_) {
        return;
    }

    const std::size_t puzzleCount = catalog.GetPuzzles().size();
    if (screen == GameScreen::LevelSelect) {
        impl_->lastLevelSelectPuzzleCount = puzzleCount;
        const bool enteringLevelSelect =
            !impl_->lastDrawnScreen.has_value() ||
            *impl_->lastDrawnScreen != GameScreen::LevelSelect;
        impl_->levelSelectPage = (std::min)(
            impl_->levelSelectPage, GetLevelPageCount(puzzleCount) - 1);
        if (enteringLevelSelect) {
            impl_->levelSelectPage = selectedItem < puzzleCount
                                         ? selectedItem / kLevelsPerPage
                                         : 0;
            impl_->lastLevelSelectSelection = selectedItem;
            impl_->manualLevelPageChange = false;
            impl_->levelPageWheelRemainder = 0;
        } else {
            const bool selectionChanged =
                !impl_->lastLevelSelectSelection.has_value() ||
                *impl_->lastLevelSelectSelection != selectedItem;
            if (selectionChanged && !impl_->manualLevelPageChange &&
                selectedItem < puzzleCount) {
                impl_->levelSelectPage = selectedItem / kLevelsPerPage;
            }
            impl_->lastLevelSelectSelection = selectedItem;
        }
    } else {
        impl_->levelSelectPage = 0;
        impl_->lastLevelSelectPuzzleCount = 0;
        impl_->lastLevelSelectSelection.reset();
        impl_->manualLevelPageChange = false;
        impl_->levelPageWheelRemainder = 0;
    }
    impl_->lastDrawnScreen = screen;

    const MenuLayout layout = MakeMenuLayout(
        screen, puzzleCount, hasNextPuzzle,
        impl_->levelSelectPage);
    const bool showFullBackground = screen == GameScreen::MainMenu ||
                                    screen == GameScreen::LevelSelect;
    const bool showOverlay = screen == GameScreen::Paused ||
                             (screen == GameScreen::Solved && solvedMenuReady);
    if (screen == GameScreen::LevelSelect) {
        impl_->background->SetColor({0.24f, 0.20f, 0.18f, 1.0f});
    } else if (showFullBackground) {
        impl_->background->SetColor({0.03f, 0.01f, 0.02f, 0.96f});
    } else if (showOverlay) {
        impl_->background->SetColor({0.03f, 0.01f, 0.02f, 0.72f});
    }

    const bool menuVisible = screen != GameScreen::Playing &&
                             (screen != GameScreen::Solved || solvedMenuReady);
    const bool showLengthWarning = board != nullptr && board->lengthExhausted &&
                                   !board->solved;
    const auto selectedEntry = std::find_if(
        layout.entries.begin(), layout.entries.end(),
        [selectedItem](const MenuLayout::Entry& entry) {
            return entry.logicalItemIndex == selectedItem;
        });
    const bool hasSelection = menuVisible && selectedEntry != layout.entries.end();
    if (hasSelection) {
        const UiRect& bounds = selectedEntry->bounds;
        const float border = selectedEntry->visual ==
                                     MenuLayout::EntryVisual::Standard
                                 ? 0.0f
                                 : 5.0f;
        impl_->selection->SetPosition({bounds.left - border, bounds.top - border});
        impl_->selection->SetSize(
            {bounds.width + border * 2.0f, bounds.height + border * 2.0f});
    }

    if (board != nullptr && currentPuzzleIndex.has_value() &&
        *currentPuzzleIndex < catalog.GetPuzzles().size()) {
        const PuzzleDefinition& puzzle = catalog.GetPuzzles()[*currentPuzzleIndex];
        for (std::size_t nodeIndex = 0; nodeIndex < puzzle.nodes.size();
             ++nodeIndex) {
            const NodeDefinition& node = puzzle.nodes[nodeIndex];
            const std::optional<Vec2> center = node.GetCenterPosition();
            if (!center.has_value() || node.displayName.empty()) {
                continue;
            }
            const Vec2 nodeSize = node.GetPixelSize();
            const Vec2 labelPosition{
                center->x,
                center->y + nodeSize.y * 0.5f + 4.0f,
            };
            const bool active = nodeIndex < board->nodeStates.size() &&
                                board->nodeStates[nodeIndex].active;
            constexpr Color kLabelShadow{0.10f, 0.04f, 0.05f, 0.92f};
            constexpr Color kActiveLabel{1.0f, 0.93f, 0.82f, 1.0f};
            constexpr Color kInactiveLabel{0.70f, 0.68f, 0.65f, 1.0f};
            QueueText(*impl_->fontSystem, impl_->font, node.displayName,
                      {labelPosition.x + 1.0f, labelPosition.y + 1.0f}, 18,
                      TextHorizontalAlignment::Center,
                      TextVerticalAlignment::Top, kLabelShadow);
            QueueText(*impl_->fontSystem, impl_->font, node.displayName,
                      labelPosition, 18, TextHorizontalAlignment::Center,
                      TextVerticalAlignment::Top,
                      active ? kActiveLabel : kInactiveLabel);
        }
        const std::string remainingText =
            "REMAINING " + LengthText(board->remainingLength) + " / " +
            LengthText(board->totalLength);
        QueueText(*impl_->fontSystem, impl_->font, remainingText,
                  {28.0f, 24.0f}, 24);
        QueueText(*impl_->fontSystem, impl_->font, "R - RETRY",
                  {1252.0f, 28.0f}, 18,
                  TextHorizontalAlignment::Right);
        if (board->lengthExhausted && !board->solved) {
            QueueText(*impl_->fontSystem, impl_->font, "NOT ENOUGH LENGTH",
                      {kUiWidth * 0.5f, 74.0f}, 25,
                      TextHorizontalAlignment::Center);
        }
    }

    switch (screen) {
    case GameScreen::MainMenu:
        QueueText(*impl_->fontSystem, impl_->font, "OBJECT CONNECT",
                  {kUiWidth * 0.5f, 118.0f}, 45,
                  TextHorizontalAlignment::Center);
        QueueText(*impl_->fontSystem, impl_->font, "RESTORE THE FLOW",
                  {kUiWidth * 0.5f, 180.0f}, 23,
                  TextHorizontalAlignment::Center);
        break;
    case GameScreen::LevelSelect:
        QueueText(*impl_->fontSystem, impl_->font, "ステージ選択",
                  {kUiWidth * 0.5f, 78.0f}, 41,
                  TextHorizontalAlignment::Center);
        break;
    case GameScreen::Paused:
        QueueText(*impl_->fontSystem, impl_->font, "PAUSED",
                  {kUiWidth * 0.5f, 100.0f}, 45,
                  TextHorizontalAlignment::Center);
        break;
    case GameScreen::Solved:
        if (solvedMenuReady) {
            QueueText(*impl_->fontSystem, impl_->font, "FLOW RESTORED",
                      {kUiWidth * 0.5f, 112.0f}, 41,
                      TextHorizontalAlignment::Center);
        }
        break;
    case GameScreen::Playing:
        break;
    }

    if (menuVisible) {
        for (const MenuLayout::Entry& entry : layout.entries) {
            const UiRect& bounds = entry.bounds;
            if (entry.visual == MenuLayout::EntryVisual::LevelTile) {
                const std::uint32_t pixelSize = GetLevelNumberPixelSize(
                    *impl_->fontSystem, impl_->font, entry.label);
                QueueText(*impl_->fontSystem, impl_->font, entry.label,
                          {bounds.left + bounds.width * 0.5f,
                           bounds.top + bounds.height * 0.5f},
                          pixelSize, TextHorizontalAlignment::Center,
                          TextVerticalAlignment::Middle);
                continue;
            }
            if (entry.visual == MenuLayout::EntryVisual::LevelBack) {
                QueueText(*impl_->fontSystem, impl_->font, entry.label,
                          {bounds.left + bounds.width * 0.5f,
                           bounds.top + bounds.height * 0.5f},
                          19, TextHorizontalAlignment::Center,
                          TextVerticalAlignment::Middle);
                continue;
            }
            if (entry.logicalItemIndex == selectedItem) {
                QueueText(*impl_->fontSystem, impl_->font, ">",
                          {bounds.left + 18.0f,
                           bounds.top + bounds.height * 0.5f},
                          kMenuTextPixelSize, TextHorizontalAlignment::Left,
                          TextVerticalAlignment::Middle);
            }
            QueueText(*impl_->fontSystem, impl_->font, entry.label,
                      {bounds.left + 58.0f,
                       bounds.top + bounds.height * 0.5f},
                      kMenuTextPixelSize, TextHorizontalAlignment::Left,
                      TextVerticalAlignment::Middle);
        }
        if (screen == GameScreen::LevelSelect) {
            const std::string pageText =
                "PAGE " + std::to_string(layout.levelPage + 1) + " / " +
                std::to_string(layout.levelPageCount);
            QueueText(*impl_->fontSystem, impl_->font, pageText,
                      {kUiWidth * 0.5f, 508.0f}, 17,
                      TextHorizontalAlignment::Center);
            QueueText(*impl_->fontSystem, impl_->font, "<",
                      {kPreviousPageBounds.left +
                           kPreviousPageBounds.width * 0.5f,
                       kPreviousPageBounds.top +
                           kPreviousPageBounds.height * 0.5f},
                      41, TextHorizontalAlignment::Center,
                      TextVerticalAlignment::Middle);
            QueueText(*impl_->fontSystem, impl_->font, ">",
                      {kNextPageBounds.left + kNextPageBounds.width * 0.5f,
                       kNextPageBounds.top + kNextPageBounds.height * 0.5f},
                      41, TextHorizontalAlignment::Center,
                      TextVerticalAlignment::Middle);
            QueueText(
                *impl_->fontSystem, impl_->font,
                "W/S OR UP/DOWN - SELECT    ENTER/LEFT CLICK - CONFIRM",
                {kUiWidth * 0.5f, 684.0f}, 17,
                TextHorizontalAlignment::Center);
        } else {
            QueueText(
                *impl_->fontSystem, impl_->font,
                "W/S OR UP/DOWN - SELECT    ENTER/LEFT CLICK - CONFIRM",
                {kUiWidth * 0.5f, 662.0f}, 17,
                TextHorizontalAlignment::Center);
        }
    }

    KamataEngine::Sprite::PreDraw(
        KamataEngine::DirectXCommon::GetInstance()->GetCommandList(),
        KamataEngine::Sprite::BlendMode::kNormal);
    if (showFullBackground || showOverlay) {
        impl_->background->Draw();
    }
    if (screen == GameScreen::LevelSelect) {
        // The level selector is built entirely from the shared white sprite:
        // nested earth-tone rectangles provide the frame and title plaque.
        for (std::size_t index = 0; index < kLevelDecorationSpecs.size();
             ++index) {
            const DecorationSpec& spec = kLevelDecorationSpecs[index];
            KamataEngine::Sprite& sprite =
                *impl_->levelDecorations[index];
            sprite.SetColor({spec.color.red, spec.color.green,
                             spec.color.blue, spec.color.alpha});
            sprite.SetPosition({spec.bounds.left, spec.bounds.top});
            sprite.SetSize({spec.bounds.width, spec.bounds.height});
            sprite.Draw();
        }
    }
    if (hasSelection &&
        selectedEntry->visual != MenuLayout::EntryVisual::Standard) {
        impl_->selection->Draw();
    }
    if (screen == GameScreen::LevelSelect) {
        std::size_t levelTileSpriteIndex = 0;
        for (const MenuLayout::Entry& entry : layout.entries) {
            if (entry.visual == MenuLayout::EntryVisual::Standard) {
                continue;
            }
            const UiRect& bounds = entry.bounds;
            KamataEngine::Sprite* tile = nullptr;
            KamataEngine::Sprite* backdrop = nullptr;
            if (entry.visual == MenuLayout::EntryVisual::LevelTile) {
                if (levelTileSpriteIndex >= impl_->levelTiles.size()) {
                    continue;
                }
                tile = impl_->levelTiles[levelTileSpriteIndex].get();
                backdrop =
                    impl_->levelNumberBackdrops[levelTileSpriteIndex].get();
                ++levelTileSpriteIndex;
            } else {
                tile = impl_->levelBackTile.get();
                backdrop = impl_->levelBackBackdrop.get();
            }
            tile->SetColor({0.94f, 0.90f, 0.82f, 1.0f});
            tile->SetPosition({bounds.left, bounds.top});
            tile->SetSize({bounds.width, bounds.height});
            tile->Draw();

            const float backdropWidth =
                entry.visual == MenuLayout::EntryVisual::LevelTile
                    ? kLevelNumberBackdropWidth
                    : bounds.width - 18.0f;
            const float backdropHeight =
                entry.visual == MenuLayout::EntryVisual::LevelTile
                    ? kLevelNumberBackdropHeight
                    : bounds.height - 22.0f;
            backdrop->SetColor({0.36f, 0.28f, 0.24f, 1.0f});
            backdrop->SetPosition({
                bounds.left + (bounds.width - backdropWidth) * 0.5f,
                bounds.top + (bounds.height - backdropHeight) * 0.5f,
            });
            backdrop->SetSize({backdropWidth, backdropHeight});
            backdrop->Draw();
        }

        const bool canGoToPreviousPage = layout.levelPage > 0;
        const bool canGoToNextPage =
            layout.levelPage + 1 < layout.levelPageCount;
        const auto drawPageButton =
            [this](const std::size_t index, const UiRect bounds,
                   const bool enabled) {
                const float brightness = enabled ? 1.0f : 0.58f;
                KamataEngine::Sprite& tile =
                    *impl_->levelPageTiles[index];
                KamataEngine::Sprite& backdrop =
                    *impl_->levelPageBackdrops[index];
                tile.SetColor(
                    {0.94f * brightness, 0.90f * brightness,
                     0.82f * brightness, 1.0f});
                tile.SetPosition({bounds.left, bounds.top});
                tile.SetSize({bounds.width, bounds.height});
                tile.Draw();
                backdrop.SetColor(
                    {0.36f * brightness, 0.28f * brightness,
                     0.24f * brightness, 1.0f});
                backdrop.SetPosition(
                    {bounds.left + 9.0f, bounds.top + 11.0f});
                backdrop.SetSize(
                    {bounds.width - 18.0f, bounds.height - 22.0f});
                backdrop.Draw();
            };
        drawPageButton(0, *layout.previousPage, canGoToPreviousPage);
        drawPageButton(1, *layout.nextPage, canGoToNextPage);
    }
    if (hasSelection &&
        selectedEntry->visual == MenuLayout::EntryVisual::Standard) {
        impl_->selection->Draw();
    }
    if (showLengthWarning) {
        impl_->hudWarning->Draw();
        impl_->messageWarning->Draw();
    }
    KamataEngine::Sprite::PostDraw();
    impl_->manualLevelPageChange = false;
}

std::optional<std::size_t> GameUiRenderer::HitTest(
    const GameScreen screen, const UiPoint point, const std::size_t puzzleCount,
    const bool hasNextPuzzle, const bool solvedMenuReady) const noexcept {
    if (!impl_ || screen == GameScreen::Playing ||
        (screen == GameScreen::Solved && !solvedMenuReady)) {
        return std::nullopt;
    }

    const MenuLayout layout = MakeMenuLayout(
        screen, puzzleCount, hasNextPuzzle,
        impl_->levelSelectPage);
    for (const MenuLayout::Entry& entry : layout.entries) {
        if (entry.bounds.Contains(point)) {
            return entry.logicalItemIndex;
        }
    }
    return std::nullopt;
}

bool GameUiRenderer::IsPointerOverAction(
    const GameScreen screen, const UiPoint point,
    const std::size_t puzzleCount, const bool hasNextPuzzle,
    const bool solvedMenuReady) const noexcept {
    if (HitTest(screen, point, puzzleCount, hasNextPuzzle, solvedMenuReady)
            .has_value()) {
        return true;
    }
    if (!impl_ || screen != GameScreen::LevelSelect) {
        return false;
    }

    const std::size_t pageCount = GetLevelPageCount(puzzleCount);
    const std::size_t page =
        (std::min)(impl_->levelSelectPage, pageCount - 1);
    return (page > 0 && kPreviousPageBounds.Contains(point)) ||
           (page + 1 < pageCount && kNextPageBounds.Contains(point));
}

std::optional<std::size_t> GameUiRenderer::ApplyLevelSelectNavigation(
    const UiPoint point, const bool mousePrimaryPressed, const int wheelDelta,
    const bool keyboardNavigated, const bool activationRequested,
    const GameScreen screen) noexcept {
    if (!impl_ || screen != GameScreen::LevelSelect ||
        !impl_->lastDrawnScreen.has_value() ||
        *impl_->lastDrawnScreen != GameScreen::LevelSelect) {
        return std::nullopt;
    }

    const std::size_t pageCount =
        GetLevelPageCount(impl_->lastLevelSelectPuzzleCount);
    impl_->levelSelectPage =
        (std::min)(impl_->levelSelectPage, pageCount - 1);
    const std::size_t previousPage = impl_->levelSelectPage;

    bool pageButtonClicked = false;
    int pageButtonDirection = 0;
    if (mousePrimaryPressed) {
        if (kPreviousPageBounds.Contains(point)) {
            pageButtonClicked = true;
            pageButtonDirection = -1;
        } else if (kNextPageBounds.Contains(point)) {
            pageButtonClicked = true;
            pageButtonDirection = 1;
        }
    }

    if (pageButtonClicked) {
        // A page button consumes the frame even at the first/last page. This
        // keeps a simultaneous wheel event from changing pages unexpectedly.
        impl_->levelPageWheelRemainder = 0;
        if (pageButtonDirection < 0 && impl_->levelSelectPage > 0) {
            --impl_->levelSelectPage;
        } else if (pageButtonDirection > 0 &&
                   impl_->levelSelectPage + 1 < pageCount) {
            ++impl_->levelSelectPage;
        }
    } else {
        if (keyboardNavigated || activationRequested) {
            impl_->levelPageWheelRemainder = 0;
            return std::nullopt;
        }
        if (wheelDelta == 0) {
            return std::nullopt;
        }

        impl_->levelPageWheelRemainder +=
            static_cast<std::int64_t>(wheelDelta);
        const std::int64_t pageDelta =
            impl_->levelPageWheelRemainder / kWheelDeltaPerPage;
        impl_->levelPageWheelRemainder %= kWheelDeltaPerPage;
        if (pageDelta > 0) {
            const std::size_t magnitude =
                static_cast<std::size_t>(pageDelta);
            impl_->levelSelectPage = magnitude >= impl_->levelSelectPage
                                         ? 0
                                         : impl_->levelSelectPage - magnitude;
        } else if (pageDelta < 0) {
            const std::uint64_t magnitude =
                static_cast<std::uint64_t>(-(pageDelta + 1)) + 1u;
            const std::size_t maximumPage = pageCount - 1;
            const std::size_t remainingPages =
                maximumPage - impl_->levelSelectPage;
            impl_->levelSelectPage =
                magnitude >= remainingPages
                    ? maximumPage
                    : impl_->levelSelectPage +
                          static_cast<std::size_t>(magnitude);
        }
    }

    if (impl_->levelSelectPage == previousPage) {
        return std::nullopt;
    }
    impl_->manualLevelPageChange = true;
    return impl_->levelSelectPage * kLevelsPerPage;
}

void GameUiRenderer::Finalize() noexcept { impl_.reset(); }
bool GameUiRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace object_connect
