#pragma once

#include "RetroFPS/Math/Vector.hpp"

#include <cstddef>
#include <vector>

namespace fps {

enum class SurfaceType {
    Floor,
    Wall,
    Door,
};

struct SurfaceTransform {
    Float3 translation{};
    Float3 rotationRadians{};
    Float3 scale{1.0f, 1.0f, 1.0f};
};

struct SurfaceInstance {
    SurfaceType type = SurfaceType::Floor;
    SurfaceTransform transform{};
    Float3 normal{0.0f, 1.0f, 0.0f};
    std::size_t row = 0;
    std::size_t column = 0;
};

// 静的グリッドマップ用のCPU側描画データ。
// ビルボードなど他の描画形式は、独立したレンダラーとジオメトリ形式を使用できる。
struct MapGeometry {
    std::vector<SurfaceInstance> surfaces;
};

} // namespace fps
