#include "RetroFPS/Rendering/EnemyBillboardRenderer.hpp"

#include "RetroFPS/Rendering/EnemyRenderSettings.hpp"

#include <KamataEngine.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fps {
namespace {

const std::filesystem::path kResourceRoot{"Resources"};

static_assert(
    offsetof(KamataEngine::Material::ConstBufferData, uvScale) == 48 &&
        offsetof(KamataEngine::Material::ConstBufferData, uvOffset) == 60,
    "Enemy atlas shaders rely on KamataEngine's contiguous material Vector3 layout.");

enum class ClipIndex : std::size_t {
    Idle,
    Moving,
    Attacking,
    Dead,
    Count,
};

[[nodiscard]] bool IsSimpleModelName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    const std::filesystem::path path{name};
    return !path.has_root_path() && path == path.filename() && name != "." && name != "..";
}

[[nodiscard]] bool ValidateRelativeTexturePath(
    const std::string& texturePath, std::string& error) {
    const std::filesystem::path relativePath{texturePath};
    if (texturePath.empty() || relativePath.has_root_path()) {
        error = "Enemy texture paths must be non-empty and relative to Resources.";
        return false;
    }
    for (const std::filesystem::path& component : relativePath) {
        if (component == "..") {
            error = "Enemy texture paths must remain within Resources.";
            return false;
        }
    }

    const std::filesystem::path asset = kResourceRoot / relativePath;
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(asset, fileError)) {
        error = "Missing required enemy rendering asset: " + asset.generic_string();
        if (fileError) {
            error += " (" + fileError.message() + ")";
        }
        return false;
    }
    return true;
}

[[nodiscard]] const EnemyAnimationClipDefinition& GetClip(
    const EnemyAnimationSetDefinition& animations, const ClipIndex index) {
    switch (index) {
    case ClipIndex::Idle:
        return animations.idle;
    case ClipIndex::Moving:
        return animations.moving;
    case ClipIndex::Attacking:
        return animations.attacking;
    case ClipIndex::Dead:
        return animations.dead;
    case ClipIndex::Count:
        break;
    }
    throw std::logic_error("Unsupported enemy animation clip index.");
}

[[nodiscard]] ClipIndex ToClipIndex(const EnemyState state) {
    switch (state) {
    case EnemyState::Idle:
        return ClipIndex::Idle;
    case EnemyState::Moving:
        return ClipIndex::Moving;
    case EnemyState::Attacking:
        return ClipIndex::Attacking;
    case EnemyState::Dead:
        return ClipIndex::Dead;
    }
    throw std::logic_error("Unsupported enemy state.");
}

[[nodiscard]] bool ValidateSettings(
    const EnemyRenderSettings& settings, std::string& error) {
    if (!IsSimpleModelName(settings.billboardModelName)) {
        error = "Enemy billboard model name must be a non-empty single path component.";
        return false;
    }

    const std::filesystem::path modelDirectory =
        kResourceRoot / settings.billboardModelName;
    const std::array<std::filesystem::path, 2> modelAssets = {
        modelDirectory / (settings.billboardModelName + ".obj"),
        modelDirectory / (settings.billboardModelName + ".mtl"),
    };
    for (const std::filesystem::path& asset : modelAssets) {
        std::error_code fileError;
        if (!std::filesystem::is_regular_file(asset, fileError)) {
            error = "Missing required enemy rendering asset: " + asset.generic_string();
            if (fileError) {
                error += " (" + fileError.message() + ")";
            }
            return false;
        }
    }

    if (!std::isfinite(settings.hitFlashBrightness) ||
        settings.hitFlashBrightness <= 0.0f) {
        error = "Enemy hit-flash brightness must be finite and greater than zero.";
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateDefinition(
    const EnemyDefinition& definition, std::string& error) {
    if (definition.id.empty()) {
        error = "Enemy rendering definitions must have a non-empty ID.";
        return false;
    }
    if (!ValidateRelativeTexturePath(definition.texturePath, error)) {
        return false;
    }
    if (!std::isfinite(definition.renderWidth) || definition.renderWidth <= 0.0f ||
        !std::isfinite(definition.renderHeight) || definition.renderHeight <= 0.0f) {
        error = "Enemy '" + definition.id +
                "' render dimensions must be finite and greater than zero.";
        return false;
    }
    if (definition.frameWidthPixels == 0 || definition.frameHeightPixels == 0) {
        error = "Enemy '" + definition.id +
                "' atlas frame dimensions must be greater than zero.";
        return false;
    }

    for (std::size_t index = 0;
         index < static_cast<std::size_t>(ClipIndex::Count); ++index) {
        const EnemyAnimationClipDefinition& clip = GetClip(
            definition.animations, static_cast<ClipIndex>(index));
        if (clip.frameCount == 0 || !std::isfinite(clip.secondsPerFrame) ||
            clip.secondsPerFrame <= 0.0f) {
            error = "Enemy '" + definition.id +
                    "' animation clips require frames and positive finite timing.";
            return false;
        }
    }
    return true;
}

[[nodiscard]] KamataEngine::Vector4 MakeBrightnessColor(const float brightness) noexcept {
    return {brightness, brightness, brightness, 1.0f};
}

} // namespace

struct EnemyBillboardRenderer::Impl final {
    struct LoadedFrame final {
        std::unique_ptr<KamataEngine::Material> material;
    };

    using LoadedClip = std::vector<LoadedFrame>;
    using LoadedClips = std::array<LoadedClip, static_cast<std::size_t>(ClipIndex::Count)>;

    struct LoadedDefinition final {
        EnemyDefinition definition;
        std::uint32_t textureHandle = 0;
        LoadedClips clips{};
    };

    struct RenderInstance final {
        EnemyId id = 0;
        EnemyDefinitionId definitionId;
        EnemyState state = EnemyState::Idle;
        float stateElapsedSeconds = 0.0f;
        float yawRadians = 0.0f;
        std::unique_ptr<KamataEngine::Object3d> object;
        std::unique_ptr<KamataEngine::ObjectColor> color;
    };

    // Object3d stores a non-owning model pointer. Declaration order ensures
    // instances and frame materials are destroyed before the shared model.
    std::unique_ptr<KamataEngine::Model> model;
    std::unordered_map<EnemyDefinitionId, LoadedDefinition> definitions;
    EnemyRenderSettings settings{};
    std::vector<RenderInstance> instances;

    [[nodiscard]] LoadedDefinition& FindDefinition(const EnemyDefinitionId& id) {
        const auto found = definitions.find(id);
        if (found == definitions.end()) {
            throw std::invalid_argument(
                "Enemy billboard snapshot references unknown definition '" + id + "'.");
        }
        return found->second;
    }

    [[nodiscard]] const LoadedDefinition& FindDefinition(
        const EnemyDefinitionId& id) const {
        const auto found = definitions.find(id);
        if (found == definitions.end()) {
            throw std::logic_error(
                "Enemy billboard instance lost definition '" + id + "'.");
        }
        return found->second;
    }

    [[nodiscard]] RenderInstance CreateInstance(const EnemySnapshot& snapshot) {
        const LoadedDefinition& loaded = FindDefinition(snapshot.definitionId);
        if (snapshot.kind != loaded.definition.kind) {
            throw std::invalid_argument(
                "Enemy billboard snapshot kind does not match definition '" +
                snapshot.definitionId + "'.");
        }

        RenderInstance instance;
        instance.id = snapshot.id;
        instance.definitionId = snapshot.definitionId;
        instance.state = snapshot.state;
        instance.stateElapsedSeconds = snapshot.stateElapsedSeconds;
        instance.object = std::make_unique<KamataEngine::Object3d>();
        instance.object->Initialize(model.get());
        instance.color = std::make_unique<KamataEngine::ObjectColor>();
        instance.color->Initialize();
        instance.color->SetColor(MakeBrightnessColor(1.0f));
        return instance;
    }
};

EnemyBillboardRenderer::EnemyBillboardRenderer() noexcept = default;

EnemyBillboardRenderer::~EnemyBillboardRenderer() {
    Finalize();
}

bool EnemyBillboardRenderer::Initialize(
    const std::span<const EnemySnapshot> snapshots,
    const std::span<const EnemyDefinition> definitions,
    const EnemyRenderSettings& settings,
    std::string& error) {
    Finalize();
    error.clear();
    if (!ValidateSettings(settings, error)) {
        return false;
    }

    try {
        auto implementation = std::make_unique<Impl>();
        implementation->settings = settings;
        implementation->model.reset(
            KamataEngine::Model::CreateFromOBJ(settings.billboardModelName));
        if (!implementation->model || implementation->model->GetMeshes().empty()) {
            error = "KamataEngine failed to create the enemy billboard quad model.";
            return false;
        }

        implementation->definitions.reserve(definitions.size());
        for (const EnemyDefinition& definition : definitions) {
            if (!ValidateDefinition(definition, error)) {
                return false;
            }
            if (implementation->definitions.contains(definition.id)) {
                error = "Duplicate enemy rendering definition ID: " + definition.id;
                return false;
            }

            Impl::LoadedDefinition loaded;
            loaded.definition = definition;
            loaded.textureHandle =
                KamataEngine::TextureManager::Load(definition.texturePath);
            const D3D12_RESOURCE_DESC textureDescription =
                KamataEngine::TextureManager::GetInstance()->GetResoureDesc(
                    loaded.textureHandle);
            if (textureDescription.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                textureDescription.Width == 0 || textureDescription.Height == 0) {
                error = "Enemy '" + definition.id +
                        "' texture did not load as a non-empty 2D atlas.";
                return false;
            }

            for (std::size_t clipIndex = 0;
                 clipIndex < static_cast<std::size_t>(ClipIndex::Count); ++clipIndex) {
                const EnemyAnimationClipDefinition& clip = GetClip(
                    definition.animations, static_cast<ClipIndex>(clipIndex));
                Impl::LoadedClip& frames = loaded.clips[clipIndex];
                frames.reserve(clip.frameCount);
                for (std::size_t frameIndex = 0; frameIndex < clip.frameCount;
                     ++frameIndex) {
                    const std::optional<EnemyAtlasUvTransform> uv = ResolveEnemyAtlasUv(
                        clip,
                        definition.frameWidthPixels,
                        definition.frameHeightPixels,
                        frameIndex,
                        textureDescription.Width,
                        textureDescription.Height);
                    if (!uv.has_value()) {
                        error = "Enemy '" + definition.id +
                                "' animation frame lies outside its loaded texture atlas.";
                        return false;
                    }

                    std::unique_ptr<KamataEngine::Material> material =
                        KamataEngine::Material::Create();
                    if (!material) {
                        error = "KamataEngine failed to create an enemy atlas material.";
                        return false;
                    }
                    material->name_ = definition.id + "_atlas_frame_" +
                                      std::to_string(clipIndex) + "_" +
                                      std::to_string(frameIndex);
                    material->ambient_ = {1.0f, 1.0f, 1.0f};
                    material->diffuse_ = {0.0f, 0.0f, 0.0f};
                    material->specular_ = {0.0f, 0.0f, 0.0f};
                    material->alpha_ = 1.0f;
                    material->uvOffset_ = {uv->offsetX, uv->offsetY, 0.0f};
                    material->uvScale_ = {uv->scaleX, uv->scaleY, 1.0f};
                    material->Update();
                    frames.push_back({std::move(material)});
                }
            }

            implementation->definitions.emplace(definition.id, std::move(loaded));
        }

        for (const EnemySnapshot& snapshot : snapshots) {
            const Impl::LoadedDefinition& loaded =
                implementation->FindDefinition(snapshot.definitionId);
            if (snapshot.kind != loaded.definition.kind) {
                error = "Enemy billboard snapshot kind does not match definition '" +
                        snapshot.definitionId + "'.";
                return false;
            }
        }

        impl_ = std::move(implementation);
        Sync(snapshots, {});
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize enemy billboard rendering: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize enemy billboard rendering because of an unknown error.";
    }

    Finalize();
    return false;
}

void EnemyBillboardRenderer::Sync(
    const std::span<const EnemySnapshot> snapshots, const Float2 viewerPosition) {
    if (!impl_) {
        return;
    }
    if (!std::isfinite(viewerPosition.x) || !std::isfinite(viewerPosition.z)) {
        throw std::invalid_argument("Enemy billboard viewer position must be finite.");
    }

    std::erase_if(impl_->instances, [snapshots](const Impl::RenderInstance& instance) {
        return std::ranges::none_of(snapshots, [&instance](const EnemySnapshot& snapshot) {
            return snapshot.id == instance.id;
        });
    });

    for (const EnemySnapshot& snapshot : snapshots) {
        if (!std::isfinite(snapshot.position.x) || !std::isfinite(snapshot.position.z) ||
            !std::isfinite(snapshot.stateElapsedSeconds) ||
            snapshot.stateElapsedSeconds < 0.0f ||
            !std::isfinite(snapshot.hitFlashRemainingSeconds) ||
            snapshot.hitFlashRemainingSeconds < 0.0f) {
            throw std::invalid_argument("Enemy billboard snapshot values must be finite and valid.");
        }

        const Impl::LoadedDefinition& loaded =
            impl_->FindDefinition(snapshot.definitionId);
        if (snapshot.kind != loaded.definition.kind) {
            throw std::invalid_argument(
                "Enemy billboard snapshot kind does not match its definition.");
        }

        auto found = std::ranges::find_if(
            impl_->instances, [&snapshot](const Impl::RenderInstance& instance) {
                return instance.id == snapshot.id;
            });
        if (found == impl_->instances.end()) {
            impl_->instances.push_back(impl_->CreateInstance(snapshot));
            found = std::prev(impl_->instances.end());
        }
        Impl::RenderInstance& instance = *found;
        if (snapshot.definitionId != instance.definitionId) {
            throw std::logic_error(
                "Enemy billboard identity changed definition while alive.");
        }

        const EnemyBillboardPose pose = ResolveEnemyBillboardPose(
            loaded.definition,
            snapshot.position,
            viewerPosition,
            instance.yawRadians);
        instance.yawRadians = pose.yawRadians;
        instance.state = snapshot.state;
        instance.stateElapsedSeconds = snapshot.stateElapsedSeconds;
        const float brightness = snapshot.hitFlashRemainingSeconds > 0.0f
                                     ? impl_->settings.hitFlashBrightness
                                     : 1.0f;
        instance.color->SetColor(MakeBrightnessColor(brightness));
        instance.object->SetTranslation(
            {snapshot.position.x, pose.centerY, snapshot.position.z});
        instance.object->SetRotation({0.0f, instance.yawRadians, 0.0f});
        instance.object->SetScale({pose.width, pose.height, 1.0f});
        instance.object->Update();
    }
}

void EnemyBillboardRenderer::Draw(const KamataEngine::Camera& camera) const {
    if (!impl_ || impl_->instances.empty()) {
        return;
    }

    KamataEngine::Model::PreDraw(
        KamataEngine::Model::CullingMode::kNone,
        KamataEngine::Model::BlendMode::kNormal,
        KamataEngine::Model::DepthTestMode::kOn);
    for (const Impl::RenderInstance& instance : impl_->instances) {
        const Impl::LoadedDefinition& loaded =
            impl_->FindDefinition(instance.definitionId);
        const ClipIndex clipIndex = ToClipIndex(instance.state);
        const EnemyAnimationClipDefinition& clip = GetClip(
            loaded.definition.animations, clipIndex);
        const std::optional<std::size_t> frameIndex = ResolveEnemyAnimationFrame(
            clip, instance.state, instance.stateElapsedSeconds);
        if (!frameIndex.has_value()) {
            continue;
        }
        const Impl::LoadedFrame& frame =
            loaded.clips[static_cast<std::size_t>(clipIndex)][*frameIndex];
        for (const std::unique_ptr<KamataEngine::Mesh>& mesh :
             impl_->model->GetMeshes()) {
            mesh->SetMaterial(frame.material.get());
        }
        impl_->model->Draw(
            instance.object->GetWorldTransform(),
            camera,
            loaded.textureHandle,
            instance.color.get());
    }
    KamataEngine::Model::PostDraw();
}

void EnemyBillboardRenderer::Finalize() noexcept {
    if (!impl_) {
        return;
    }
    impl_->instances.clear();
    impl_->model.reset();
    impl_->definitions.clear();
    impl_.reset();
}

bool EnemyBillboardRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace fps
