#include "RetroFPS/Rendering/GameUiRenderer.hpp"

#include <2d/DebugText.h>
#include <2d/Sprite.h>
#include <base/DirectXCommon.h>
#include <base/TextureManager.h>
#include <base/WinApp.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <string_view>

namespace fps {
namespace {

struct UiRect final {
    float left;
    float top;
    float width;
    float height;

    [[nodiscard]] bool Contains(const UiPoint point) const noexcept {
        return point.x >= left && point.x < left + width &&
               point.y >= top && point.y < top + height;
    }
};

struct MenuItem final {
    std::string_view label;
    UiRect bounds;
};

constexpr float kMenuLeft = 400.0f;
constexpr float kMenuWidth = 480.0f;
constexpr float kItemHeight = 54.0f;

constexpr std::array<MenuItem, 3> kMainMenuItems = {{
    {"START GAME", {kMenuLeft, 280.0f, kMenuWidth, kItemHeight}},
    {"CONTROLS", {kMenuLeft, 352.0f, kMenuWidth, kItemHeight}},
    {"EXIT GAME", {kMenuLeft, 424.0f, kMenuWidth, kItemHeight}},
}};

constexpr std::array<MenuItem, 1> kControlsItems = {{
    {"BACK", {kMenuLeft, 560.0f, kMenuWidth, kItemHeight}},
}};

constexpr std::array<MenuItem, 3> kPauseMenuItems = {{
    {"RESUME", {kMenuLeft, 280.0f, kMenuWidth, kItemHeight}},
    {"MAIN MENU", {kMenuLeft, 352.0f, kMenuWidth, kItemHeight}},
    {"EXIT GAME", {kMenuLeft, 424.0f, kMenuWidth, kItemHeight}},
}};

[[nodiscard]] std::span<const MenuItem> GetItems(const GameUiScreen screen) noexcept {
    switch (screen) {
    case GameUiScreen::MainMenu:
        return kMainMenuItems;
    case GameUiScreen::Controls:
        return kControlsItems;
    case GameUiScreen::PauseMenu:
        return kPauseMenuItems;
    default:
        return {};
    }
}

void PrintText(
    KamataEngine::DebugText& debugText,
    const std::string_view text,
    const float x,
    const float y,
    const float scale) {
    debugText.Print(std::string{text}, x, y, scale);
}

void QueueScreenText(
    KamataEngine::DebugText& debugText,
    const GameUiScreen screen,
    const std::size_t selectedIndex) {
    switch (screen) {
    case GameUiScreen::MainMenu:
        PrintText(debugText, "OBJECT FPS", 485.0f, 125.0f, 2.5f);
        break;
    case GameUiScreen::Controls:
        PrintText(debugText, "CONTROLS", 505.0f, 90.0f, 2.2f);
        PrintText(debugText, "WASD       MOVE", 445.0f, 205.0f, 1.5f);
        PrintText(debugText, "MOUSE      LOOK", 445.0f, 255.0f, 1.5f);
        PrintText(debugText, "ESC        PAUSE / RESUME", 445.0f, 305.0f, 1.5f);
        PrintText(debugText, "ESC/ENTER  BACK", 445.0f, 380.0f, 1.25f);
        break;
    case GameUiScreen::PauseMenu:
        PrintText(debugText, "PAUSED", 535.0f, 130.0f, 2.5f);
        break;
    default:
        break;
    }

    const std::span<const MenuItem> items = GetItems(screen);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const MenuItem& item = items[index];
        if (index == selectedIndex) {
            PrintText(debugText, ">", item.bounds.left + 16.0f, item.bounds.top + 10.0f, 1.7f);
        }
        PrintText(
            debugText,
            item.label,
            item.bounds.left + 54.0f,
            item.bounds.top + 10.0f,
            1.7f);
    }

    if (screen != GameUiScreen::Controls) {
        PrintText(
            debugText,
            "UP/DOWN OR W/S - SELECT    ENTER/LEFT CLICK - CONFIRM",
            305.0f,
            640.0f,
            1.0f);
    }
}

} // namespace

struct GameUiRenderer::Impl final {
    KamataEngine::DebugText* debugText = nullptr;
    std::unique_ptr<KamataEngine::Sprite> background;
    std::unique_ptr<KamataEngine::Sprite> selection;
};

GameUiRenderer::GameUiRenderer() noexcept = default;

GameUiRenderer::~GameUiRenderer() {
    Finalize();
}

bool GameUiRenderer::Initialize(std::string& error) {
    Finalize();
    error.clear();

    try {
        auto next = std::make_unique<Impl>();
        next->debugText = KamataEngine::DebugText::GetInstance();

        const uint32_t whiteTexture = KamataEngine::TextureManager::Load("white1x1.png");
        next->background.reset(KamataEngine::Sprite::Create(
            whiteTexture, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.92f}));
        next->selection.reset(KamataEngine::Sprite::Create(
            whiteTexture, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.22f}));
        if (next->debugText == nullptr || !next->background || !next->selection) {
            error = "KamataEngine failed to create the game UI resources.";
            return false;
        }

        next->background->SetSize({
            static_cast<float>(KamataEngine::WinApp::kWindowWidth),
            static_cast<float>(KamataEngine::WinApp::kWindowHeight),
        });
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize the game UI: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize the game UI because of an unknown error.";
    }

    Finalize();
    return false;
}

void GameUiRenderer::Draw(
    const GameUiScreen screen, const std::size_t selectedIndex) const {
    if (!impl_) {
        return;
    }

    const float backgroundAlpha = screen == GameUiScreen::PauseMenu ? 0.62f : 0.92f;
    impl_->background->SetColor({0.0f, 0.0f, 0.0f, backgroundAlpha});

    const std::span<const MenuItem> items = GetItems(screen);
    const bool hasSelection = selectedIndex < items.size();
    if (hasSelection) {
        const UiRect& bounds = items[selectedIndex].bounds;
        impl_->selection->SetPosition({bounds.left, bounds.top});
        impl_->selection->SetSize({bounds.width, bounds.height});
    }

    QueueScreenText(*impl_->debugText, screen, selectedIndex);

    KamataEngine::Sprite::PreDraw(
        KamataEngine::DirectXCommon::GetInstance()->GetCommandList(),
        KamataEngine::Sprite::BlendMode::kNormal);
    impl_->background->Draw();
    if (hasSelection) {
        impl_->selection->Draw();
    }
    impl_->debugText->DrawAll();
    KamataEngine::Sprite::PostDraw();
}

void GameUiRenderer::Finalize() noexcept {
    impl_.reset();
}

std::optional<std::size_t> GameUiRenderer::HitTest(
    const GameUiScreen screen, const UiPoint point) const noexcept {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        return std::nullopt;
    }

    const std::span<const MenuItem> items = GetItems(screen);
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (items[index].bounds.Contains(point)) {
            return index;
        }
    }
    return std::nullopt;
}

std::size_t GameUiRenderer::GetItemCount(const GameUiScreen screen) noexcept {
    return GetItems(screen).size();
}

bool GameUiRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace fps
