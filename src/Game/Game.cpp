#include "RetroFPS/Game/Game.hpp"

#include "RetroFPS/Collision/CombatCollision.hpp"
#include "RetroFPS/Data/GameData.hpp"
#include "RetroFPS/Game/CampaignRunState.hpp"
#include "RetroFPS/Game/GameFlow.hpp"
#include "RetroFPS/Game/MapSceneManager.hpp"
#include "RetroFPS/Gameplay/Combat/ProjectileSystem.hpp"
#include "RetroFPS/Gameplay/Enemy/EnemySpawnDirector.hpp"
#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/Gameplay/Player/Player.hpp"
#include "RetroFPS/Gameplay/Player/PlayerCombatState.hpp"
#include "RetroFPS/Gameplay/Player/PlayerController.hpp"
#include "RetroFPS/Gameplay/Weapon/WeaponController.hpp"
#include "RetroFPS/Gameplay/Weapon/WeaponState.hpp"
#include "RetroFPS/Input/InputState.hpp"
#include "RetroFPS/Input/InputSystem.hpp"
#include "RetroFPS/Rendering/EnemyBillboardRenderer.hpp"
#include "RetroFPS/Rendering/FirstPersonCamera.hpp"
#include "RetroFPS/Rendering/GameHudRenderer.hpp"
#include "RetroFPS/Rendering/GameUiRenderer.hpp"
#include "RetroFPS/Rendering/MapGeometryGenerator.hpp"
#include "RetroFPS/Rendering/MapRenderer.hpp"
#include "RetroFPS/Rendering/ProjectileRenderer.hpp"
#include "RetroFPS/Rendering/ScenePostProcessRenderer.hpp"
#include "RetroFPS/Rendering/ScreenFadeRenderer.hpp"
#include "RetroFPS/Rendering/SkySphereRenderer.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fps {
namespace {

constexpr float kMaximumShotDistance = 50.0f;
constexpr float kMuzzleForwardOffset = 0.35f;
constexpr float kMuzzleRightOffset = 0.28f;
constexpr float kMuzzleDownOffset = 0.22f;
constexpr float kCrosshairBaseGapPixels = 8.0f;
constexpr float kCrosshairMaximumGapPixels = 32.0f;
constexpr float kCrosshairRecoveryPixelsPerSecond = 48.0f;
constexpr float kWeaponKickRecoveryPixelsPerSecond = 96.0f;
constexpr float kLengthEpsilon = 0.000001f;

struct ViewBasis final {
    Float3 forward{};
    Float3 right{};
    Float3 up{};
};

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

[[nodiscard]] bool MousePositionChanged(
    const MouseState& mouse,
    const bool hasPreviousPosition,
    const UiPoint previousPosition) noexcept {
    if (!hasPreviousPosition) {
        return true;
    }
    return mouse.positionX != previousPosition.x || mouse.positionY != previousPosition.y;
}

[[nodiscard]] bool IsMovementHeld(const KeyboardState& keyboard) noexcept {
    return keyboard.w || keyboard.a || keyboard.s || keyboard.d;
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

[[nodiscard]] ViewBasis MakeViewBasis(
    const float yawRadians,
    const float pitchRadians) noexcept {
    const float sineYaw = std::sin(yawRadians);
    const float cosineYaw = std::cos(yawRadians);
    const float sinePitch = std::sin(pitchRadians);
    const float cosinePitch = std::cos(pitchRadians);
    return {
        {sineYaw * cosinePitch, -sinePitch, cosineYaw * cosinePitch},
        {cosineYaw, 0.0f, -sineYaw},
        {sineYaw * sinePitch, cosinePitch, cosineYaw * sinePitch},
    };
}

[[nodiscard]] Float3 AddScaled(
    const Float3 value,
    const Float3 direction,
    const float distance) noexcept {
    return {
        value.x + direction.x * distance,
        value.y + direction.y * distance,
        value.z + direction.z * distance,
    };
}

[[nodiscard]] Float3 Subtract(const Float3 left, const Float3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] float Length(const Float3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] std::vector<CombatTarget> MakeCombatTargets(
    const std::span<const EnemySnapshot> snapshots) {
    std::vector<CombatTarget> targets;
    targets.reserve(snapshots.size());
    for (const EnemySnapshot& enemy : snapshots) {
        if (enemy.state == EnemyState::Dead) {
            continue;
        }
        targets.push_back({
            enemy.id,
            {enemy.position, enemy.hitboxHeight, enemy.collisionRadius},
        });
    }
    return targets;
}

[[nodiscard]] int SaturatingInt(const std::uint32_t value) noexcept {
    constexpr std::uint32_t maximum =
        static_cast<std::uint32_t>((std::numeric_limits<int>::max)());
    return value > maximum ? (std::numeric_limits<int>::max)()
                           : static_cast<int>(value);
}

} // namespace

struct Game::Impl final {
    struct LevelSession final {
        std::size_t definitionIndex = 0;
        World world;
        Player player;
        EnemySystem enemies;
        EnemySpawnDirector spawnDirector;
        ProjectileSystem projectiles;
        MapRenderer renderer;
        EnemyBillboardRenderer enemyRenderer;
        ProjectileRenderer projectileRenderer;
        bool doorVisible = false;
        bool exitRequiresLeave = false;
    };

    GameConfig config;
    GameDataCatalog dataCatalog;
    std::vector<LevelDefinition> orderedLevels;
    EnemyDefinition meleeDefinition;
    EnemyDefinition rangedDefinition;
    WeaponDefinition weaponDefinition;
    MapSceneManager mapScenes;
    std::unique_ptr<LevelSession> level;
    PlayerController playerController;
    PlayerCombatState playerCombat;
    WeaponController weaponController;
    WeaponState weaponState;
    CampaignRunState campaign;
    FirstPersonCamera camera;
    SkySphereRenderer sky;
    ScenePostProcessRenderer scenePostProcess;
    GameHudRenderer hud;
    GameUiRenderer ui;
    ScreenFadeRenderer screenFade;
    InputSystem input;
    GameFlow flow;
    std::vector<ResultRoomEntry> resultEntries;
    UiPoint previousMousePosition{};
    float crosshairExpansionPixels = 0.0f;
    float weaponKickPixels = 0.0f;
    bool hasPreviousMousePosition = false;
    bool windowWasFocused = false;
    bool firstLevelPrepared = false;
    bool movementReleaseRequired = false;
    bool fireReleaseRequired = false;
    bool shouldQuit = false;

    [[nodiscard]] bool Initialize(const GameConfig& requestedConfig, std::string& error) {
        config = requestedConfig;
        GameDataLoadResult dataResult = GameDataLoader::Load(config.data);
        if (!dataResult) {
            error = "Failed to load game-data catalogs: " + dataResult.error;
            return false;
        }
        dataCatalog = std::move(*dataResult.catalog);

        if (!ResolveConfiguredDefinitions(error) || !BuildLevelOrder(error)) {
            return false;
        }

        std::vector<GridMap> loadedMaps;
        loadedMaps.reserve(orderedLevels.size());
        for (const LevelDefinition& definition : orderedLevels) {
            const std::filesystem::path mapPath =
                config.data.resourceRoot / definition.mapPath;
            MapLoadResult mapResult = GridMapLoader::Load(mapPath);
            if (!mapResult) {
                error = "Failed to load level '" + definition.id + "' map ('" +
                        mapPath.generic_string() + "'): " + mapResult.error;
                return false;
            }
            loadedMaps.push_back(std::move(*mapResult.map));
        }
        if (!mapScenes.Initialize(std::move(loadedMaps), config.mapTransition, error)) {
            return false;
        }

        if (!playerController.Configure(config.player, error) ||
            !weaponController.Configure(weaponDefinition, config.weapon, error) ||
            !weaponController.Initialize(weaponState, error)) {
            return false;
        }

        std::vector<CampaignRoomDefinition> campaignRooms;
        campaignRooms.reserve(orderedLevels.size());
        for (const LevelDefinition& definition : orderedLevels) {
            campaignRooms.push_back({definition.id, definition.name});
        }
        if (!campaign.Initialize(campaignRooms, error) || !PreflightMaps(error) ||
            !camera.Initialize(config.camera, error) ||
            !sky.Initialize(config.skyRendering, config.camera.farClip, error) ||
            !scenePostProcess.Initialize(config.scenePostProcess, error) ||
            !hud.Initialize(weaponDefinition.texturePath, error) || !ui.Initialize(error) ||
            !screenFade.Initialize(error) || !input.Initialize(error)) {
            return false;
        }
        input.SetMouseCaptureEnabled(false);

        std::unique_ptr<LevelSession> firstLevel = BuildLevel(0, error);
        if (!firstLevel) {
            return false;
        }
        level = std::move(firstLevel);
        SyncCamera();
        firstLevelPrepared = true;

        flow.ReturnToMainMenu();
        resultEntries.clear();
        hasPreviousMousePosition = false;
        windowWasFocused = false;
        movementReleaseRequired = false;
        fireReleaseRequired = false;
        shouldQuit = false;
        return true;
    }

    [[nodiscard]] bool ResolveConfiguredDefinitions(std::string& error) {
        const EnemyDefinition* const melee =
            dataCatalog.enemies.FindById(config.meleeEnemyId);
        if (melee == nullptr) {
            error = "Configured melee enemy ID does not exist: " + config.meleeEnemyId;
            return false;
        }
        if (melee->kind != EnemyKind::Melee) {
            error = "Configured melee enemy ID does not reference a melee definition: " +
                    config.meleeEnemyId;
            return false;
        }

        const EnemyDefinition* const ranged =
            dataCatalog.enemies.FindById(config.rangedEnemyId);
        if (ranged == nullptr) {
            error = "Configured ranged enemy ID does not exist: " + config.rangedEnemyId;
            return false;
        }
        if (ranged->kind != EnemyKind::Ranged) {
            error = "Configured ranged enemy ID does not reference a ranged definition: " +
                    config.rangedEnemyId;
            return false;
        }

        const WeaponDefinition* const weapon =
            dataCatalog.weapons.FindById(config.startingWeaponId);
        if (weapon == nullptr) {
            error = "Configured starting weapon ID does not exist: " +
                    config.startingWeaponId;
            return false;
        }

        meleeDefinition = *melee;
        rangedDefinition = *ranged;
        weaponDefinition = *weapon;
        return true;
    }

    [[nodiscard]] bool BuildLevelOrder(std::string& error) {
        orderedLevels.clear();
        const LevelDefinition* current =
            dataCatalog.levels.FindById(config.startLevelId);
        if (current == nullptr) {
            error = "Configured start level ID does not exist: " + config.startLevelId;
            return false;
        }
        const LevelDefinition* const catalogStart = dataCatalog.levels.GetStartLevel();
        if (catalogStart == nullptr || catalogStart->id != current->id) {
            error = "Configured start level ID must reference the first level in the linear campaign: " +
                    config.startLevelId;
            return false;
        }

        std::unordered_set<LevelDefinitionId> visited;
        while (current != nullptr) {
            if (!visited.insert(current->id).second) {
                error = "Configured level chain contains a cycle at ID: " + current->id;
                return false;
            }
            orderedLevels.push_back(*current);
            if (!current->nextLevelId.has_value()) {
                current = nullptr;
            } else {
                current = dataCatalog.levels.FindById(*current->nextLevelId);
                if (current == nullptr) {
                    error = "Configured level chain references an unknown next level ID.";
                    return false;
                }
            }
        }

        if (orderedLevels.size() != dataCatalog.levels.GetDefinitions().size()) {
            error = "Configured start level does not reach every level in the catalog.";
            orderedLevels.clear();
            return false;
        }
        return true;
    }

    [[nodiscard]] bool PreflightMaps(std::string& error) const {
        for (std::size_t index = 0; index < mapScenes.GetMapCount(); ++index) {
            try {
                const GridMap* const map = mapScenes.TryGetMap(index);
                if (map == nullptr || index >= orderedLevels.size()) {
                    error = "Map manager lost a preloaded level definition.";
                    return false;
                }
                const World candidateWorld{*map, config.world};
                Player candidatePlayer;
                if (!playerController.Initialize(
                        candidatePlayer,
                        candidateWorld.GetMap(),
                        candidateWorld.GetSettings(),
                        error)) {
                    error = "Level '" + orderedLevels[index].id +
                            "' failed player-spawn validation: " + error;
                    return false;
                }

                EnemySystem candidateEnemies;
                if (!candidateEnemies.InitializeEmpty(
                        candidateWorld.GetMap(),
                        candidatePlayer.GetPositionXZ(),
                        playerController.GetSettings().collisionRadius,
                        candidateWorld.GetSettings().cellSize,
                        config.enemies,
                        error)) {
                    error = "Level '" + orderedLevels[index].id +
                            "' failed enemy-system validation: " + error;
                    return false;
                }
                EnemySpawnDirector candidateDirector;
                if (!candidateDirector.Initialize(
                        candidateWorld.GetMap(),
                        orderedLevels[index],
                        meleeDefinition,
                        rangedDefinition,
                        error)) {
                    error = "Level '" + orderedLevels[index].id +
                            "' failed spawn-director validation: " + error;
                    return false;
                }
                const EnemySpawnBatchResult spawnResult = candidateDirector.SpawnAvailable(
                    candidateEnemies,
                    candidateWorld.GetMap(),
                    candidatePlayer.GetPositionXZ(),
                    playerController.GetSettings().collisionRadius,
                    error);
                static_cast<void>(spawnResult);
                if (!error.empty()) {
                    error = "Level '" + orderedLevels[index].id +
                            "' failed initial enemy-spawn validation: " + error;
                    return false;
                }
                static_cast<void>(MapGeometryGenerator::Generate(
                    candidateWorld.GetMap(), candidateWorld.GetSettings()));
            } catch (const std::exception& exception) {
                error = "Level '" + orderedLevels[index].id +
                        "' failed preflight validation: " + exception.what();
                return false;
            } catch (...) {
                error = "Level '" + orderedLevels[index].id +
                        "' failed preflight validation because of an unknown error.";
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::unique_ptr<LevelSession> BuildLevel(
        const std::size_t mapIndex,
        std::string& error) {
        error.clear();
        const GridMap* const map = mapScenes.TryGetMap(mapIndex);
        if (map == nullptr || mapIndex >= orderedLevels.size()) {
            error = "Requested level index is outside the configured campaign.";
            return nullptr;
        }

        try {
            auto candidate = std::make_unique<LevelSession>();
            candidate->definitionIndex = mapIndex;
            candidate->world.Initialize(*map, config.world);
            if (!playerController.Initialize(
                    candidate->player,
                    candidate->world.GetMap(),
                    candidate->world.GetSettings(),
                    error)) {
                error = "Failed to initialize level '" + orderedLevels[mapIndex].id +
                        "' player: " + error;
                return nullptr;
            }
            if (!candidate->enemies.InitializeEmpty(
                    candidate->world.GetMap(),
                    candidate->player.GetPositionXZ(),
                    playerController.GetSettings().collisionRadius,
                    candidate->world.GetSettings().cellSize,
                    config.enemies,
                    error)) {
                error = "Failed to initialize level '" + orderedLevels[mapIndex].id +
                        "' enemy system: " + error;
                return nullptr;
            }
            if (!candidate->spawnDirector.Initialize(
                    candidate->world.GetMap(),
                    orderedLevels[mapIndex],
                    meleeDefinition,
                    rangedDefinition,
                    error)) {
                error = "Failed to initialize level '" + orderedLevels[mapIndex].id +
                        "' spawn director: " + error;
                return nullptr;
            }
            const EnemySpawnBatchResult initialSpawn = candidate->spawnDirector.SpawnAvailable(
                candidate->enemies,
                candidate->world.GetMap(),
                candidate->player.GetPositionXZ(),
                playerController.GetSettings().collisionRadius,
                error);
            static_cast<void>(initialSpawn);
            if (!error.empty()) {
                error = "Failed to spawn level '" + orderedLevels[mapIndex].id +
                        "' initial enemies: " + error;
                return nullptr;
            }
            if (!candidate->projectiles.Configure(config.projectiles)) {
                error = "Failed to configure level projectiles.";
                return nullptr;
            }

            const MapGeometry geometry = MapGeometryGenerator::Generate(
                candidate->world.GetMap(), candidate->world.GetSettings());
            if (!candidate->renderer.Initialize(geometry, config.mapRendering, error)) {
                error = "Failed to initialize level '" + orderedLevels[mapIndex].id +
                        "' map rendering: " + error;
                return nullptr;
            }
            candidate->doorVisible = false;
            candidate->renderer.SetDoorVisible(false);
            if (!candidate->enemyRenderer.Initialize(
                    candidate->enemies.GetSnapshots(),
                    dataCatalog.enemies.GetDefinitions(),
                    config.enemyRendering,
                    error)) {
                error = "Failed to initialize level '" + orderedLevels[mapIndex].id +
                        "' enemy rendering: " + error;
                return nullptr;
            }
            if (!candidate->projectileRenderer.Initialize(error)) {
                error = "Failed to initialize level '" + orderedLevels[mapIndex].id +
                        "' projectile rendering: " + error;
                return nullptr;
            }
            candidate->enemyRenderer.Sync(
                candidate->enemies.GetSnapshots(), candidate->player.GetPositionXZ());
            candidate->projectileRenderer.Sync(candidate->projectiles.GetSnapshots());
            return candidate;
        } catch (const std::exception& exception) {
            error = "Failed to create level '" + orderedLevels[mapIndex].id + "': " +
                    exception.what();
        } catch (...) {
            error = "Failed to create level '" + orderedLevels[mapIndex].id +
                    "' because of an unknown error.";
        }
        return nullptr;
    }

    [[nodiscard]] std::unique_ptr<LevelSession> BuildLevelOrThrow(
        const std::size_t mapIndex) {
        std::string error;
        std::unique_ptr<LevelSession> candidate = BuildLevel(mapIndex, error);
        if (!candidate) {
            throw std::runtime_error(error);
        }
        return candidate;
    }

    void ResetRunOrThrow() {
        std::string error;
        playerCombat.Reset();
        if (!weaponController.Initialize(weaponState, error)) {
            throw std::runtime_error("Failed to reset starting weapon: " + error);
        }
        campaign.ResetRun();
        resultEntries.clear();
        crosshairExpansionPixels = 0.0f;
        weaponKickPixels = 0.0f;
        movementReleaseRequired = true;
        fireReleaseRequired = true;
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
            if (transitionResult.completedThisFrame &&
                flow.GetScreen() == GameScreen::Playing && !inputState.windowFocused) {
                flow.EnterPaused();
                input.SetMouseCaptureEnabled(false);
            }
            if (transitionResult.completedThisFrame) {
                movementReleaseRequired = true;
                fireReleaseRequired = true;
            }
            return;
        }

        const GameScreen screenBeforeInput = flow.GetScreen();
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
        flowInput.focusLost = screenBeforeInput == GameScreen::Playing && windowWasFocused &&
                              !inputState.windowFocused;

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
            if (flow.GetScreen() == GameScreen::Playing) {
                movementReleaseRequired = true;
                fireReleaseRequired = true;
            }
        }

        switch (flowResult.action) {
        case GameFlowAction::None:
            break;
        case GameFlowAction::RequestStartGame:
            ResetRunOrThrow();
            if (!mapScenes.BeginFirstMap()) {
                throw std::logic_error("Map manager rejected the first-level transition.");
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
        if (fireReleaseRequired) {
            if (!inputState.mouse.leftHeld) {
                fireReleaseRequired = false;
            }
            gameplayInput.mouse.leftPressed = false;
            gameplayInput.mouse.leftHeld = false;
        }

        UpdateGameplay(gameplayInput, deltaSeconds);
    }

    void UpdateGameplay(const InputState& inputState, const float deltaSeconds) {
        if (!level) {
            return;
        }

        const Float2 previousPlayerPosition = level->player.GetPositionXZ();
        const std::vector<CircleObstacle> enemyBlockers =
            level->enemies.CollectAliveColliders();
        playerController.Update(
            level->player,
            inputState,
            deltaSeconds,
            level->world.GetMap(),
            level->world.GetSettings(),
            enemyBlockers);

        // Advance existing enemies before resolving this frame's hitscan so a
        // newly applied hit flash remains visible for its full first frame.
        level->enemies.Update(
            level->world.GetMap(),
            {
                level->player.GetPositionXZ(),
                playerController.GetSettings().collisionRadius,
                playerController.GetSettings().bodyHeight,
            },
            deltaSeconds);

        const float recoveredRecoil = (std::max)(
            0.0f,
            weaponState.GetRecoilDegrees() -
                config.weapon.recoilRecoveryDegreesPerSecond * deltaSeconds);
        static_cast<void>(playerController.SetVerticalRecoilDegrees(
            level->player, recoveredRecoil));
        crosshairExpansionPixels = (std::max)(
            0.0f,
            crosshairExpansionPixels - kCrosshairRecoveryPixelsPerSecond * deltaSeconds);
        weaponKickPixels = (std::max)(
            0.0f,
            weaponKickPixels - kWeaponKickRecoveryPixelsPerSecond * deltaSeconds);
        weaponController.Update(weaponState, inputState, deltaSeconds);
        for (const ShotEvent& shot : weaponController.GetShotEvents()) {
            ResolvePlayerShot(shot);
            crosshairExpansionPixels = (std::min)(
                kCrosshairMaximumGapPixels - kCrosshairBaseGapPixels,
                crosshairExpansionPixels + shot.recoilDegrees * 4.0f);
            weaponKickPixels = (std::min)(
                80.0f, weaponKickPixels + shot.recoilDegrees * 12.0f);
        }
        static_cast<void>(playerController.SetVerticalRecoilDegrees(
            level->player, weaponState.GetRecoilDegrees()));

        const VerticalCapsule playerCapsule{
            level->player.GetPositionXZ(),
            playerController.GetSettings().bodyHeight,
            playerController.GetSettings().collisionRadius,
        };
        for (const PlayerProjectileHit& hit : level->projectiles.Update(
                 level->world.GetMap(),
                 level->world.GetSettings(),
                 playerCapsule,
                 deltaSeconds)) {
            static_cast<void>(playerCombat.ApplyDamage(hit.damage));
            if (playerCombat.IsDead()) {
                BeginDeathResults();
                SyncVisuals();
                return;
            }
        }

        for (const EnemyAttackEvent& attack : level->enemies.GetAttackEvents()) {
            if (attack.kind == EnemyKind::Melee) {
                static_cast<void>(playerCombat.ApplyDamage(attack.damage));
                if (playerCombat.IsDead()) {
                    BeginDeathResults();
                    SyncVisuals();
                    return;
                }
            } else {
                const Float3 direction = Subtract(attack.target, attack.origin);
                if (Length(direction) > kLengthEpsilon) {
                    static_cast<void>(level->projectiles.SpawnEnemyProjectile(
                        attack.origin, attack.target, attack.damage));
                }
            }
        }

        static_cast<void>(level->enemies.RetireExpiredDead());
        std::string spawnError;
        const EnemySpawnBatchResult spawnResult = level->spawnDirector.SpawnAvailable(
            level->enemies,
            level->world.GetMap(),
            level->player.GetPositionXZ(),
            playerController.GetSettings().collisionRadius,
            spawnError);
        static_cast<void>(spawnResult);
        if (!spawnError.empty()) {
            throw std::runtime_error("Enemy spawn failed: " + spawnError);
        }

        UpdateDoorState();
        SyncCamera();
        SyncVisuals();
        HandleMapExit(previousPlayerPosition);
    }

    void ResolvePlayerShot(const ShotEvent& shot) {
        if (!level) {
            return;
        }
        const Float2 position = level->player.GetPositionXZ();
        const Float3 cameraOrigin{
            position.x,
            playerController.GetSettings().eyeHeight,
            position.z,
        };
        const ViewBasis basis = MakeViewBasis(
            level->player.GetYawRadians(), level->player.GetPitchRadians());
        const std::vector<CombatTarget> targets =
            MakeCombatTargets(level->enemies.GetSnapshots());
        const std::optional<CombatHit> aimHit = CombatCollision::Raycast(
            level->world.GetMap(),
            level->world.GetSettings(),
            cameraOrigin,
            basis.forward,
            kMaximumShotDistance,
            targets);
        const Float3 aimPoint = aimHit.has_value()
                                    ? aimHit->position
                                    : AddScaled(
                                          cameraOrigin,
                                          basis.forward,
                                          kMaximumShotDistance);
        Float3 muzzle = AddScaled(cameraOrigin, basis.forward, kMuzzleForwardOffset);
        muzzle = AddScaled(muzzle, basis.right, kMuzzleRightOffset);
        muzzle = AddScaled(muzzle, basis.up, -kMuzzleDownOffset);

        std::optional<CombatHit> resolvedHit;
        Float3 resolvedPoint = aimPoint;
        const Float3 muzzleToAim = Subtract(aimPoint, muzzle);
        const float muzzleDistance = Length(muzzleToAim);
        if (muzzleDistance > kLengthEpsilon) {
            resolvedHit = CombatCollision::Raycast(
                level->world.GetMap(),
                level->world.GetSettings(),
                muzzle,
                muzzleToAim,
                muzzleDistance,
                targets);
            if (resolvedHit.has_value()) {
                resolvedPoint = resolvedHit->position;
            } else {
                resolvedHit = aimHit;
            }
        } else {
            resolvedHit = aimHit;
        }

        if (resolvedHit.has_value() && resolvedHit->kind == CombatHitKind::Target) {
            const EnemyDamageResult damage = level->enemies.ApplyDamage(
                resolvedHit->targetId, shot.damage);
            if (damage.killed) {
                const LevelDefinition& definition =
                    orderedLevels[level->definitionIndex];
                static_cast<void>(campaign.RecordKill(definition.id));
            }
        }
        static_cast<void>(level->projectiles.SpawnPlayerTracer(muzzle, resolvedPoint));
    }

    void BeginDeathResults() {
        if (campaign.GetOutcome() == CampaignOutcome::InProgress) {
            campaign.Fail();
            if (!mapScenes.BeginResults()) {
                throw std::logic_error("Map manager rejected the death Results transition.");
            }
        }
    }

    void UpdateDoorState() {
        if (!level || level->doorVisible) {
            return;
        }
        const LevelDefinition& definition = orderedLevels[level->definitionIndex];
        const CampaignRoomStats* const room = campaign.FindRoom(definition.id);
        if (room == nullptr || room->kills < definition.clearKillCount) {
            return;
        }

        level->doorVisible = true;
        level->renderer.SetDoorVisible(true);
        level->exitRequiresLeave = IsPlayerOnExit();
    }

    [[nodiscard]] bool IsPlayerOnExit() const {
        if (!level) {
            return false;
        }
        const GridMap& map = level->world.GetMap();
        const std::optional<GridCoordinate> coordinate = map.TryGetCoordinateAtPosition(
            level->player.GetPositionXZ(), level->world.GetSettings().cellSize);
        return coordinate.has_value() &&
               map.GetTile(coordinate->row, coordinate->column) ==
                   TileType::NextMapExit;
    }

    void HandleMapExit(const Float2 previousPlayerPosition) {
        if (!level || !level->doorVisible) {
            return;
        }

        const GridMap& map = level->world.GetMap();
        const bool endedOnExit = IsPlayerOnExit();
        const bool crossedExit = SegmentIntersectsCell(
            previousPlayerPosition,
            level->player.GetPositionXZ(),
            map.GetNextMapExitCell(),
            level->world.GetSettings().cellSize);
        if (level->exitRequiresLeave) {
            if (!endedOnExit) {
                level->exitRequiresLeave = false;
            }
            return;
        }
        if (!endedOnExit && !crossedExit) {
            return;
        }

        const LevelDefinition& definition = orderedLevels[level->definitionIndex];
        if (!definition.nextLevelId.has_value()) {
            campaign.Complete();
            if (!mapScenes.BeginResults()) {
                throw std::logic_error("Map manager rejected the successful Results transition.");
            }
            return;
        }

        const std::size_t nextIndex = level->definitionIndex + 1;
        if (nextIndex >= orderedLevels.size() ||
            orderedLevels[nextIndex].id != *definition.nextLevelId ||
            !mapScenes.BeginMap(nextIndex)) {
            throw std::logic_error("Map manager rejected the configured next-level transition.");
        }
    }

    void SyncVisuals() {
        if (!level) {
            return;
        }
        level->enemyRenderer.Sync(
            level->enemies.GetSnapshots(), level->player.GetPositionXZ());
        level->projectileRenderer.Sync(level->projectiles.GetSnapshots());
    }

    void CommitPendingScene() {
        const std::optional<MapSceneDestination> destination =
            mapScenes.GetCommitDestination();
        if (!destination.has_value()) {
            throw std::logic_error(
                "Map transition requested a commit without a destination.");
        }

        switch (destination->kind) {
        case MapSceneDestinationKind::Map: {
            const bool canUsePreparedFirstLevel =
                firstLevelPrepared && level && destination->mapIndex == 0 &&
                !mapScenes.GetActiveMapIndex().has_value();
            if (!canUsePreparedFirstLevel) {
                level = BuildLevelOrThrow(destination->mapIndex);
            }
            if (!level || destination->mapIndex >= orderedLevels.size() ||
                !campaign.EnterRoom(orderedLevels[destination->mapIndex].id)) {
                throw std::logic_error("Campaign rejected the committed room.");
            }

            firstLevelPrepared = false;
            weaponController.ResetVisualFeedback(weaponState);
            playerController.ClearVerticalRecoil(level->player);
            crosshairExpansionPixels = 0.0f;
            weaponKickPixels = 0.0f;
            SyncCamera();
            SyncVisuals();
            flow.EnterPlaying();
            input.SetMouseCaptureEnabled(true);
            movementReleaseRequired = true;
            fireReleaseRequired = true;
            break;
        }
        case MapSceneDestinationKind::Results:
            RefreshResultEntries();
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

    void RefreshResultEntries() {
        resultEntries.clear();
        for (const CampaignRoomStats& room : campaign.GetRooms()) {
            if (room.visited) {
                resultEntries.push_back({room.levelName, room.kills});
            }
        }
    }

    [[nodiscard]] GameHudState MakeHudState() const {
        const WeaponHudSnapshot weaponHud =
            weaponController.MakeHudSnapshot(weaponState);
        return {
            static_cast<int>(std::ceil((std::max)(0.0f, playerCombat.GetHealth()))),
            SaturatingInt(weaponHud.magazineAmmo),
            SaturatingInt(weaponHud.reserveAmmo),
            weaponHud.reloading,
            weaponHud.reloadProgress,
            kCrosshairBaseGapPixels + crosshairExpansionPixels,
            weaponKickPixels,
        };
    }

    void Draw() const {
        const GameScreen screen = flow.GetScreen();
        if ((screen == GameScreen::Playing || screen == GameScreen::Paused) && level) {
            scenePostProcess.BeginScene();
            sky.Draw(camera.GetNativeCamera());
            level->renderer.Draw(camera.GetNativeCamera());
            level->enemyRenderer.Draw(camera.GetNativeCamera());
            level->projectileRenderer.Draw(camera.GetNativeCamera());
            scenePostProcess.Composite();
            hud.Draw(MakeHudState());
        }

        const std::optional<GameUiScreen> uiScreen = ToUiScreen(screen);
        if (uiScreen.has_value()) {
            if (*uiScreen == GameUiScreen::Results) {
                const ResultsUiView results{
                    campaign.GetOutcome() == CampaignOutcome::Completed,
                    resultEntries,
                };
                ui.Draw(*uiScreen, flow.GetSelectedItem(), &results);
            } else {
                ui.Draw(*uiScreen, flow.GetSelectedItem());
            }
        }
        screenFade.Draw(mapScenes.GetFadeOpacity());
    }

    void SyncCamera() {
        if (!level) {
            return;
        }
        const Float2 position = level->player.GetPositionXZ();
        const Float3 cameraPosition{
            position.x, playerController.GetSettings().eyeHeight, position.z};
        camera.Sync(
            cameraPosition,
            level->player.GetYawRadians(),
            level->player.GetPitchRadians());
        sky.Sync(cameraPosition);
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
