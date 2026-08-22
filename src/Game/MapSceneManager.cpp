#include "RetroFPS/Game/MapSceneManager.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace fps {
namespace {

[[nodiscard]] bool IsValidDuration(const float durationSeconds) noexcept {
    return std::isfinite(durationSeconds) && durationSeconds > 0.0f;
}

[[nodiscard]] float NormalizeDeltaSeconds(const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return 0.0f;
    }
    return deltaSeconds;
}

[[nodiscard]] float Smoothstep(const float progress) noexcept {
    const float clamped = std::clamp(progress, 0.0f, 1.0f);
    return clamped * clamped * (3.0f - 2.0f * clamped);
}

} // namespace

bool MapSceneManager::Initialize(std::vector<GridMap> maps,
                                 const MapSceneTransitionSettings settings, std::string& error) {
    error.clear();
    if (maps.empty()) {
        error = "MapSceneManager requires at least one map.";
        return false;
    }
    if (!IsValidDuration(settings.fadeOutSeconds)) {
        error = "MapSceneManager fade-out duration must be finite and greater than "
                "zero.";
        return false;
    }
    if (!IsValidDuration(settings.fadeInSeconds)) {
        error = "MapSceneManager fade-in duration must be finite and greater than "
                "zero.";
        return false;
    }

    maps_ = std::move(maps);
    activeScene_ = {MapSceneDestinationKind::MainMenu, 0};
    destination_.reset();
    settings_ = settings;
    phase_ = MapScenePhase::Idle;
    phaseElapsedSeconds_ = 0.0f;
    fadeOpacity_ = 0.0f;
    return true;
}

bool MapSceneManager::BeginFirstMap() noexcept { return BeginMap(0); }

bool MapSceneManager::BeginMap(const std::size_t mapIndex) noexcept {
    if (mapIndex >= maps_.size()) {
        return false;
    }
    return BeginTransition({MapSceneDestinationKind::Map, mapIndex});
}

bool MapSceneManager::BeginNextOrResults() noexcept {
    if (phase_ != MapScenePhase::Idle || activeScene_.kind != MapSceneDestinationKind::Map) {
        return false;
    }

    const std::size_t currentMapIndex = activeScene_.mapIndex;
    if (currentMapIndex < maps_.size() - 1) {
        return BeginMap(currentMapIndex + 1);
    }
    return BeginResults();
}

bool MapSceneManager::BeginResults() noexcept {
    if (phase_ != MapScenePhase::Idle || activeScene_.kind != MapSceneDestinationKind::Map) {
        return false;
    }
    return BeginTransition({MapSceneDestinationKind::Results, 0});
}

bool MapSceneManager::BeginMainMenu() noexcept {
    if (phase_ != MapScenePhase::Idle || activeScene_.kind == MapSceneDestinationKind::MainMenu) {
        return false;
    }
    return BeginTransition({MapSceneDestinationKind::MainMenu, 0});
}

MapSceneUpdateResult MapSceneManager::Update(const float deltaSeconds) noexcept {
    MapSceneUpdateResult result{};
    const float normalizedDeltaSeconds = NormalizeDeltaSeconds(deltaSeconds);

    switch (phase_) {
    case MapScenePhase::Idle:
    case MapScenePhase::CommitPending:
        break;

    case MapScenePhase::FadingOut: {
        phaseElapsedSeconds_ =
            (std::min)(settings_.fadeOutSeconds, phaseElapsedSeconds_ + normalizedDeltaSeconds);
        fadeOpacity_ = Smoothstep(phaseElapsedSeconds_ / settings_.fadeOutSeconds);
        if (phaseElapsedSeconds_ >= settings_.fadeOutSeconds) {
            phase_ = MapScenePhase::OpaqueHold;
            phaseElapsedSeconds_ = 0.0f;
            fadeOpacity_ = 1.0f;
        }
        break;
    }

    case MapScenePhase::OpaqueHold:
        phase_ = MapScenePhase::CommitPending;
        result.commitRequested = true;
        break;

    case MapScenePhase::FadingIn: {
        phaseElapsedSeconds_ =
            (std::min)(settings_.fadeInSeconds, phaseElapsedSeconds_ + normalizedDeltaSeconds);
        fadeOpacity_ = 1.0f - Smoothstep(phaseElapsedSeconds_ / settings_.fadeInSeconds);
        if (phaseElapsedSeconds_ >= settings_.fadeInSeconds) {
            phase_ = MapScenePhase::ReleaseHold;
            phaseElapsedSeconds_ = 0.0f;
            fadeOpacity_ = 0.0f;
        }
        break;
    }

    case MapScenePhase::ReleaseHold:
        phase_ = MapScenePhase::Idle;
        destination_.reset();
        result.completedThisFrame = true;
        break;
    }

    return result;
}

std::optional<MapSceneDestination> MapSceneManager::GetCommitDestination() const noexcept {
    if (phase_ != MapScenePhase::CommitPending) {
        return std::nullopt;
    }
    return destination_;
}

bool MapSceneManager::CompleteCommit() noexcept {
    if (phase_ != MapScenePhase::CommitPending || !destination_.has_value()) {
        return false;
    }

    if (destination_->kind == MapSceneDestinationKind::Map &&
        destination_->mapIndex >= maps_.size()) {
        return false;
    }
    activeScene_ = *destination_;

    phase_ = MapScenePhase::FadingIn;
    phaseElapsedSeconds_ = 0.0f;
    fadeOpacity_ = 1.0f;
    return true;
}

std::optional<std::size_t> MapSceneManager::GetActiveMapIndex() const noexcept {
    if (activeScene_.kind != MapSceneDestinationKind::Map) {
        return std::nullopt;
    }
    return activeScene_.mapIndex;
}

const GridMap* MapSceneManager::TryGetMap(const std::size_t mapIndex) const noexcept {
    if (mapIndex >= maps_.size()) {
        return nullptr;
    }
    return &maps_[mapIndex];
}

bool MapSceneManager::IsInputLocked() const noexcept { return phase_ != MapScenePhase::Idle; }

bool MapSceneManager::IsSimulationLocked() const noexcept { return phase_ != MapScenePhase::Idle; }

bool MapSceneManager::IsTransitioning() const noexcept { return phase_ != MapScenePhase::Idle; }

bool MapSceneManager::BeginTransition(const MapSceneDestination destination) noexcept {
    if (maps_.empty() || phase_ != MapScenePhase::Idle) {
        return false;
    }

    destination_ = destination;
    phase_ = MapScenePhase::FadingOut;
    phaseElapsedSeconds_ = 0.0f;
    fadeOpacity_ = 0.0f;
    return true;
}

} // namespace fps
