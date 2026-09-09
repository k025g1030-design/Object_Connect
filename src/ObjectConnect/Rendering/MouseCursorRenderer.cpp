#include "ObjectConnect/Rendering/MouseCursorRenderer.hpp"

#include "TextureHandleRegistry.hpp"

#include <2d/Sprite.h>
#include <base/DirectXCommon.h>

#include <Windows.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

namespace object_connect {
namespace {

constexpr float kCursorSize = 32.0f;
constexpr std::size_t kIconCount = 3;
constexpr int kMaximumShowCursorAdjustments = 64;

constexpr std::array<const char*, kIconCount> kTexturePaths = {{
    "assets/textures/mouse/mouse_icon_01.png",
    "assets/textures/mouse/mouse_icon_02.png",
    "assets/textures/mouse/mouse_icon_03.png",
}};

[[nodiscard]] constexpr std::size_t ToIndex(
    const MouseCursorIcon icon) noexcept {
    switch (icon) {
    case MouseCursorIcon::Normal:
        return 0;
    case MouseCursorIcon::Interactive:
        return 1;
    case MouseCursorIcon::Holding:
        return 2;
    }
    return 0;
}

[[nodiscard]] constexpr KamataEngine::Vector2 GetAnchorPoint(
    const MouseCursorIcon icon) noexcept {
    switch (icon) {
    case MouseCursorIcon::Interactive:
        return {0.5f, 1.0f / kCursorSize};
    case MouseCursorIcon::Normal:
    case MouseCursorIcon::Holding:
        return {0.5f, 0.5f};
    }
    return {0.5f, 0.5f};
}

class SpriteDrawScope final {
public:
    explicit SpriteDrawScope(ID3D12GraphicsCommandList* const commandList) {
        KamataEngine::Sprite::PreDraw(
            commandList, KamataEngine::Sprite::BlendMode::kNormal);
    }

    ~SpriteDrawScope() { KamataEngine::Sprite::PostDraw(); }

    SpriteDrawScope(const SpriteDrawScope&) = delete;
    SpriteDrawScope& operator=(const SpriteDrawScope&) = delete;
};

} // namespace

struct MouseCursorRenderer::Impl final {
    KamataEngine::DirectXCommon* directX = nullptr;
    std::array<std::string, kIconCount> texturePaths{};
    std::array<std::uint32_t, kIconCount> textureHandles{};
    std::array<bool, kIconCount> ownsTextures{};
    std::array<std::unique_ptr<KamataEngine::Sprite>, kIconCount> sprites{};
    MouseCursorIcon icon = MouseCursorIcon::Normal;
    float positionX = 0.0f;
    float positionY = 0.0f;
    int showCursorHideCallCount = 0;
    bool drawEnabled = false;

    ~Impl() {
        RestoreSystemCursor();
        for (auto& sprite : sprites) {
            sprite.reset();
        }
        for (std::size_t index = 0; index < texturePaths.size(); ++index) {
            if (ownsTextures[index]) {
                rendering_detail::TextureHandleRegistry::Release(
                    texturePaths[index]);
                ownsTextures[index] = false;
            }
        }
    }

    [[nodiscard]] bool HideSystemCursor() noexcept {
        if (showCursorHideCallCount > 0) {
            return true;
        }

        // ShowCursor uses a per-thread display counter rather than a boolean.
        // Record every decrement so Finalize can restore the exact prior value.
        for (int attempt = 0; attempt < kMaximumShowCursorAdjustments;
             ++attempt) {
            const int displayCount = ::ShowCursor(FALSE);
            ++showCursorHideCallCount;
            if (displayCount < 0) {
                return true;
            }
        }

        // A foreign owner left the counter at a pathological value. Avoid an
        // unbounded loop and roll back our calls rather than risk two cursors.
        RestoreSystemCursor();
        return false;
    }

    void RestoreSystemCursor() noexcept {
        while (showCursorHideCallCount > 0) {
            static_cast<void>(::ShowCursor(TRUE));
            --showCursorHideCallCount;
        }
        drawEnabled = false;
    }
};

MouseCursorRenderer::MouseCursorRenderer() noexcept = default;
MouseCursorRenderer::~MouseCursorRenderer() { Finalize(); }

bool MouseCursorRenderer::Initialize(std::string& error) {
    Finalize();
    error.clear();

    try {
        auto next = std::make_unique<Impl>();
        next->directX = KamataEngine::DirectXCommon::GetInstance();
        if (next->directX == nullptr || !next->directX->IsInitialized()) {
            error = "MouseCursorRenderer requires initialized DirectX.";
            return false;
        }

        for (std::size_t index = 0; index < kTexturePaths.size(); ++index) {
            next->texturePaths[index] = kTexturePaths[index];
            next->textureHandles[index] =
                rendering_detail::TextureHandleRegistry::Acquire(
                    next->texturePaths[index]);
            next->ownsTextures[index] = true;
        }

        for (std::size_t index = 0; index < next->sprites.size(); ++index) {
            const MouseCursorIcon icon =
                index == 1 ? MouseCursorIcon::Interactive
                           : (index == 2 ? MouseCursorIcon::Holding
                                         : MouseCursorIcon::Normal);
            next->sprites[index].reset(KamataEngine::Sprite::Create(
                next->textureHandles[index], {0.0f, 0.0f},
                {1.0f, 1.0f, 1.0f, 1.0f}, GetAnchorPoint(icon)));
            if (!next->sprites[index]) {
                throw std::runtime_error(
                    "KamataEngine could not create a custom cursor sprite.");
            }
            next->sprites[index]->SetSize({kCursorSize, kCursorSize});
        }

        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize MouseCursorRenderer: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize MouseCursorRenderer because of an unknown error.";
    }

    Finalize();
    return false;
}

void MouseCursorRenderer::Update(const InputState& input,
                                 const MouseCursorIcon icon) noexcept {
    if (!impl_) {
        return;
    }

    impl_->icon = icon;
    impl_->positionX = input.mouse.positionX;
    impl_->positionY = input.mouse.positionY;
    const bool canDraw = ShouldUseCustomMouseCursor(
                             input.windowFocused, input.mouse.insideClient) &&
                         std::isfinite(impl_->positionX) &&
                         std::isfinite(impl_->positionY);
    if (!canDraw) {
        impl_->RestoreSystemCursor();
        return;
    }

    impl_->drawEnabled = impl_->HideSystemCursor();
}

void MouseCursorRenderer::Draw() {
    if (!impl_ || !impl_->drawEnabled) {
        return;
    }

    ID3D12GraphicsCommandList* const commandList =
        impl_->directX != nullptr ? impl_->directX->GetCommandList() : nullptr;
    if (commandList == nullptr) {
        impl_->RestoreSystemCursor();
        return;
    }

    KamataEngine::Sprite& sprite = *impl_->sprites[ToIndex(impl_->icon)];
    sprite.SetPosition({impl_->positionX, impl_->positionY});
    SpriteDrawScope drawScope{commandList};
    sprite.Draw();
}

void MouseCursorRenderer::Finalize() noexcept { impl_.reset(); }

bool MouseCursorRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace object_connect
