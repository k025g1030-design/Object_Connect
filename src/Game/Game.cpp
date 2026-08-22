#include "RetroFPS/Game/Game.hpp"

#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Gameplay/Player/PlayerController.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/Input/InputSystem.hpp"
#include "RetroFPS/Rendering/FirstPersonCamera.hpp"
#include "RetroFPS/Rendering/MapGeometryGenerator.hpp"
#include "RetroFPS/Rendering/MapRenderer.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/World.hpp"

#include <exception>
#include <utility>

namespace fps {

struct Game::Impl final {
    World world;
    Player player;
    PlayerController playerController;
    FirstPersonCamera camera;
    MapRenderer renderer;
    InputSystem input;
    bool shouldQuit = false;

    [[nodiscard]] bool Initialize(const GameConfig& config, std::string& error) {
        MapLoadResult mapResult = GridMapLoader::Load(config.mapPath);
        if (!mapResult) {
            error = "Failed to load the MVP map: " + mapResult.error;
            return false;
        }

        world.Initialize(std::move(*mapResult.map), config.world);

        if (!playerController.Configure(config.player, error)) {
            return false;
        }
        if (!playerController.Initialize(
                player, world.GetMap(), world.GetSettings(), error)) {
            return false;
        }
        if (!camera.Initialize(config.camera, error)) {
            return false;
        }

        SyncCamera();

        const MapGeometry geometry =
            MapGeometryGenerator::Generate(world.GetMap(), world.GetSettings());
        if (!renderer.Initialize(geometry, config.mapRendering, error)) {
            return false;
        }
        if (!input.Initialize(error)) {
            return false;
        }

        shouldQuit = false;
        return true;
    }

    void Update(const float deltaSeconds) {
        const InputState inputState = input.Sample();
        if (inputState.keyboard.escapePressed) {
            shouldQuit = true;
            return;
        }

        playerController.Update(
            player,
            inputState,
            deltaSeconds,
            world.GetMap(),
            world.GetSettings());
        SyncCamera();
    }

    void SyncCamera() {
        const Float2 position = player.GetPositionXZ();
        camera.Sync(
            {position.x, playerController.GetSettings().eyeHeight, position.z},
            player.GetYawRadians(),
            player.GetPitchRadians());
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
        impl_->renderer.Draw(impl_->camera.GetNativeCamera());
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
