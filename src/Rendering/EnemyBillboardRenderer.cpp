#include "RetroFPS/Rendering/EnemyBillboardRenderer.hpp"

#include "RetroFPS/Rendering/EnemyRenderSettings.hpp"

#include <KamataEngine.h>

#include <algorithm>
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
#include <system_error>
#include <utility>
#include <vector>

namespace fps {
namespace {

const std::filesystem::path kResourceRoot{"Resources"};

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

[[nodiscard]] bool IsFiniteUnitColor(const EnemyTint& color) noexcept {
    const auto valid = [](const float component) {
        return std::isfinite(component) && component >= 0.0f && component <= 1.0f;
    };
    return valid(color.red) && valid(color.green) && valid(color.blue) && valid(color.alpha);
}

[[nodiscard]] const EnemyAnimationClipSettings& GetClip(
    const EnemyAnimationSetSettings& set, const ClipIndex index) {
    switch (index) {
    case ClipIndex::Idle:
        return set.idle;
    case ClipIndex::Moving:
        return set.moving;
    case ClipIndex::Attacking:
        return set.attacking;
    case ClipIndex::Dead:
        return set.dead;
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

[[nodiscard]] bool ValidateAnimationSet(
    const EnemyAnimationSetSettings& set, const char* const label, std::string& error) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(ClipIndex::Count); ++index) {
        const auto clipIndex = static_cast<ClipIndex>(index);
        const EnemyAnimationClipSettings& clip = GetClip(set, clipIndex);
        if (!std::isfinite(clip.secondsPerFrame) || clip.secondsPerFrame <= 0.0f) {
            error = std::string{"Enemy "} + label +
                    " animation secondsPerFrame must be finite and greater than zero.";
            return false;
        }
        for (const std::string& texturePath : clip.frameTexturePaths) {
            if (!ValidateRelativeTexturePath(texturePath, error)) {
                return false;
            }
        }
    }
    return true;
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

    if (!ValidateRelativeTexturePath(settings.fallbackTexturePath, error)) {
        return false;
    }
    if (!std::isfinite(settings.billboardWidth) || settings.billboardWidth <= 0.0f ||
        !std::isfinite(settings.rangedHeight) || settings.rangedHeight <= 0.0f ||
        !std::isfinite(settings.meleeHeightScale) || settings.meleeHeightScale <= 0.0f) {
        error = "Enemy billboard dimensions must be finite and greater than zero.";
        return false;
    }
    const float meleeHeight = settings.rangedHeight * settings.meleeHeightScale;
    if (!std::isfinite(meleeHeight) || meleeHeight <= 0.0f) {
        error = "Enemy melee billboard height must be finite and greater than zero.";
        return false;
    }
    if (!IsFiniteUnitColor(settings.meleeTint) ||
        !IsFiniteUnitColor(settings.rangedTint)) {
        error = "Enemy billboard tint components must be finite values from zero to one.";
        return false;
    }
    return ValidateAnimationSet(settings.meleeAnimations, "melee", error) &&
           ValidateAnimationSet(settings.rangedAnimations, "ranged", error);
}

[[nodiscard]] KamataEngine::Vector4 ToEngineColor(const EnemyTint& tint) noexcept {
    return {tint.red, tint.green, tint.blue, tint.alpha};
}

} // namespace

struct EnemyBillboardRenderer::Impl final {
    using LoadedClip = std::vector<std::uint32_t>;
    using LoadedSet = std::array<LoadedClip, static_cast<std::size_t>(ClipIndex::Count)>;

    struct RenderInstance final {
        EnemyId id = 0;
        EnemyKind kind = EnemyKind::Melee;
        EnemyState state = EnemyState::Idle;
        float stateElapsedSeconds = 0.0f;
        float yawRadians = 0.0f;
        std::uint32_t baseTextureHandle = 0;
        std::unique_ptr<KamataEngine::Object3d> object;
        std::unique_ptr<KamataEngine::ObjectColor> color;
    };

    // Object3d stores a non-owning model pointer, so instances must be destroyed first.
    std::unique_ptr<KamataEngine::Model> model;
    std::uint32_t fallbackTextureHandle = 0;
    EnemyRenderSettings settings{};
    LoadedSet meleeFrames{};
    LoadedSet rangedFrames{};
    std::vector<RenderInstance> instances;

    [[nodiscard]] std::uint32_t ResolveTexture(const RenderInstance& instance) const {
        const ClipIndex clipIndex = ToClipIndex(instance.state);
        const std::size_t index = static_cast<std::size_t>(clipIndex);
        const LoadedSet& loaded =
            instance.kind == EnemyKind::Melee ? meleeFrames : rangedFrames;
        if (loaded[index].empty()) {
            return instance.baseTextureHandle != 0 ? instance.baseTextureHandle
                                                   : fallbackTextureHandle;
        }

        const EnemyAnimationClipSettings& clip =
            GetEnemyAnimationClip(settings, instance.kind, instance.state);
        const std::optional<std::size_t> frame = ResolveEnemyAnimationFrame(
            clip, instance.stateElapsedSeconds, loaded[index].size());
        return frame.has_value()
                   ? loaded[index][*frame]
                   : (instance.baseTextureHandle != 0 ? instance.baseTextureHandle
                                                      : fallbackTextureHandle);
    }
};

EnemyBillboardRenderer::EnemyBillboardRenderer() noexcept = default;

EnemyBillboardRenderer::~EnemyBillboardRenderer() {
    Finalize();
}

bool EnemyBillboardRenderer::Initialize(
    const std::span<const EnemySnapshot> snapshots,
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
        if (!implementation->model) {
            error = "KamataEngine failed to create the enemy billboard model.";
            return false;
        }
        implementation->fallbackTextureHandle =
            KamataEngine::TextureManager::Load(settings.fallbackTexturePath);

        const auto loadSet = [](const EnemyAnimationSetSettings& source,
                                Impl::LoadedSet& destination) {
            for (std::size_t index = 0;
                 index < static_cast<std::size_t>(ClipIndex::Count); ++index) {
                const EnemyAnimationClipSettings& clip =
                    GetClip(source, static_cast<ClipIndex>(index));
                destination[index].reserve(clip.frameTexturePaths.size());
                for (const std::string& path : clip.frameTexturePaths) {
                    destination[index].push_back(KamataEngine::TextureManager::Load(path));
                }
            }
        };
        loadSet(settings.meleeAnimations, implementation->meleeFrames);
        loadSet(settings.rangedAnimations, implementation->rangedFrames);

        implementation->instances.reserve(snapshots.size());
        for (const EnemySnapshot& snapshot : snapshots) {
            Impl::RenderInstance instance;
            instance.id = snapshot.id;
            instance.kind = snapshot.kind;
            instance.state = snapshot.state;
            instance.stateElapsedSeconds = snapshot.stateElapsedSeconds;
            instance.baseTextureHandle = snapshot.texturePath.empty()
                                             ? implementation->fallbackTextureHandle
                                             : KamataEngine::TextureManager::Load(
                                                   snapshot.texturePath);
            instance.object = std::make_unique<KamataEngine::Object3d>();
            instance.object->Initialize(implementation->model.get());
            instance.color = std::make_unique<KamataEngine::ObjectColor>();
            instance.color->Initialize();
            instance.color->SetColor(ToEngineColor(
                snapshot.kind == EnemyKind::Melee ? settings.meleeTint : settings.rangedTint));
            implementation->instances.push_back(std::move(instance));
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
            !std::isfinite(snapshot.stateElapsedSeconds) || snapshot.stateElapsedSeconds < 0.0f) {
            throw std::invalid_argument("Enemy billboard snapshot values must be finite and valid.");
        }

        auto found = std::ranges::find_if(
            impl_->instances, [&snapshot](const Impl::RenderInstance& instance) {
                return instance.id == snapshot.id;
            });
        if (found == impl_->instances.end()) {
            Impl::RenderInstance instance;
            instance.id = snapshot.id;
            instance.kind = snapshot.kind;
            instance.state = snapshot.state;
            instance.stateElapsedSeconds = snapshot.stateElapsedSeconds;
            instance.baseTextureHandle = snapshot.texturePath.empty()
                                             ? impl_->fallbackTextureHandle
                                             : KamataEngine::TextureManager::Load(
                                                   snapshot.texturePath);
            instance.object = std::make_unique<KamataEngine::Object3d>();
            instance.object->Initialize(impl_->model.get());
            instance.color = std::make_unique<KamataEngine::ObjectColor>();
            instance.color->Initialize();
            impl_->instances.push_back(std::move(instance));
            found = std::prev(impl_->instances.end());
        }
        Impl::RenderInstance& instance = *found;
        if (snapshot.kind != instance.kind) {
            throw std::logic_error("Enemy billboard identity changed kind while alive.");
        }

        EnemyBillboardPose pose = ResolveEnemyBillboardPose(
            impl_->settings,
            snapshot.kind,
            snapshot.position,
            viewerPosition,
            instance.yawRadians);
        if (std::isfinite(snapshot.hitboxHeight) && snapshot.hitboxHeight > 0.0f) {
            pose.height = snapshot.hitboxHeight;
            pose.centerY = snapshot.hitboxHeight * 0.5f;
        }
        instance.yawRadians = pose.yawRadians;
        instance.state = snapshot.state;
        instance.stateElapsedSeconds = snapshot.stateElapsedSeconds;
        const EnemyTint baseTint = snapshot.kind == EnemyKind::Melee
                                       ? impl_->settings.meleeTint
                                       : impl_->settings.rangedTint;
        instance.color->SetColor(
            snapshot.hitFlashRemainingSeconds > 0.0f
                ? KamataEngine::Vector4{1.0f, 1.0f, 1.0f, 1.0f}
                : ToEngineColor(baseTint));
        instance.object->SetTranslation({snapshot.position.x, pose.centerY, snapshot.position.z});
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
        instance.object->GetModel()->Draw(
            instance.object->GetWorldTransform(),
            camera,
            impl_->ResolveTexture(instance),
            instance.color.get());
    }
    KamataEngine::Model::PostDraw();
}

void EnemyBillboardRenderer::Finalize() noexcept {
    impl_.reset();
}

bool EnemyBillboardRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace fps
