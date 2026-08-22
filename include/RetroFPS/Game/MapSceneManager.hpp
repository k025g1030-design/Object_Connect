#pragma once

#include "RetroFPS/World/GridMap.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace fps {

enum class MapScenePhase {
    Idle,
    FadingOut,
    OpaqueHold,
    CommitPending,
    FadingIn,
    ReleaseHold,
};

enum class MapSceneDestinationKind {
    Map,
    Results,
    MainMenu,
};

struct MapSceneDestination final {
    MapSceneDestinationKind kind = MapSceneDestinationKind::MainMenu;
    // Ignored when kind is Results or MainMenu.
    std::size_t mapIndex = 0;

    [[nodiscard]] bool operator==(const MapSceneDestination&) const noexcept = default;
};

struct MapSceneTransitionSettings final {
    float fadeOutSeconds = 0.4f;
    float fadeInSeconds = 0.4f;
};

struct MapSceneUpdateResult final {
    bool commitRequested = false;
    bool completedThisFrame = false;
};

// Engine-independent campaign-map ownership and scene-transition protocol. The
// caller applies its engine scene while this manager is in CommitPending, then
// acknowledges the successful scene commit.
class MapSceneManager final {
public:
    [[nodiscard]] bool Initialize(std::vector<GridMap> maps, MapSceneTransitionSettings settings,
                                  std::string& error);

    [[nodiscard]] bool BeginFirstMap() noexcept;
    [[nodiscard]] bool BeginMap(std::size_t mapIndex) noexcept;
    [[nodiscard]] bool BeginNextOrResults() noexcept;
    [[nodiscard]] bool BeginResults() noexcept;
    [[nodiscard]] bool BeginMainMenu() noexcept;

    [[nodiscard]] MapSceneUpdateResult Update(float deltaSeconds) noexcept;
    [[nodiscard]] std::optional<MapSceneDestination> GetCommitDestination() const noexcept;
    [[nodiscard]] bool CompleteCommit() noexcept;

    [[nodiscard]] MapSceneDestination GetActiveScene() const noexcept { return activeScene_; }
    [[nodiscard]] std::optional<std::size_t> GetActiveMapIndex() const noexcept;
    [[nodiscard]] const GridMap* TryGetMap(std::size_t mapIndex) const noexcept;
    [[nodiscard]] std::size_t GetMapCount() const noexcept { return maps_.size(); }
    [[nodiscard]] MapScenePhase GetPhase() const noexcept { return phase_; }
    [[nodiscard]] float GetFadeOpacity() const noexcept { return fadeOpacity_; }
    [[nodiscard]] bool IsInputLocked() const noexcept;
    [[nodiscard]] bool IsSimulationLocked() const noexcept;
    [[nodiscard]] bool IsTransitioning() const noexcept;

private:
    [[nodiscard]] bool BeginTransition(MapSceneDestination destination) noexcept;

    std::vector<GridMap> maps_;
    MapSceneDestination activeScene_{};
    std::optional<MapSceneDestination> destination_;
    MapSceneTransitionSettings settings_{};
    MapScenePhase phase_ = MapScenePhase::Idle;
    float phaseElapsedSeconds_ = 0.0f;
    float fadeOpacity_ = 0.0f;
};

} // namespace fps
