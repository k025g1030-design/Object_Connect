#include "RetroFPS/Game/Game.hpp"

#include "RetroFPS/Game/GameFlow.hpp"
#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Gameplay/Player/PlayerController.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/Input/InputSystem.hpp"
#include "RetroFPS/Rendering/FirstPersonCamera.hpp"
#include "RetroFPS/Rendering/GameUiRenderer.hpp"
#include "RetroFPS/Rendering/MapGeometryGenerator.hpp"
#include "RetroFPS/Rendering/MapRenderer.hpp"
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
    case GameScreen::Playing:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] bool MousePositionChanged(
    const MouseState& mouse,
    const bool hasPreviousPosition,
    const UiPoint previousPosition) noexcept {
    if (!hasPreviousPosition) {
        return true;
    }
    return mouse.positionX != previousPosition.x || mouse.positionY != previousPosition.y;
}

[[nodiscard]] bool SegmentIntersectsCell(
    const Float2 start,
    const Float2 end,
    const GridCoordinate cell,
    const float cellSize) noexcept {
    const double minimumX = static_cast<double>(cell.column) * cellSize;
    const double maximumX = minimumX + cellSize;
    const double minimumZ = static_cast<double>(cell.row) * cellSize;
    const double maximumZ = minimumZ + cellSize;
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaZ = static_cast<double>(end.z) - start.z;
    double minimumTime = 0.0;
    double maximumTime = 1.0;

    const auto clipAxis = [&minimumTime, &maximumTime](
                              const double origin,
                              const double delta,
                              const double minimum,
                              const double maximum) {
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

        void Update(
            const PlayerController& controller,
            const InputState& inputState,
            const float deltaSeconds) {
            controller.Update(
                player,
                inputState,
                deltaSeconds,
                world.GetMap(),
                world.GetSettings());
        }
    };

    GameConfig config;
    std::vector<GridMap> maps;
    std::unique_ptr<LevelSession> level;
    PlayerController playerController;
    FirstPersonCamera camera;
    GameUiRenderer ui;
    InputSystem input;
    GameFlow flow;
    std::size_t currentMapIndex = 0;
    UiPoint previousMousePosition{};
    bool hasPreviousMousePosition = false;
    bool windowWasFocused = false;
    bool firstLevelPrepared = false;
    bool shouldQuit = false;

    [[nodiscard]] bool Initialize(const GameConfig& requestedConfig, std::string& error) {
        if (requestedConfig.mapPaths.empty()) {
            error = "GameConfig must contain at least one map path.";
            return false;
        }

        config = requestedConfig;
        maps.clear();
        maps.reserve(config.mapPaths.size());
        for (std::size_t index = 0; index < config.mapPaths.size(); ++index) {
            MapLoadResult mapResult = GridMapLoader::Load(config.mapPaths[index]);
            if (!mapResult) {
                error = "Failed to load map " + std::to_string(index + 1) + " ('" +
                        config.mapPaths[index].generic_string() + "'): " + mapResult.error;
                return false;
            }
            maps.push_back(std::move(*mapResult.map));
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
        if (!input.Initialize(error)) {
            return false;
        }
        input.SetMouseCaptureEnabled(false);

        // Build the first level during startup so map rendering asset failures use
        // Game::Initialize's existing error-reporting path. It remains hidden behind
        // the main menu until START GAME is chosen.
        if (!LoadLevel(0, error)) {
            return false;
        }
        firstLevelPrepared = true;

        flow.ReturnToMainMenu();
        hasPreviousMousePosition = false;
        windowWasFocused = false;
        shouldQuit = false;
        return true;
    }

    [[nodiscard]] bool PreflightMaps(std::string& error) const {
        for (std::size_t index = 0; index < maps.size(); ++index) {
            try {
                const World candidateWorld{maps[index], config.world};
                Player candidatePlayer;
                if (!playerController.Initialize(
                        candidatePlayer,
                        candidateWorld.GetMap(),
                        candidateWorld.GetSettings(),
                        error)) {
                    error = "Map " + std::to_string(index + 1) +
                            " failed player-spawn validation: " + error;
                    return false;
                }
                static_cast<void>(MapGeometryGenerator::Generate(
                    candidateWorld.GetMap(), candidateWorld.GetSettings()));
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

    [[nodiscard]] bool LoadLevel(const std::size_t mapIndex, std::string& error) {
        error.clear();
        if (mapIndex >= maps.size()) {
            error = "Requested map index is outside the configured map list.";
            return false;
        }

        try {
            auto candidate = std::make_unique<LevelSession>();
            candidate->world.Initialize(maps[mapIndex], config.world);
            if (!playerController.Initialize(
                    candidate->player,
                    candidate->world.GetMap(),
                    candidate->world.GetSettings(),
                    error)) {
                error = "Failed to initialize map " + std::to_string(mapIndex + 1) +
                        " player: " + error;
                return false;
            }

            const MapGeometry geometry = MapGeometryGenerator::Generate(
                candidate->world.GetMap(), candidate->world.GetSettings());
            if (!candidate->renderer.Initialize(geometry, config.mapRendering, error)) {
                error = "Failed to initialize map " + std::to_string(mapIndex + 1) +
                        " rendering: " + error;
                return false;
            }

            level = std::move(candidate);
            currentMapIndex = mapIndex;
            SyncCamera();
            return true;
        } catch (const std::exception& exception) {
            error = "Failed to create map " + std::to_string(mapIndex + 1) + ": ";
            error += exception.what();
        } catch (...) {
            error = "Failed to create map " + std::to_string(mapIndex + 1) +
                    " because of an unknown error.";
        }
        return false;
    }

    void LoadLevelOrThrow(const std::size_t mapIndex) {
        std::string error;
        if (!LoadLevel(mapIndex, error)) {
            throw std::runtime_error(error);
        }
    }

    void Update(const float deltaSeconds) {
        const GameScreen screenBeforeInput = flow.GetScreen();
        const InputState inputState = input.Sample();
        const bool focusGained = !windowWasFocused && inputState.windowFocused;

        GameFlowInput flowInput{};
        if (!focusGained) {
            flowInput.previousPressed =
                inputState.keyboard.upPressed || inputState.keyboard.wPressed;
            flowInput.nextPressed =
                inputState.keyboard.downPressed || inputState.keyboard.sPressed;
            flowInput.confirmPressed = inputState.keyboard.enterPressed;
            flowInput.escapePressed = inputState.keyboard.escapePressed;
            flowInput.mousePrimaryPressed = inputState.mouse.leftPressed;
        }
        flowInput.focusLost = screenBeforeInput == GameScreen::Playing &&
                              windowWasFocused && !inputState.windowFocused;

        const std::optional<GameUiScreen> uiScreen = ToUiScreen(screenBeforeInput);
        if (uiScreen.has_value()) {
            const bool mouseMoved = MousePositionChanged(
                inputState.mouse, hasPreviousMousePosition, previousMousePosition);
            if (mouseMoved || inputState.mouse.leftPressed) {
                flowInput.hoveredItem = ui.HitTest(
                    *uiScreen,
                    {inputState.mouse.positionX, inputState.mouse.positionY});
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
        case GameFlowAction::StartGame:
            if (!firstLevelPrepared) {
                LoadLevelOrThrow(0);
            }
            firstLevelPrepared = false;
            break;
        case GameFlowAction::ResetToMainMenu:
            level.reset();
            currentMapIndex = 0;
            firstLevelPrepared = false;
            break;
        case GameFlowAction::QuitGame:
            shouldQuit = true;
            return;
        }

        if (!flowResult.simulateGameplay || !level) {
            return;
        }

        const Float2 previousPlayerPosition = level->player.GetPositionXZ();
        level->Update(playerController, inputState, deltaSeconds);
        SyncCamera();
        HandleMapExit(previousPlayerPosition);
    }

    void HandleMapExit(const Float2 previousPlayerPosition) {
        if (!level) {
            return;
        }

        const GridMap& map = level->world.GetMap();
        const std::optional<GridCoordinate> coordinate = map.TryGetCoordinateAtPosition(
            level->player.GetPositionXZ(), level->world.GetSettings().cellSize);
        const bool endedOnExit = coordinate.has_value() &&
                                  map.GetTile(coordinate->row, coordinate->column) ==
                                      TileType::NextMapExit;
        const bool crossedExit = SegmentIntersectsCell(
            previousPlayerPosition,
            level->player.GetPositionXZ(),
            map.GetNextMapExitCell(),
            level->world.GetSettings().cellSize);
        if (!endedOnExit && !crossedExit) {
            return;
        }

        const std::optional<std::size_t> nextMapIndex =
            TryGetNextMapIndex(currentMapIndex, maps.size());
        if (nextMapIndex.has_value()) {
            LoadLevelOrThrow(*nextMapIndex);
            return;
        }

        level.reset();
        currentMapIndex = 0;
        firstLevelPrepared = false;
        flow.ReturnToMainMenu();
        input.SetMouseCaptureEnabled(false);
        hasPreviousMousePosition = false;
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
    }

    void SyncCamera() {
        if (!level) {
            return;
        }
        const Float2 position = level->player.GetPositionXZ();
        camera.Sync(
            {position.x, playerController.GetSettings().eyeHeight, position.z},
            level->player.GetYawRadians(),
            level->player.GetPitchRadians());
    }
};

Game::Game() noexcept = default;

Game::~Game() {
    Finalize();
}

bool Game::Initialize(std::string& error) {
    return Initialize(GameConfig{}, error);
}

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

void Game::Finalize() noexcept {
    impl_.reset();
}

bool Game::ShouldQuit() const noexcept {
    return impl_ && impl_->shouldQuit;
}

bool Game::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace fps
