#pragma once

#include "RetroFPS/Game/MapSceneManager.hpp"
#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"
#include "RetroFPS/Rendering/CameraSettings.hpp"
#include "RetroFPS/Rendering/MapRenderAssets.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <filesystem>
#include <vector>

namespace fps {

// 上位層の構成設定。各サブシステムが担当セクションの検証と
// 解釈を行う。
struct GameConfig final {
    std::vector<std::filesystem::path> mapPaths{
        "Resources/maps/mvp_map.txt",
        "Resources/maps/mvp_map_02.txt",
    };
    WorldSettings world{};
    PlayerSettings player{};
    CameraSettings camera{};
    MapRenderAssets mapRendering{};
    MapSceneTransitionSettings mapTransition{};
};

} // namespace fps
