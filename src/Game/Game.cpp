#include "RetroFPS/Game/Game.hpp"

#include "RetroFPS/Game/GameFlow.hpp"
#include "RetroFPS/Game/MapSceneManager.hpp"
#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Gameplay/Player/PlayerController.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/Input/InputSystem.hpp"
#include "RetroFPS/Rendering/FirstPersonCamera.hpp"
#include "RetroFPS/Rendering/GameUiRenderer.hpp"
#include "RetroFPS/Rendering/MapGeometryGenerator.hpp"
#include "RetroFPS/Rendering/MapRenderer.hpp"
#include "RetroFPS/Rendering/ScreenFadeRenderer.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/World.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fps {
namespace {

[[nodiscard]] std::optional<GameUiScreen> ToUiScreen(const GameScreen screen) noexcept {
    switch (screen) {
    case GameScreen::MainMenu:
        return GameUiScreen::MainMenu;
    case GameScreen::Controls:
        return GameUiScreen::Controls;
    case GameScreen::Paused:
        return GameUiScreen::PauseMenu;
    case GameScreen::Results:
        return GameUiScreen::Results;
    case GameScreen::Playing:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool MousePositionChanged(const MouseState& mouse, const bool hasPreviousPosition,
                                        const UiPoint previousPosition) noexcept {
    if (!hasPreviousPosition) {
        return true;
    }
    return mouse.positionX != previousPosition.x || mouse.positionY != previousPosition.y;
}

[[nodiscard]] bool IsMovementHeld(const KeyboardState& keyboard) noexcept {
    return keyboard.w || keyboard.a || keyboard.s || keyboard.d;
}

[[nodiscard]] bool SegmentIntersectsCell(const Float2 start, const Float2 end,
                                         const GridCoordinate cell, const float cellSize) noexcept {
    const double minimumX = static_cast<double>(cell.column) * cellSize;
    const double maximumX = minimumX + cellSize;
    const double minimumZ = static_cast<double>(cell.row) * cellSize;
    const double maximumZ = minimumZ + cellSize;
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaZ = static_cast<double>(end.z) - start.z;
    double minimumTime = 0.0;
    double maximumTime = 1.0;

    const auto clipAxis = [&minimumTime, &maximumTime](const double origin, const double delta,
                                                       const double minimum, const double maximum) {
        if (delta == 0.0) {
            return origin >= minimum && origin <= maximum;
        }

        double entryTime = (minimum - origin) / delta;
        double exitTime = (maximum - origin) / delta;
        if (entryTime > exitTime) {
            std::swap(entryTime, exitTime);
        }
        minimumTime = (std::max)(minimumTime, entryTime);
        maximumTime = (std::min)(maximumTime, exitTime);
        return minimumTime <= maximumTime;
    };

    return clipAxis(start.x, deltaX, minimumX, maximumX) &&
           clipAxis(start.z, deltaZ, minimumZ, maximumZ);
}

} // namespace

struct Game::Impl final {
    struct LevelSession final {
        World world;
        Player player;
        MapRenderer renderer;

        void Update(const PlayerController& controller, const InputState& inputState,
                    const float deltaSeconds) {
            controller.Update(player, inputState, deltaSeconds, world.GetMap(),
                              world.GetSettings());
        }
    };

    GameConfig config;
    MapSceneManager mapScenes;
    std::unique_ptr<LevelSession> level;
    PlayerController playerController;
    FirstPersonCamera camera;
    GameUiRenderer ui;
    ScreenFadeRenderer screenFade;
    InputSystem input;
    GameFlow flow;
    UiPoint previousMousePosition{};
    bool hasPreviousMousePosition = false;
    bool windowWasFocused = false;
    bool firstLevelPrepared = false;
    bool movementReleaseRequired = false;
    bool shouldQuit = false;

    [[nodiscard]] bool Initialize(const GameConfig& requestedConfig, std::string& error) {
        if (requestedConfig.mapPaths.empty()) {
            error = "GameConfig must contain at least one map path.";
            return false;
        }

        config = requestedConfig;
        std::vector<GridMap> loadedMaps;
        loadedMaps.reserve(config.mapPaths.size());
        for (std::size_t index = 0; index < config.mapPaths.size(); ++index) {
            MapLoadResult mapResult = GridMapLoader::Load(config.mapPaths[index]);
            if (!mapResult) {
                error = "Failed to load map " + std::to_string(index + 1) + " ('" +
                        config.mapPaths[index].generic_string() + "'): " + mapResult.error;
                return false;
            }
            loadedMaps.push_back(std::move(*mapResult.map));
        }
        if (!mapScenes.Initialize(std::move(loadedMaps), config.mapTransition, error)) {
            return false;
        }

        if (!playerController.Configure(config.player, error)) {
            return false;
        }
        if (!PreflightMaps(error)) {
            return false;
        }
        if (!camera.Initialize(config.camera, error)) {
            return false;
        }
        if (!ui.Initialize(error)) {
            return false;
        }
        if (!screenFade.Initialize(error)) {
            return false;
        }
        if (!input.Initialize(error)) {
            return false;
        }
        input.SetMouseCaptureEnabled(false);

        // Build the first level during startup so map rendering asset failures
        // use Game::Initialize's existing error-reporting path. It remains
        // hidden behind the main menu until START GAME is chosen.
        std::unique_ptr<LevelSession> firstLevel = BuildLevel(0, error);
        if (!firstLevel) {
            return false;
        }
        level = std::move(firstLevel);
        SyncCamera();
        firstLevelPrepared = true;

        flow.ReturnToMainMenu();
        hasPreviousMousePosition = false;
        windowWasFocused = false;
        movementReleaseRequired = false;
        shouldQuit = false;
        return true;
    }

    [[nodiscard]] bool PreflightMaps(std::string& error) const {
        for (std::size_t index = 0; index < mapScenes.GetMapCount(); ++index) {
            try {
                const GridMap* const map = mapScenes.TryGetMap(index);
                if (map == nullptr) {
                    error = "Map manager lost a preloaded map definition.";
                    return false;
                }
                const World candidateWorld{*map, config.world};
                Player candidatePlayer;
                if (!playerController.Initialize(candidatePlayer, candidateWorld.GetMap(),
                                                 candidateWorld.GetSettings(), error)) {
                    error = "Map " + std::to_string(index + 1) +
                            " failed player-spawn validation: " + error;
                    return false;
                }
                static_cast<void>(MapGeometryGenerator::Generate(candidateWorld.GetMap(),
                                                                 candidateWorld.GetSettings()));
            } catch (const std::exception& exception) {
                error = "Map " + std::to_string(index + 1) +
                        " failed preflight validation: " + exception.what();
                return false;
            } catch (...) {
                error = "Map " + std::to_string(index + 1) +
                        " failed preflight validation because of an unknown error.";
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::unique_ptr<LevelSession> BuildLevel(const std::size_t mapIndex,
                                                           std::string& error) {
        error.clear();
        const GridMap* const map = mapScenes.TryGetMap(mapIndex);
        if (map == nullptr) {
            error = "Requested map index is outside the configured map list.";
            return nullptr;
        }

        try {
            auto candidate = std::make_unique<LevelSession>();
            candidate->world.Initialize(*map, config.world);
            if (!playerController.Initialize(candidate->player, candidate->world.GetMap(),
                                             candidate->world.GetSettings(), error)) {
                error = "Failed to initialize map " + std::to_string(mapIndex + 1) +
                        " player: " + error;
                return nullptr;
            }

            const MapGeometry geometry = MapGeometryGenerator::Generate(
                candidate->world.GetMap(), candidate->world.GetSettings());
            if (!candidate->renderer.Initialize(geometry, config.mapRendering, error)) {
                error = "Failed to initialize map " + std::to_string(mapIndex + 1) +
                        " rendering: " + error;
                return nullptr;
            }

            return candidate;
        } catch (const std::exception& exception) {
            error = "Failed to create map " + std::to_string(mapIndex + 1) + ": ";
            error += exception.what();
        } catch (...) {
            error = "Failed to create map " + std::to_string(mapIndex + 1) +
                    " because of an unknown error.";
        }
        return nullptr;
    }

    [[nodiscard]] std::unique_ptr<LevelSession> BuildLevelOrThrow(const std::size_t mapIndex) {
        std::string error;
        std::unique_ptr<LevelSession> candidate = BuildLevel(mapIndex, error);
        if (!candidate) {
            throw std::runtime_error(error);
        }
        return candidate;
    }

    void Update(const float deltaSeconds) {
        const InputState inputState = input.Sample();
        const bool focusGained = !windowWasFocused && inputState.windowFocused;
        const bool transitionLockedAtFrameStart = mapScenes.IsInputLocked();

        if (transitionLockedAtFrameStart) {
            windowWasFocused = inputState.windowFocused;
            hasPreviousMousePosition = false;

            const MapSceneUpdateResult transitionResult = mapScenes.Update(deltaSeconds);
            if (transitionResult.commitRequested) {
                CommitPendingScene();
            }
            if (transitionResult.completedThisFrame && flow.GetScreen() == GameScreen::Playing &&
                !inputState.windowFocused) {
                flow.EnterPaused();
                input.SetMouseCaptureEnabled(false);
            }
            if (transitionResult.completedThisFrame) {
                movementReleaseRequired = true;
            }
            return;
        }

        const GameScreen screenBeforeInput = flow.GetScreen();

        GameFlowInput flowInput{};
        if (!focusGained) {
            flowInput.previousPressed =
                inputState.keyboard.upPressed || inputState.keyboard.wPressed;
            flowInput.nextPressed = inputState.keyboard.downPressed || inputState.keyboard.sPressed;
            flowInput.confirmPressed = inputState.keyboard.enterPressed;
            flowInput.escapePressed = inputState.keyboard.escapePressed;
            flowInput.mousePrimaryPressed = inputState.mouse.leftPressed;
        }
        flowInput.focusLost = screenBeforeInput == GameScreen::Playing && windowWasFocused &&
                              !inputState.windowFocused;

        const std::optional<GameUiScreen> uiScreen = ToUiScreen(screenBeforeInput);
        if (uiScreen.has_value()) {
            const bool mouseMoved = MousePositionChanged(inputState.mouse, hasPreviousMousePosition,
                                                         previousMousePosition);
            if (mouseMoved || inputState.mouse.leftPressed) {
                flowInput.hoveredItem =
                    ui.HitTest(*uiScreen, {inputState.mouse.positionX, inputState.mouse.positionY});
            }
        }

        previousMousePosition = {
            inputState.mouse.positionX,
            inputState.mouse.positionY,
        };
        hasPreviousMousePosition = true;
        windowWasFocused = inputState.windowFocused;

        const GameFlowResult flowResult = flow.Update(flowInput);
        if (flowResult.screenChanged) {
            input.SetMouseCaptureEnabled(flow.GetScreen() == GameScreen::Playing);
            hasPreviousMousePosition = false;
        }

        switch (flowResult.action) {
        case GameFlowAction::None:
            break;
        case GameFlowAction::RequestStartGame:
            if (!mapScenes.BeginFirstMap()) {
                throw std::logic_error("Map manager rejected the first-map transition.");
            }
            return;
        case GameFlowAction::RequestMainMenu:
            if (!mapScenes.BeginMainMenu()) {
                throw std::logic_error("Map manager rejected the main-menu transition.");
            }
            return;
        case GameFlowAction::QuitGame:
            shouldQuit = true;
            return;
        }

        if (!flowResult.simulateGameplay || !level) {
            return;
        }

        InputState gameplayInput = inputState;
        if (movementReleaseRequired) {
            if (inputState.windowFocused && !IsMovementHeld(inputState.keyboard)) {
                movementReleaseRequired = false;
            }
            gameplayInput.keyboard.w = false;
            gameplayInput.keyboard.a = false;
            gameplayInput.keyboard.s = false;
            gameplayInput.keyboard.d = false;
        }

        const Float2 previousPlayerPosition = level->player.GetPositionXZ();
        level->Update(playerController, gameplayInput, deltaSeconds);
        SyncCamera();
        HandleMapExit(previousPlayerPosition);
    }

    void CommitPendingScene() {
        const std::optional<MapSceneDestination> destination = mapScenes.GetCommitDestination();
        if (!destination.has_value()) {
            throw std::logic_error("Map transition requested a commit without a destination.");
        }

        switch (destination->kind) {
        case MapSceneDestinationKind::Map: {
            const bool canUsePreparedFirstLevel = firstLevelPrepared && level &&
                                                  destination->mapIndex == 0 &&
                                                  !mapScenes.GetActiveMapIndex().has_value();
            if (!canUsePreparedFirstLevel) {
                std::unique_ptr<LevelSession> candidate = BuildLevelOrThrow(destination->mapIndex);
                level = std::move(candidate);
            }

            firstLevelPrepared = false;
            SyncCamera();
            flow.EnterPlaying();
            input.SetMouseCaptureEnabled(true);
            break;
        }
        case MapSceneDestinationKind::Results:
            level.reset();
            firstLevelPrepared = false;
            flow.EnterResults();
            input.SetMouseCaptureEnabled(false);
            break;
        case MapSceneDestinationKind::MainMenu:
            level.reset();
            firstLevelPrepared = false;
            flow.ReturnToMainMenu();
            input.SetMouseCaptureEnabled(false);
            break;
        }

        hasPreviousMousePosition = false;
        if (!mapScenes.CompleteCommit()) {
            throw std::logic_error("Map manager rejected a completed scene commit.");
        }
    }

    void HandleMapExit(const Float2 previousPlayerPosition) {
        if (!level) {
            return;
        }

        const GridMap& map = level->world.GetMap();
        const std::optional<GridCoordinate> coordinate = map.TryGetCoordinateAtPosition(
            level->player.GetPositionXZ(), level->world.GetSettings().cellSize);
        const bool endedOnExit =
            coordinate.has_value() &&
            map.GetTile(coordinate->row, coordinate->column) == TileType::NextMapExit;
        const bool crossedExit =
            SegmentIntersectsCell(previousPlayerPosition, level->player.GetPositionXZ(),
                                  map.GetNextMapExitCell(), level->world.GetSettings().cellSize);
        if (!endedOnExit && !crossedExit) {
            return;
        }

        if (!mapScenes.BeginNextOrResults()) {
            throw std::logic_error("Map manager rejected the exit transition.");
        }
    }

    void Draw() const {
        const GameScreen screen = flow.GetScreen();
        if ((screen == GameScreen::Playing || screen == GameScreen::Paused) && level) {
            level->renderer.Draw(camera.GetNativeCamera());
        }

        const std::optional<GameUiScreen> uiScreen = ToUiScreen(screen);
        if (uiScreen.has_value()) {
            ui.Draw(*uiScreen, flow.GetSelectedItem());
        }

        screenFade.Draw(mapScenes.GetFadeOpacity());
    }

    void SyncCamera() {
        if (!level) {
            return;
        }
        const Float2 position = level->player.GetPositionXZ();
        camera.Sync({position.x, playerController.GetSettings().eyeHeight, position.z},
                    level->player.GetYawRadians(), level->player.GetPitchRadians());
    }
};

Game::Game() noexcept = default;

Game::~Game() { Finalize(); }

bool Game::Initialize(std::string& error) { return Initialize(GameConfig{}, error); }

bool Game::Initialize(const GameConfig& config, std::string& error) {
    Finalize();
    error.clear();

    try {
        auto next = std::make_unique<Impl>();
        if (!next->Initialize(config, error)) {
            return false;
        }

        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize the game: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize the game because of an unknown error.";
    }

    return false;
}

void Game::Update(const float deltaSeconds) {
    if (impl_) {
        impl_->Update(deltaSeconds);
    }
}

void Game::Draw() const {
    if (impl_) {
        impl_->Draw();
    }
}

void Game::Finalize() noexcept { impl_.reset(); }

bool Game::ShouldQuit() const noexcept { return impl_ && impl_->shouldQuit; }

bool Game::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace fps
