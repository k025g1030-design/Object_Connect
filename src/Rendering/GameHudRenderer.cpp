#include "RetroFPS/Rendering/GameHudRenderer.hpp"

#include <2d/DebugText.h>
#include <2d/Sprite.h>
#include <base/DirectXCommon.h>
#include <base/TextureManager.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace fps {
namespace {

constexpr float kCenterX = 640.0f;
constexpr float kCenterY = 360.0f;
constexpr float kCrosshairLength = 10.0f;
constexpr float kCrosshairThickness = 2.0f;
constexpr float kWeaponRotation = -0.13962634f;
const std::filesystem::path kResourceRoot{"Resources"};

[[nodiscard]] bool ValidateTexturePath(const std::string& path, std::string& error) {
    const std::filesystem::path relative{path};
    if (path.empty() || relative.has_root_path()) {
        error = "Weapon texture path must be non-empty and relative to Resources.";
        return false;
    }
    for (const std::filesystem::path& component : relative) {
        if (component == "..") {
            error = "Weapon texture path must remain within Resources.";
            return false;
        }
    }
    std::error_code fileError;
    const std::filesystem::path absolute = kResourceRoot / relative;
    if (!std::filesystem::is_regular_file(absolute, fileError)) {
        error = "Missing weapon texture asset: " + absolute.generic_string();
        if (fileError) {
            error += " (" + fileError.message() + ")";
        }
        return false;
    }
    return true;
}

[[nodiscard]] std::unique_ptr<KamataEngine::Sprite> MakeSolidSprite(
    const std::uint32_t texture,
    const KamataEngine::Vector2 position,
    const KamataEngine::Vector4 color,
    const KamataEngine::Vector2 size) {
    std::unique_ptr<KamataEngine::Sprite> sprite(
        KamataEngine::Sprite::Create(texture, position, color));
    if (sprite) {
        sprite->SetSize(size);
    }
    return sprite;
}

[[nodiscard]] std::string FormatMinimumTwoDigits(const int value) {
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << (std::max)(0, value);
    return output.str();
}

} // namespace

struct GameHudRenderer::Impl final {
    KamataEngine::DebugText* debugText = nullptr;
    std::unique_ptr<KamataEngine::Sprite> weapon;
    std::unique_ptr<KamataEngine::Sprite> panel;
    std::array<std::unique_ptr<KamataEngine::Sprite>, 4> crosshair;
    std::unique_ptr<KamataEngine::Sprite> reloadBackground;
    std::unique_ptr<KamataEngine::Sprite> reloadFill;
    std::array<std::unique_ptr<KamataEngine::Sprite>, 6> ammoBrackets;
};

GameHudRenderer::GameHudRenderer() noexcept = default;

GameHudRenderer::~GameHudRenderer() { Finalize(); }

bool GameHudRenderer::Initialize(const std::string& weaponTexturePath, std::string& error) {
    Finalize();
    error.clear();
    if (!ValidateTexturePath(weaponTexturePath, error)) {
        return false;
    }

    try {
        auto next = std::make_unique<Impl>();
        next->debugText = KamataEngine::DebugText::GetInstance();
        const std::uint32_t white = KamataEngine::TextureManager::Load("white1x1.png");
        const std::uint32_t weaponTexture =
            KamataEngine::TextureManager::Load(weaponTexturePath);

        const bool placeholder = weaponTexturePath == "white1x1.png";
        next->weapon.reset(KamataEngine::Sprite::Create(
            weaponTexture,
            {1280.0f, 720.0f},
            placeholder ? KamataEngine::Vector4{0.08f, 0.82f, 0.22f, 1.0f}
                        : KamataEngine::Vector4{1.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 1.0f}));
        if (next->weapon) {
            next->weapon->SetSize({420.0f, 180.0f});
            next->weapon->SetRotation(kWeaponRotation);
        }

        next->panel = MakeSolidSprite(
            white, {1020.0f, 606.0f}, {0.0f, 0.0f, 0.0f, 0.68f}, {240.0f, 102.0f});
        for (auto& line : next->crosshair) {
            line = MakeSolidSprite(
                white, {}, {1.0f, 1.0f, 1.0f, 0.95f}, {1.0f, 1.0f});
        }
        next->reloadBackground = MakeSolidSprite(
            white, {560.0f, 408.0f}, {0.08f, 0.08f, 0.08f, 0.90f}, {160.0f, 10.0f});
        next->reloadFill = MakeSolidSprite(
            white, {560.0f, 408.0f}, {0.10f, 0.90f, 0.25f, 1.0f}, {0.0f, 10.0f});

        constexpr std::array<KamataEngine::Vector2, 6> bracketPositions{{
            {1048.0f, 652.0f}, {1048.0f, 652.0f}, {1048.0f, 685.0f},
            {1236.0f, 652.0f}, {1226.0f, 652.0f}, {1226.0f, 685.0f},
        }};
        constexpr std::array<KamataEngine::Vector2, 6> bracketSizes{{
            {2.0f, 35.0f}, {12.0f, 2.0f}, {12.0f, 2.0f},
            {2.0f, 35.0f}, {12.0f, 2.0f}, {12.0f, 2.0f},
        }};
        for (std::size_t index = 0; index < next->ammoBrackets.size(); ++index) {
            next->ammoBrackets[index] = MakeSolidSprite(
                white,
                bracketPositions[index],
                {1.0f, 1.0f, 1.0f, 0.95f},
                bracketSizes[index]);
        }

        const auto missing = [&next]() {
            if (next->debugText == nullptr || !next->weapon || !next->panel ||
                !next->reloadBackground || !next->reloadFill) {
                return true;
            }
            return std::ranges::any_of(next->crosshair, [](const auto& value) { return !value; }) ||
                   std::ranges::any_of(
                       next->ammoBrackets, [](const auto& value) { return !value; });
        };
        if (missing()) {
            error = "KamataEngine failed to create the gameplay HUD resources.";
            return false;
        }

        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize the gameplay HUD: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize the gameplay HUD because of an unknown error.";
    }
    Finalize();
    return false;
}

void GameHudRenderer::Draw(const GameHudState& state) const {
    if (!impl_) {
        return;
    }
    const float gap = std::clamp(
        std::isfinite(state.crosshairGapPixels) ? state.crosshairGapPixels : 8.0f,
        0.0f,
        64.0f);
    const float kick = std::clamp(
        std::isfinite(state.weaponKickPixels) ? state.weaponKickPixels : 0.0f,
        0.0f,
        80.0f);
    const float reloadProgress = std::clamp(
        std::isfinite(state.reloadProgress) ? state.reloadProgress : 0.0f,
        0.0f,
        1.0f);

    impl_->weapon->SetPosition({1280.0f + kick, 720.0f + kick});
    impl_->crosshair[0]->SetPosition(
        {kCenterX - gap - kCrosshairLength, kCenterY - kCrosshairThickness * 0.5f});
    impl_->crosshair[0]->SetSize({kCrosshairLength, kCrosshairThickness});
    impl_->crosshair[1]->SetPosition(
        {kCenterX + gap, kCenterY - kCrosshairThickness * 0.5f});
    impl_->crosshair[1]->SetSize({kCrosshairLength, kCrosshairThickness});
    impl_->crosshair[2]->SetPosition(
        {kCenterX - kCrosshairThickness * 0.5f, kCenterY - gap - kCrosshairLength});
    impl_->crosshair[2]->SetSize({kCrosshairThickness, kCrosshairLength});
    impl_->crosshair[3]->SetPosition(
        {kCenterX - kCrosshairThickness * 0.5f, kCenterY + gap});
    impl_->crosshair[3]->SetSize({kCrosshairThickness, kCrosshairLength});
    impl_->reloadFill->SetSize({160.0f * reloadProgress, 10.0f});

    impl_->debugText->Print(
        "HP " + std::to_string((std::max)(0, state.hitPoints)), 1072.0f, 614.0f, 1.55f);
    impl_->debugText->Print(
        FormatMinimumTwoDigits(state.magazineAmmo) + "/" +
            FormatMinimumTwoDigits(state.reserveAmmo),
        1072.0f,
        654.0f,
        1.55f);
    if (state.reloading) {
        impl_->debugText->Print("RELOADING", 590.0f, 382.0f, 1.15f);
    }

    KamataEngine::Sprite::PreDraw(
        KamataEngine::DirectXCommon::GetInstance()->GetCommandList(),
        KamataEngine::Sprite::BlendMode::kNormal);
    impl_->weapon->Draw();
    impl_->panel->Draw();
    for (const auto& bracket : impl_->ammoBrackets) {
        bracket->Draw();
    }
    for (const auto& line : impl_->crosshair) {
        line->Draw();
    }
    if (state.reloading) {
        impl_->reloadBackground->Draw();
        impl_->reloadFill->Draw();
    }
    impl_->debugText->DrawAll();
    KamataEngine::Sprite::PostDraw();
}

void GameHudRenderer::Finalize() noexcept { impl_.reset(); }

bool GameHudRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace fps
