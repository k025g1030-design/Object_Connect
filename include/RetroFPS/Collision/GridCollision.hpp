#pragma once

#include "RetroFPS/Math/Vector.hpp"
#include "RetroFPS/World/GridMap.hpp"

#include <span>

namespace fps {

struct CircleObstacle {
    Float2 center{};
    float radius = 0.0f;
};

class GridCollision final {
public:
    // 厳密な相切は重なりとして扱わない。
    [[nodiscard]] static bool OverlapsCircle(
        Float2 center,
        float radius,
        const CircleObstacle& obstacle);

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

    // 動的な円形障害物にも衝突する移動。開始地点が障害物と重なっている場合は
    // 移動方向を一意に解決できないため invalid_argument を送出する。
    [[nodiscard]] static Float2 MoveCircle(
        const GridMap& map,
        Float2 start,
        Float2 displacement,
        float radius,
        std::span<const CircleObstacle> obstacles,
        float cellSize = 1.0f);
};

} // namespace fps
