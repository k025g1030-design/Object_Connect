#include "RetroFPS/Rendering/SkySphereRenderer.hpp"

#include <KamataEngine.h>

#include <cmath>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace fps {
namespace {

const std::filesystem::path kResourceRoot{"Resources"};
constexpr std::uint32_t kMaximumSphereSegments = 512;

[[nodiscard]] bool ValidateTexturePath(
    const std::string& texturePath,
    std::string& error) {
    const std::filesystem::path relativePath{texturePath};
    if (texturePath.empty() || relativePath.has_root_path()) {
        error = "Sky texture path must be non-empty and relative to Resources.";
        return false;
    }
    for (const std::filesystem::path& component : relativePath) {
        if (component == "..") {
            error = "Sky texture path must remain within Resources.";
            return false;
        }
    }

    std::error_code fileError;
    const std::filesystem::path asset = kResourceRoot / relativePath;
    if (!std::filesystem::is_regular_file(asset, fileError)) {
        error = "Missing sky texture: " + asset.generic_string();
        if (fileError) {
            error += " (" + fileError.message() + ")";
        }
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateSettings(
    const SkyRenderSettings& settings,
    const float cameraFarClip,
    std::string& error) {
    if (!std::isfinite(settings.radius) || settings.radius <= 0.0f) {
        error = "Sky radius must be finite and greater than zero.";
        return false;
    }
    if (!std::isfinite(cameraFarClip) || cameraFarClip <= settings.radius) {
        error = "Camera far clip must be finite and greater than the sky radius.";
        return false;
    }
    if (settings.verticalSegments < 3 || settings.horizontalSegments < 3) {
        error = "Sky sphere must use at least 3 vertical and horizontal segments.";
        return false;
    }
    if (settings.verticalSegments > kMaximumSphereSegments ||
        settings.horizontalSegments > kMaximumSphereSegments) {
        error = "Sky sphere segment counts must not exceed 512 per axis.";
        return false;
    }
    return ValidateTexturePath(settings.texturePath, error);
}

void MakeUnlitWhite(KamataEngine::Model& model) {
    for (const std::unique_ptr<KamataEngine::Mesh>& mesh : model.GetMeshes()) {
        KamataEngine::Material* material = mesh ? mesh->GetMaterial() : nullptr;
        if (material == nullptr) {
            throw std::runtime_error("Sky sphere contains a mesh without a material.");
        }
        material->ambient_ = {1.0f, 1.0f, 1.0f};
        material->diffuse_ = {0.0f, 0.0f, 0.0f};
        material->specular_ = {0.0f, 0.0f, 0.0f};
        material->alpha_ = 1.0f;
        material->Update();
    }
}

void ValidatePosition(const Float3 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z)) {
        throw std::invalid_argument("Sky sphere position must contain finite values.");
    }
}

} // namespace

struct SkySphereRenderer::Impl final {
    std::unique_ptr<KamataEngine::Model> model;
    std::unique_ptr<KamataEngine::Object3d> object;
    std::unique_ptr<KamataEngine::ObjectColor> color;
    std::uint32_t textureHandle = 0;
};

SkySphereRenderer::SkySphereRenderer() noexcept = default;

SkySphereRenderer::~SkySphereRenderer() { Finalize(); }

bool SkySphereRenderer::Initialize(
    const SkyRenderSettings& settings,
    const float cameraFarClip,
    std::string& error) {
    Finalize();
    error.clear();
    if (!ValidateSettings(settings, cameraFarClip, error)) {
        return false;
    }

    try {
        auto next = std::make_unique<Impl>();
        next->model.reset(KamataEngine::Model::CreateSphere(
            settings.verticalSegments, settings.horizontalSegments));
        if (!next->model) {
            error = "KamataEngine failed to create the sky sphere model.";
            return false;
        }
        MakeUnlitWhite(*next->model);
        next->textureHandle = KamataEngine::TextureManager::Load(settings.texturePath);

        next->object = std::make_unique<KamataEngine::Object3d>();
        next->object->Initialize(next->model.get());
        next->object->SetTranslation({0.0f, 0.0f, 0.0f});
        next->object->SetRotation({0.0f, 0.0f, 0.0f});
        next->object->SetScale({settings.radius, settings.radius, settings.radius});
        next->object->Update();

        next->color = std::make_unique<KamataEngine::ObjectColor>();
        next->color->Initialize();
        next->color->SetColor({1.0f, 1.0f, 1.0f, 1.0f});

        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize sky rendering: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize sky rendering because of an unknown error.";
    }

    Finalize();
    return false;
}

void SkySphereRenderer::Sync(const Float3 cameraPosition) {
    if (!impl_) {
        throw std::logic_error("Sky sphere renderer is not initialized.");
    }
    ValidatePosition(cameraPosition);
    impl_->object->SetTranslation(
        {cameraPosition.x, cameraPosition.y, cameraPosition.z});
    impl_->object->Update();
}

void SkySphereRenderer::Draw(const KamataEngine::Camera& camera) const {
    if (!impl_) {
        return;
    }

    KamataEngine::Model::PreDraw(
        KamataEngine::Model::CullingMode::kFront,
        KamataEngine::Model::BlendMode::kNone,
        KamataEngine::Model::DepthTestMode::kReadOnly);
    impl_->model->Draw(
        impl_->object->GetWorldTransform(),
        camera,
        impl_->textureHandle,
        impl_->color.get());
    KamataEngine::Model::PostDraw();
}

void SkySphereRenderer::Finalize() noexcept { impl_.reset(); }

bool SkySphereRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace fps
