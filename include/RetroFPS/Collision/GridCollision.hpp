#pragma once

#include "RetroFPS/Math/Vector.hpp"
#include "RetroFPS/World/GridMap.hpp"

namespace fps {

class GridCollision final {
public:
    [[nodiscard]] static bool OverlapsSolid(
        const GridMap& map,
        Float2 center,
        float radius,
        float cellSize = 1.0f);

    // 変位を半径の1/2以下のサブステップに分割する。各サブステップではX、Zの順に
    // 解決し、一方の軸が塞がれていても壁沿いに移動できるようにする。
    [[nodiscard]] static Float2 MoveCircle(
        const GridMap& map,
        Float2 start,
        Float2 displacement,
        float radius,
        float cellSize = 1.0f);
};

} // namespace fps
