#pragma once

#include "RetroFPS/Data/GameData.hpp"
#include "RetroFPS/Game/MapSceneManager.hpp"
#include "RetroFPS/Gameplay/Combat/ProjectileSystem.hpp"
#include "RetroFPS/Gameplay/Enemy/EnemySystem.hpp"
#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"
#include "RetroFPS/Gameplay/Weapon/WeaponState.hpp"
#include "RetroFPS/Rendering/CameraSettings.hpp"
#include "RetroFPS/Rendering/EnemyRenderSettings.hpp"
#include "RetroFPS/Rendering/MapRenderAssets.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <string>

namespace fps {

// 上位層の構成設定。各サブシステムが担当セクションの検証と
// 解釈を行う。
struct GameConfig final {
    GameDataPaths data{};
    std::string startLevelId{"room_01"};
    std::string startingWeaponId{"starter_pistol"};
    std::string meleeEnemyId{"melee_basic"};
    std::string rangedEnemyId{"ranged_basic"};
    WorldSettings world{};
    PlayerSettings player{};
    EnemySettings enemies{};
    WeaponControllerSettings weapon{};
    ProjectileSettings projectiles{};
    CameraSettings camera{};
    EnemyRenderSettings enemyRendering{};
    MapRenderAssets mapRendering{};
    MapSceneTransitionSettings mapTransition{};
};

} // namespace fps
