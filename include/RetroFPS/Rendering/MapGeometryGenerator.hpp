#pragma once

#include "RetroFPS/Rendering/MapGeometry.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

namespace fps {

class GridMap;

class MapGeometryGenerator final {
public:
    // 床の元メッシュは原点中心、法線+Yの単位XZクアッド。
    // 壁の元メッシュは原点中心、法線+Zの単位XYクアッド。
    [[nodiscard]] static MapGeometry Generate(
        const GridMap& map, const WorldSettings& settings = {});
};

} // namespace fps
