#include "RetroFPS/Rendering/FirstPersonCamera.hpp"

#include <3d/Camera.h>

#include <cmath>
#include <exception>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace fps {
namespace {

void ValidateSettings(const CameraSettings& settings) {
    if (!std::isfinite(settings.fieldOfViewDegrees) ||
        settings.fieldOfViewDegrees <= 0.0f ||
        settings.fieldOfViewDegrees >= 180.0f) {
        throw std::invalid_argument("camera field of view must be between 0 and 180 degrees");
    }
    if (!std::isfinite(settings.nearClip) || settings.nearClip <= 0.0f) {
        throw std::invalid_argument("camera near clip must be finite and greater than zero");
    }
    if (!std::isfinite(settings.farClip) || settings.farClip <= settings.nearClip) {
        throw std::invalid_argument("camera far clip must be finite and greater than near clip");
    }
}

void ValidatePose(
    const Float3 position, const float yawRadians, const float pitchRadians) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(position.z) || !std::isfinite(yawRadians) ||
        !std::isfinite(pitchRadians)) {
        throw std::invalid_argument("camera pose must contain only finite values");
    }
}

} // namespace

struct FirstPersonCamera::Impl {
    KamataEngine::Camera camera;
};

FirstPersonCamera::FirstPersonCamera() noexcept = default;

FirstPersonCamera::~FirstPersonCamera() {
    Finalize();
}

bool FirstPersonCamera::Initialize(std::string& error) {
    return Initialize(CameraSettings{}, error);
}

bool FirstPersonCamera::Initialize(
    const CameraSettings& settings, std::string& error) {
    Finalize();
    error.clear();

    try {
        ValidateSettings(settings);

        auto implementation = std::make_unique<Impl>();
        implementation->camera.fovAngleY =
            settings.fieldOfViewDegrees * std::numbers::pi_v<float> / 180.0f;
        implementation->camera.nearZ = settings.nearClip;
        implementation->camera.farZ = settings.farClip;
        implementation->camera.Initialize();
        implementation->camera.translation_ = {0.0f, 0.0f, 0.0f};
        implementation->camera.rotation_ = {0.0f, 0.0f, 0.0f};
        implementation->camera.UpdateMatrix();
        implementation->camera.TransferMatrix();

        impl_ = std::move(implementation);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize the first-person camera: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize the first-person camera because of an unknown error.";
    }

    Finalize();
    return false;
}

void FirstPersonCamera::Sync(
    const Float3 position, const float yawRadians, const float pitchRadians) {
    if (!impl_) {
        throw std::logic_error("first-person camera is not initialized");
    }
    ValidatePose(position, yawRadians, pitchRadians);

    impl_->camera.translation_ = {position.x, position.y, position.z};
    impl_->camera.rotation_ = {pitchRadians, yawRadians, 0.0f};
    impl_->camera.UpdateMatrix();
    impl_->camera.TransferMatrix();
}

void FirstPersonCamera::Finalize() noexcept {
    impl_.reset();
}

bool FirstPersonCamera::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

const KamataEngine::Camera& FirstPersonCamera::GetNativeCamera() const {
    if (!impl_) {
        throw std::logic_error("first-person camera is not initialized");
    }
    return impl_->camera;
}

KamataEngine::Camera& FirstPersonCamera::GetNativeCamera() {
    if (!impl_) {
        throw std::logic_error("first-person camera is not initialized");
    }
    return impl_->camera;
}

} // namespace fps
