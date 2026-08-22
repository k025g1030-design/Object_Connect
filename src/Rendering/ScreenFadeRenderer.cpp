#include "RetroFPS/Rendering/ScreenFadeRenderer.hpp"

#include <2d/Sprite.h>
#include <base/DirectXCommon.h>
#include <base/TextureManager.h>
#include <base/WinApp.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>

namespace fps {

struct ScreenFadeRenderer::Impl final {
    std::unique_ptr<KamataEngine::Sprite> overlay;
};

ScreenFadeRenderer::ScreenFadeRenderer() noexcept = default;

ScreenFadeRenderer::~ScreenFadeRenderer() { Finalize(); }

bool ScreenFadeRenderer::Initialize(std::string& error) {
    Finalize();
    error.clear();

    try {
        auto next = std::make_unique<Impl>();
        const std::uint32_t whiteTexture = KamataEngine::TextureManager::Load("white1x1.png");
        next->overlay.reset(
            KamataEngine::Sprite::Create(whiteTexture, {0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}));
        if (!next->overlay) {
            error = "KamataEngine failed to create the screen fade resource.";
            return false;
        }

        next->overlay->SetSize({
            static_cast<float>(KamataEngine::WinApp::kWindowWidth),
            static_cast<float>(KamataEngine::WinApp::kWindowHeight),
        });
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize screen fading: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize screen fading because of an unknown error.";
    }

    Finalize();
    return false;
}

void ScreenFadeRenderer::Draw(const float alpha) const {
    if (!impl_ || !std::isfinite(alpha) || alpha <= 0.0f) {
        return;
    }

    const float clampedAlpha = (std::min)(alpha, 1.0f);
    impl_->overlay->SetColor({0.0f, 0.0f, 0.0f, clampedAlpha});

    KamataEngine::Sprite::PreDraw(KamataEngine::DirectXCommon::GetInstance()->GetCommandList(),
                                  KamataEngine::Sprite::BlendMode::kNormal);
    impl_->overlay->Draw();
    KamataEngine::Sprite::PostDraw();
}

void ScreenFadeRenderer::Finalize() noexcept { impl_.reset(); }

bool ScreenFadeRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace fps
