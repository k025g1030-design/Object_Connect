#pragma once

#include "RetroFPS/Gameplay/Player/PlayerSettings.hpp"
#include "RetroFPS/Rendering/CameraSettings.hpp"
#include "RetroFPS/Rendering/MapRenderAssets.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <filesystem>

namespace fps {

// 上位層の構成設定。各サブシステムが担当セクションの検証と
// 解釈を行う。
struct GameConfig final {
    std::filesystem::path mapPath{"Resources/maps/mvp_map.txt"};
    WorldSettings world{};
    PlayerSettings player{};
    CameraSettings camera{};
    MapRenderAssets mapRendering{};
};

} // namespace fps
