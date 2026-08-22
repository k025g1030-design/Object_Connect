#include "../TestSupport.hpp"

#include "RetroFPS/Game/MapSceneManager.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseMap(TestContext& context, const std::string& text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "MapSceneManager test map should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error("MapSceneManager test fixture failed to parse");
    }
    return std::move(*result.map);
}

[[nodiscard]] std::vector<GridMap> MakeMaps(TestContext& context) {
    std::vector<GridMap> maps;
    maps.push_back(ParseMap(context, "PD"));
    maps.push_back(ParseMap(context, "P.ED"));
    return maps;
}

void ReachCommitPending(MapSceneManager& manager, TestContext& context) {
    const MapSceneUpdateResult fadedOut = manager.Update(0.4f);
    context.Expect(!fadedOut.commitRequested, "fade-out completion enters the opaque barrier");
    context.Expect(manager.GetPhase() == MapScenePhase::OpaqueHold,
                   "opaque barrier is observable for a complete frame");
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 1.0f), "opaque barrier is fully black");

    const MapSceneUpdateResult commitRequest = manager.Update(0.0f);
    context.Expect(commitRequest.commitRequested, "opaque barrier requests commit next frame");
    context.Expect(manager.GetPhase() == MapScenePhase::CommitPending,
                   "manager waits for explicit commit completion");
}

void FinishFadeIn(MapSceneManager& manager, TestContext& context) {
    const MapSceneUpdateResult fadedIn = manager.Update(0.4f);
    context.Expect(!fadedIn.completedThisFrame, "fade-in completion enters release barrier");
    context.Expect(manager.GetPhase() == MapScenePhase::ReleaseHold,
                   "release barrier is observable for a complete frame");
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.0f), "release barrier is transparent");
    context.Expect(manager.IsInputLocked(), "release barrier still locks input");
    context.Expect(manager.IsSimulationLocked(), "release barrier still locks simulation");

    const MapSceneUpdateResult completed = manager.Update(0.0f);
    context.Expect(completed.completedThisFrame, "release barrier reports transition completion");
    context.Expect(manager.GetPhase() == MapScenePhase::Idle, "transition returns to idle");
    context.Expect(!manager.IsTransitioning(), "completed transition is no longer active");
}

void CommitCurrentDestination(MapSceneManager& manager, TestContext& context) {
    ReachCommitPending(manager, context);
    context.Expect(manager.CompleteCommit(), "pending destination should commit");
    context.Expect(manager.GetPhase() == MapScenePhase::FadingIn,
                   "successful commit begins fade-in");
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 1.0f), "commit remains fully opaque");
    FinishFadeIn(manager, context);
}

void TestInitializationAndOwnership(TestContext& context) {
    const MapSceneTransitionSettings defaults{};
    context.Expect(NearlyEqual(defaults.fadeOutSeconds, 0.4f), "fade-out defaults to 0.4 seconds");
    context.Expect(NearlyEqual(defaults.fadeInSeconds, 0.4f), "fade-in defaults to 0.4 seconds");

    MapSceneManager manager;
    std::string error;
    context.Expect(manager.Initialize(MakeMaps(context), defaults, error),
                   "manager initializes with owned campaign maps");
    context.Expect(error.empty(), "successful manager initialization clears the error");
    context.Expect(manager.GetMapCount() == 2, "manager owns every configured map");
    context.Expect(manager.TryGetMap(0) != nullptr, "owned map is queryable by index");
    context.Expect(manager.TryGetMap(2) == nullptr, "out-of-range map query returns null");
    context.Expect(!manager.GetActiveMapIndex().has_value(),
                   "campaign begins without an active map");
    context.Expect(manager.GetActiveScene() ==
                       MapSceneDestination{MapSceneDestinationKind::MainMenu, 0},
                   "campaign begins on the main-menu scene");
    context.Expect(manager.GetPhase() == MapScenePhase::Idle, "manager initializes idle");
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.0f), "idle manager is transparent");
    context.Expect(!manager.IsInputLocked(), "idle manager leaves input unlocked");
    context.Expect(!manager.IsSimulationLocked(), "idle manager leaves simulation unlocked");
}

void TestInvalidInitialization(TestContext& context) {
    MapSceneManager manager;
    std::string error;
    context.Expect(!manager.Initialize({}, {}, error), "manager rejects an empty map campaign");
    context.Expect(!error.empty(), "empty campaign reports an initialization error");

    context.Expect(manager.Initialize(MakeMaps(context), {}, error),
                   "valid settings initialize before strong-guarantee checks");
    const GridMap* const originalMap = manager.TryGetMap(0);

    const auto expectInvalidSettings = [&context, &manager, &error,
                                        originalMap](const MapSceneTransitionSettings settings,
                                                     const std::string& description) {
        context.Expect(!manager.Initialize(MakeMaps(context), settings, error), description);
        context.Expect(!error.empty(), "invalid transition settings report an error");
        context.Expect(manager.GetMapCount() == 2 && manager.TryGetMap(0) == originalMap,
                       "failed initialization preserves the previous manager state");
    };

    expectInvalidSettings({0.0f, 0.4f}, "manager rejects zero fade-out duration");
    expectInvalidSettings({-1.0f, 0.4f}, "manager rejects negative fade-out duration");
    expectInvalidSettings({(std::numeric_limits<float>::quiet_NaN)(), 0.4f},
                          "manager rejects NaN fade-out duration");
    expectInvalidSettings({0.4f, (std::numeric_limits<float>::infinity)()},
                          "manager rejects infinite fade-in duration");
    expectInvalidSettings({0.4f, 0.0f}, "manager rejects zero fade-in duration");
}

void TestFadeBarriersAndSmoothstep(TestContext& context) {
    MapSceneManager manager;
    std::string error;
    context.Expect(manager.Initialize(MakeMaps(context), {}, error),
                   "fade test manager initializes");
    context.Expect(manager.BeginFirstMap(), "first-map transition begins from main menu");
    context.Expect(manager.GetPhase() == MapScenePhase::FadingOut, "first map starts fade-out");
    context.Expect(manager.IsTransitioning(), "fade-out is an active transition");
    context.Expect(manager.IsInputLocked(), "fade-out locks input");
    context.Expect(manager.IsSimulationLocked(), "fade-out locks simulation");
    context.Expect(!manager.BeginFirstMap(),
                   "a second transition cannot begin while one is active");
    context.Expect(manager.GetPhase() == MapScenePhase::FadingOut,
                   "a rejected duplicate request does not change the phase");
    context.Expect(!manager.CompleteCommit(), "commit cannot complete before it is requested");

    static_cast<void>(manager.Update(0.1f));
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.15625f),
                   "fade-out opacity uses smoothstep easing");
    static_cast<void>(manager.Update(0.1f));
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.5f),
                   "smoothstep reaches half opacity at half duration");
    static_cast<void>(manager.Update(0.2f));
    context.Expect(manager.GetPhase() == MapScenePhase::OpaqueHold, "fade-out reaches opaque hold");

    const MapSceneUpdateResult requested = manager.Update(0.0f);
    context.Expect(requested.commitRequested, "opaque hold emits one commit request");
    const std::optional<MapSceneDestination> destination = manager.GetCommitDestination();
    context.Expect(destination.has_value(), "commit destination is exposed while pending");
    if (destination.has_value()) {
        context.Expect(destination->kind == MapSceneDestinationKind::Map &&
                           destination->mapIndex == 0,
                       "first-map destination identifies map zero");
    }
    context.Expect(!manager.Update(0.0f).commitRequested,
                   "commit request is an edge event rather than a repeated command");
    context.Expect(manager.CompleteCommit(), "first map commits at full opacity");
    context.Expect(manager.GetActiveMapIndex() == 0, "first map becomes active at commit");
    context.Expect(!manager.CompleteCommit(), "the same destination cannot be committed twice");
    context.Expect(manager.GetActiveMapIndex() == 0,
                   "a rejected duplicate commit preserves the active map");
    context.Expect(!manager.GetCommitDestination().has_value(),
                   "commit destination is hidden outside CommitPending");

    static_cast<void>(manager.Update(0.1f));
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.84375f),
                   "fade-in reverses smoothstep easing");
    static_cast<void>(manager.Update(0.1f));
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.5f), "fade-in reaches half opacity");
    static_cast<void>(manager.Update(0.2f));
    context.Expect(manager.GetPhase() == MapScenePhase::ReleaseHold,
                   "fade-in reaches release hold");
    context.Expect(manager.IsInputLocked(), "release hold keeps input locked at zero opacity");
    const MapSceneUpdateResult completed = manager.Update(0.0f);
    context.Expect(completed.completedThisFrame, "release hold completes on the following frame");
    context.Expect(manager.GetPhase() == MapScenePhase::Idle, "completed fade returns to idle");
}

void TestInvalidDeltaSeconds(TestContext& context) {
    MapSceneManager manager;
    std::string error;
    context.Expect(manager.Initialize(MakeMaps(context), {}, error),
                   "delta test manager initializes");
    context.Expect(manager.BeginFirstMap(), "delta test transition begins");

    static_cast<void>(manager.Update(-1.0f));
    static_cast<void>(manager.Update((std::numeric_limits<float>::quiet_NaN)()));
    static_cast<void>(manager.Update((std::numeric_limits<float>::infinity)()));
    context.Expect(manager.GetPhase() == MapScenePhase::FadingOut,
                   "invalid deltas do not advance time");
    context.Expect(NearlyEqual(manager.GetFadeOpacity(), 0.0f),
                   "negative and non-finite deltas are normalized to zero");
}

void TestLargeDeltaPreservesFrameBarriers(TestContext& context) {
    MapSceneManager manager;
    std::string error;
    context.Expect(manager.Initialize(MakeMaps(context), {}, error),
                   "large-delta manager initializes");
    context.Expect(manager.BeginFirstMap(), "large-delta transition begins");

    const MapSceneUpdateResult fadedOut = manager.Update(100.0f);
    context.Expect(!fadedOut.commitRequested, "large delta cannot skip the opaque frame barrier");
    context.Expect(manager.GetPhase() == MapScenePhase::OpaqueHold,
                   "large delta stops at opaque hold");

    const MapSceneUpdateResult commit = manager.Update(100.0f);
    context.Expect(commit.commitRequested, "opaque hold requests one commit on the next frame");
    context.Expect(manager.CompleteCommit(), "large-delta destination commits");

    const MapSceneUpdateResult fadedIn = manager.Update(100.0f);
    context.Expect(!fadedIn.completedThisFrame,
                   "large delta cannot skip the release frame barrier");
    context.Expect(manager.GetPhase() == MapScenePhase::ReleaseHold,
                   "large delta stops at release hold");
    context.Expect(manager.Update(100.0f).completedThisFrame,
                   "release hold completes on its next frame");
}

void TestMapProgressionResultsAndMainMenu(TestContext& context) {
    MapSceneManager manager;
    std::string error;
    context.Expect(manager.Initialize(MakeMaps(context), {}, error),
                   "progression test manager initializes");
    context.Expect(!manager.BeginMap(2), "out-of-range map transition is rejected");
    context.Expect(!manager.BeginNextOrResults(),
                   "next-map transition requires an active campaign map");
    context.Expect(!manager.BeginResults(), "results transition requires an active campaign map");
    context.Expect(!manager.BeginMainMenu(),
                   "main-menu transition is rejected while already on the main menu");

    context.Expect(manager.BeginFirstMap(), "campaign begins with its first map");
    CommitCurrentDestination(manager, context);
    context.Expect(manager.GetActiveMapIndex() == 0, "map zero remains active after fade-in");
    context.Expect(manager.GetActiveScene() == MapSceneDestination{MapSceneDestinationKind::Map, 0},
                   "map zero is the complete active scene after commit");

    context.Expect(manager.BeginNextOrResults(), "non-final map advances to the next map");
    ReachCommitPending(manager, context);
    const std::optional<MapSceneDestination> next = manager.GetCommitDestination();
    context.Expect(next.has_value() && next->kind == MapSceneDestinationKind::Map &&
                       next->mapIndex == 1,
                   "next-map destination identifies map one");
    context.Expect(manager.GetActiveMapIndex() == 0, "old map remains active before commit");
    context.Expect(manager.CompleteCommit(), "next map commits explicitly");
    context.Expect(manager.GetActiveMapIndex() == 1, "active index changes only at commit");
    FinishFadeIn(manager, context);

    context.Expect(manager.BeginNextOrResults(), "final map begins a transition to results");
    ReachCommitPending(manager, context);
    const std::optional<MapSceneDestination> results = manager.GetCommitDestination();
    context.Expect(results.has_value() && results->kind == MapSceneDestinationKind::Results,
                   "final-map destination is the results scene");
    context.Expect(manager.GetActiveMapIndex() == 1, "final map remains active through fade-out");
    context.Expect(manager.GetActiveScene() == MapSceneDestination{MapSceneDestinationKind::Map, 1},
                   "complete active scene remains the final map before commit");
    context.Expect(manager.CompleteCommit(), "results destination commits explicitly");
    context.Expect(!manager.GetActiveMapIndex().has_value(), "results commit clears active map");
    context.Expect(manager.GetActiveScene() ==
                       MapSceneDestination{MapSceneDestinationKind::Results, 0},
                   "results becomes the complete active scene at commit");
    FinishFadeIn(manager, context);

    context.Expect(!manager.BeginNextOrResults(),
                   "map progression cannot begin from the results scene");
    context.Expect(!manager.BeginResults(), "results cannot transition to itself");
    context.Expect(manager.BeginMainMenu(), "results begins a transition to the main menu");
    ReachCommitPending(manager, context);
    const std::optional<MapSceneDestination> menu = manager.GetCommitDestination();
    context.Expect(menu.has_value() && menu->kind == MapSceneDestinationKind::MainMenu,
                   "results destination identifies the main menu");
    context.Expect(manager.GetActiveScene() ==
                       MapSceneDestination{MapSceneDestinationKind::Results, 0},
                   "results remains active until the main-menu commit");
    context.Expect(manager.CompleteCommit(), "main-menu destination commits explicitly");
    context.Expect(!manager.GetActiveMapIndex().has_value(), "main-menu has no active map");
    context.Expect(manager.GetActiveScene() ==
                       MapSceneDestination{MapSceneDestinationKind::MainMenu, 0},
                   "main menu becomes the complete active scene at commit");
    FinishFadeIn(manager, context);
    context.Expect(!manager.BeginMainMenu(),
                   "main menu cannot transition to itself after completion");
}

void TestExplicitSceneBegins(TestContext& context) {
    MapSceneManager manager;
    std::string error;
    context.Expect(manager.Initialize(MakeMaps(context), {}, error),
                   "explicit begin manager initializes");
    context.Expect(manager.BeginMap(1), "an explicit valid map transition begins");
    CommitCurrentDestination(manager, context);
    context.Expect(manager.GetActiveMapIndex() == 1, "explicit map becomes active");

    context.Expect(manager.BeginMainMenu(), "explicit main-menu transition begins from a map");
    CommitCurrentDestination(manager, context);
    context.Expect(manager.GetActiveScene().kind == MapSceneDestinationKind::MainMenu,
                   "direct map-to-menu destination becomes active");
    context.Expect(!manager.GetActiveMapIndex().has_value(),
                   "direct map-to-menu destination clears the active index");

    context.Expect(manager.BeginMap(0), "a map can begin again from the main menu");
    CommitCurrentDestination(manager, context);
    context.Expect(manager.BeginResults(), "explicit results transition begins from a map");
    CommitCurrentDestination(manager, context);
    context.Expect(manager.GetActiveScene().kind == MapSceneDestinationKind::Results,
                   "explicit results destination becomes active");
    context.Expect(!manager.GetActiveMapIndex().has_value(),
                   "explicit results destination has no active map index");

    context.Expect(manager.BeginMainMenu(), "explicit main-menu transition begins from results");
    CommitCurrentDestination(manager, context);
    context.Expect(!manager.GetActiveMapIndex().has_value(),
                   "explicit menu commit clears active map");
}

} // namespace

void RunMapSceneManagerTests(TestContext& context) {
    TestInitializationAndOwnership(context);
    TestInvalidInitialization(context);
    TestFadeBarriersAndSmoothstep(context);
    TestInvalidDeltaSeconds(context);
    TestLargeDeltaPreservesFrameBarriers(context);
    TestMapProgressionResultsAndMainMenu(context);
    TestExplicitSceneBegins(context);
}

} // namespace fps::tests
