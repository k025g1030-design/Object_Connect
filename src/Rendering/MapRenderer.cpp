#include "RetroFPS/Rendering/MapRenderer.hpp"

#include "RetroFPS/Rendering/MapGeometry.hpp"
#include "RetroFPS/Rendering/MapRenderAssets.hpp"

#include <KamataEngine.h>

#include <array>
#include <exception>
#include <filesystem>
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
           ValidateModelAssets(assets.wallModelName, error);
}

[[nodiscard]] KamataEngine::Vector3 ToEngineVector(const Float3& value) noexcept {
    return {value.x, value.y, value.z};
}

} // namespace

struct MapRenderer::Impl {
    // Object3d は Model への非所有ポインタを保持する。メンバーは宣言と逆順に
    // 破棄されるため、共有モデルより先にすべてのオブジェクトが破棄される。
    std::unique_ptr<KamataEngine::Model> floorModel;
    std::unique_ptr<KamataEngine::Model> wallModel;
    std::vector<std::unique_ptr<KamataEngine::Object3d>> objects;
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

    if (!ValidateAssets(assets, error)) {
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

        implementation->objects.reserve(geometry.surfaces.size());
        for (const SurfaceInstance& surface : geometry.surfaces) {
            KamataEngine::Model* model = nullptr;
            switch (surface.type) {
            case SurfaceType::Floor:
                model = implementation->floorModel.get();
                break;
            case SurfaceType::Wall:
                model = implementation->wallModel.get();
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
            implementation->objects.push_back(std::move(object));
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

    for (const std::unique_ptr<KamataEngine::Object3d>& object : impl_->objects) {
        object->Draw(camera);
    }

    KamataEngine::Model::PostDraw();
}

void MapRenderer::Finalize() noexcept {
    impl_.reset();
}

bool MapRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace fps
