#include "RetroFPS/Rendering/ProjectileRenderer.hpp"

#include <KamataEngine.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fps {

struct ProjectileRenderer::Impl final {
    struct Instance final {
        ProjectileId id = 0;
        ProjectileKind kind = ProjectileKind::PlayerTracer;
        std::unique_ptr<KamataEngine::Object3d> object;
        std::unique_ptr<KamataEngine::ObjectColor> color;
    };

    // Object3d keeps a non-owning Model pointer, so instances are declared last.
    std::unique_ptr<KamataEngine::Model> sphere;
    std::vector<Instance> instances;
};

ProjectileRenderer::ProjectileRenderer() noexcept = default;

ProjectileRenderer::~ProjectileRenderer() { Finalize(); }

bool ProjectileRenderer::Initialize(std::string& error) {
    Finalize();
    error.clear();
    try {
        auto next = std::make_unique<Impl>();
        next->sphere.reset(KamataEngine::Model::CreateSphere(8, 8));
        if (!next->sphere) {
            error = "KamataEngine failed to create the projectile sphere model.";
            return false;
        }
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize projectile rendering: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize projectile rendering because of an unknown error.";
    }
    Finalize();
    return false;
}

void ProjectileRenderer::Sync(const std::span<const ProjectileSnapshot> snapshots) {
    if (!impl_) {
        return;
    }

    std::erase_if(impl_->instances, [snapshots](const Impl::Instance& instance) {
        return std::ranges::none_of(snapshots, [&instance](const ProjectileSnapshot& snapshot) {
            return snapshot.id == instance.id;
        });
    });

    for (const ProjectileSnapshot& snapshot : snapshots) {
        if (!std::isfinite(snapshot.position.x) || !std::isfinite(snapshot.position.y) ||
            !std::isfinite(snapshot.position.z) || !std::isfinite(snapshot.radius) ||
            snapshot.radius <= 0.0f) {
            throw std::invalid_argument("Projectile renderer received an invalid snapshot.");
        }
        auto found = std::ranges::find_if(
            impl_->instances, [&snapshot](const Impl::Instance& instance) {
                return instance.id == snapshot.id;
            });
        if (found == impl_->instances.end()) {
            Impl::Instance instance;
            instance.id = snapshot.id;
            instance.kind = snapshot.kind;
            instance.object = std::make_unique<KamataEngine::Object3d>();
            instance.object->Initialize(impl_->sphere.get());
            instance.color = std::make_unique<KamataEngine::ObjectColor>();
            instance.color->Initialize();
            const KamataEngine::Vector4 color =
                snapshot.kind == ProjectileKind::PlayerTracer
                    ? KamataEngine::Vector4{1.0f, 1.0f, 1.0f, 1.0f}
                    : KamataEngine::Vector4{1.0f, 0.42f, 0.0f, 1.0f};
            instance.color->SetColor(color);
            impl_->instances.push_back(std::move(instance));
            found = std::prev(impl_->instances.end());
        }
        if (found->kind != snapshot.kind) {
            throw std::logic_error("Projectile identity changed kind while alive.");
        }
        found->object->SetTranslation(
            {snapshot.position.x, snapshot.position.y, snapshot.position.z});
        found->object->SetScale({snapshot.radius, snapshot.radius, snapshot.radius});
        found->object->Update();
    }
}

void ProjectileRenderer::Draw(const KamataEngine::Camera& camera) const {
    if (!impl_ || impl_->instances.empty()) {
        return;
    }
    KamataEngine::Model::PreDraw(
        KamataEngine::Model::CullingMode::kBack,
        KamataEngine::Model::BlendMode::kNormal,
        KamataEngine::Model::DepthTestMode::kOn);
    for (const Impl::Instance& instance : impl_->instances) {
        instance.object->GetModel()->Draw(
            instance.object->GetWorldTransform(), camera, instance.color.get());
    }
    KamataEngine::Model::PostDraw();
}

void ProjectileRenderer::Finalize() noexcept { impl_.reset(); }

bool ProjectileRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace fps
