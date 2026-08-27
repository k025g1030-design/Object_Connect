#include "RetroFPS/Rendering/MapRenderer.hpp"

#include "RetroFPS/Rendering/MapGeometry.hpp"
#include "RetroFPS/Rendering/MapRenderAssets.hpp"

#include <KamataEngine.h>

#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace fps {
namespace {

// KamataEngine::Model::CreateFromOBJ は、次の固定実行時ディレクトリ配下から
// モデルを解決する。エンジンが利用できないパスを公開しないよう、制約をここに集約する。
const std::filesystem::path kModelResourceRoot{"Resources"};

[[nodiscard]] bool IsSimpleModelName(const std::string& name) {
    if (name.empty()) {
        return false;
    }

    const std::filesystem::path path{name};
    return !path.has_root_path() && path == path.filename() && name != "." && name != "..";
}

[[nodiscard]] bool ValidateModelAssets(
    const std::string& modelName,
    std::string& error) {
    if (!IsSimpleModelName(modelName)) {
        error = "Map model names must be non-empty single path components.";
        return false;
    }

    const std::filesystem::path modelDirectory = kModelResourceRoot / modelName;
    const std::array<std::filesystem::path, 2> requiredAssets = {
        modelDirectory / (modelName + ".obj"),
        modelDirectory / (modelName + ".mtl"),
    };

    for (const std::filesystem::path& asset : requiredAssets) {
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(asset, fileError)) {
            error = "Missing required map rendering asset: " + asset.generic_string();
            if (fileError) {
                error += " (" + fileError.message() + ")";
            }
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool ValidateAssets(
    const MapRenderAssets& assets, std::string& error) {
    return ValidateModelAssets(assets.floorModelName, error) &&
           ValidateModelAssets(assets.wallModelName, error) &&
           ValidateModelAssets(assets.doorModelName, error);
}

[[nodiscard]] bool ValidateTextureAsset(
    const std::string& texturePath,
    std::string& error) {
    const std::filesystem::path relativePath{texturePath};
    if (texturePath.empty() || relativePath.has_root_path()) {
        error = "Map texture paths must be non-empty and relative to Resources.";
        return false;
    }

    for (const std::filesystem::path& component : relativePath) {
        if (component == "..") {
            error = "Map texture paths must remain within Resources.";
            return false;
        }
    }

    const std::filesystem::path asset = kModelResourceRoot / relativePath;
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(asset, fileError)) {
        error = "Missing required map rendering asset: " + asset.generic_string();
        if (fileError) {
            error += " (" + fileError.message() + ")";
        }
        return false;
    }

    return true;
}

[[nodiscard]] KamataEngine::Vector3 ToEngineVector(const Float3& value) noexcept {
    return {value.x, value.y, value.z};
}

void MakeUnlitWhite(KamataEngine::Model& model) {
    for (const std::unique_ptr<KamataEngine::Mesh>& mesh : model.GetMeshes()) {
        KamataEngine::Material* material = mesh ? mesh->GetMaterial() : nullptr;
        if (material == nullptr) {
            throw std::runtime_error("Map model contains a mesh without a material.");
        }
        material->ambient_ = {1.0f, 1.0f, 1.0f};
        material->diffuse_ = {0.0f, 0.0f, 0.0f};
        material->specular_ = {0.0f, 0.0f, 0.0f};
        material->alpha_ = 1.0f;
        material->Update();
    }
}

} // namespace

struct MapRenderer::Impl {
    struct RenderInstance final {
        std::unique_ptr<KamataEngine::Object3d> object;
        bool usesDoorTexture = false;
    };

    // Object3d は Model への非所有ポインタを保持する。メンバーは宣言と逆順に
    // 破棄されるため、共有モデルより先にすべてのオブジェクトが破棄される。
    std::unique_ptr<KamataEngine::Model> floorModel;
    std::unique_ptr<KamataEngine::Model> wallModel;
    std::unique_ptr<KamataEngine::Model> doorModel;
    std::uint32_t doorTextureHandle = 0;
    bool doorVisible = true;
    std::vector<RenderInstance> objects;
};

MapRenderer::MapRenderer() noexcept = default;

MapRenderer::~MapRenderer() {
    Finalize();
}

bool MapRenderer::Initialize(
    const MapGeometry& geometry,
    const MapRenderAssets& assets,
    std::string& error) {
    Finalize();
    error.clear();

    if (!ValidateAssets(assets, error) ||
        !ValidateTextureAsset(assets.doorTexturePath, error)) {
        return false;
    }

    try {
        auto implementation = std::make_unique<Impl>();
        implementation->floorModel.reset(
            KamataEngine::Model::CreateFromOBJ(assets.floorModelName));
        if (!implementation->floorModel) {
            error = "KamataEngine failed to create the map floor model.";
            return false;
        }

        implementation->wallModel.reset(
            KamataEngine::Model::CreateFromOBJ(assets.wallModelName));
        if (!implementation->wallModel) {
            error = "KamataEngine failed to create the map wall model.";
            return false;
        }

        implementation->doorModel.reset(
            KamataEngine::Model::CreateFromOBJ(assets.doorModelName));
        if (!implementation->doorModel) {
            error = "KamataEngine failed to create the map door model.";
            return false;
        }
        MakeUnlitWhite(*implementation->floorModel);
        MakeUnlitWhite(*implementation->wallModel);
        MakeUnlitWhite(*implementation->doorModel);
        implementation->doorTextureHandle =
            KamataEngine::TextureManager::Load(assets.doorTexturePath);

        implementation->objects.reserve(geometry.surfaces.size());
        for (const SurfaceInstance& surface : geometry.surfaces) {
            KamataEngine::Model* model = nullptr;
            bool usesDoorTexture = false;
            switch (surface.type) {
            case SurfaceType::Floor:
                model = implementation->floorModel.get();
                break;
            case SurfaceType::Wall:
                model = implementation->wallModel.get();
                break;
            case SurfaceType::Door:
                model = implementation->doorModel.get();
                usesDoorTexture = true;
                break;
            default:
                error = "Map geometry contains an unsupported surface type.";
                return false;
            }

            auto object = std::make_unique<KamataEngine::Object3d>();
            object->Initialize(model);
            object->SetTranslation(ToEngineVector(surface.transform.translation));
            object->SetRotation(ToEngineVector(surface.transform.rotationRadians));
            object->SetScale(ToEngineVector(surface.transform.scale));
            object->Update();
            implementation->objects.push_back({std::move(object), usesDoorTexture});
        }

        impl_ = std::move(implementation);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize map rendering: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize map rendering because of an unknown error.";
    }

    Finalize();
    return false;
}

void MapRenderer::Draw(const KamataEngine::Camera& camera) const {
    if (!impl_) {
        return;
    }

    KamataEngine::Model::PreDraw(
        KamataEngine::Model::CullingMode::kNone,
        KamataEngine::Model::BlendMode::kNone,
        KamataEngine::Model::DepthTestMode::kOn);

    for (const Impl::RenderInstance& instance : impl_->objects) {
        if (instance.usesDoorTexture) {
            if (!impl_->doorVisible) {
                continue;
            }
            instance.object->GetModel()->Draw(
                instance.object->GetWorldTransform(),
                camera,
                impl_->doorTextureHandle);
        } else {
            instance.object->Draw(camera);
        }
    }

    KamataEngine::Model::PostDraw();
}

void MapRenderer::SetDoorVisible(const bool visible) noexcept {
    if (impl_) {
        impl_->doorVisible = visible;
    }
}

void MapRenderer::Finalize() noexcept {
    impl_.reset();
}

bool MapRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

bool MapRenderer::IsDoorVisible() const noexcept {
    return impl_ && impl_->doorVisible;
}

} // namespace fps
