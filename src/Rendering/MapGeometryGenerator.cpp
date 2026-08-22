#include "RetroFPS/Rendering/MapGeometryGenerator.hpp"

#include "RetroFPS/World/GridMap.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace fps {
namespace {

struct WallDirection {
    std::ptrdiff_t rowOffset;
    std::ptrdiff_t columnOffset;
    Float3 normal;
    float yawRadians;
    Float2 boundaryOffset;
};

constexpr float kPi = std::numbers::pi_v<float>;
constexpr std::array<WallDirection, 4> kWallDirections = {{
    {-1, 0, {0.0f, 0.0f, 1.0f}, 0.0f, {0.0f, -0.5f}},
    {0, 1, {-1.0f, 0.0f, 0.0f}, -kPi * 0.5f, {0.5f, 0.0f}},
    {1, 0, {0.0f, 0.0f, -1.0f}, kPi, {0.0f, 0.5f}},
    {0, -1, {1.0f, 0.0f, 0.0f}, kPi * 0.5f, {-0.5f, 0.0f}},
}};

void ValidateSettings(const WorldSettings& settings) {
    if (!std::isfinite(settings.cellSize) || settings.cellSize <= 0.0f) {
        throw std::invalid_argument("cell size must be finite and greater than zero");
    }
    if (!std::isfinite(settings.wallHeight) || settings.wallHeight <= 0.0f) {
        throw std::invalid_argument("wall height must be finite and greater than zero");
    }
}

} // namespace

MapGeometry MapGeometryGenerator::Generate(
    const GridMap& map, const WorldSettings& settings) {
    ValidateSettings(settings);

    MapGeometry geometry;
    geometry.surfaces.reserve(map.GetWidth() * map.GetHeight() * 3);

    for (std::size_t row = 0; row < map.GetHeight(); ++row) {
        for (std::size_t column = 0; column < map.GetWidth(); ++column) {
            const auto signedRow = static_cast<std::ptrdiff_t>(row);
            const auto signedColumn = static_cast<std::ptrdiff_t>(column);
            if (!map.IsWalkable(signedRow, signedColumn)) {
                continue;
            }

            const float centerX = (static_cast<float>(column) + 0.5f) * settings.cellSize;
            const float centerZ = (static_cast<float>(row) + 0.5f) * settings.cellSize;

            geometry.surfaces.push_back({
                SurfaceType::Floor,
                {
                    {centerX, 0.0f, centerZ},
                    {0.0f, 0.0f, 0.0f},
                    {settings.cellSize, 1.0f, settings.cellSize},
                },
                {0.0f, 1.0f, 0.0f},
                row,
                column,
            });

            for (const WallDirection& direction : kWallDirections) {
                if (!map.IsSolid(
                        signedRow + direction.rowOffset,
                        signedColumn + direction.columnOffset)) {
                    continue;
                }

                geometry.surfaces.push_back({
                    SurfaceType::Wall,
                    {
                        {
                            centerX + direction.boundaryOffset.x * settings.cellSize,
                            settings.wallHeight * 0.5f,
                            centerZ + direction.boundaryOffset.z * settings.cellSize,
                        },
                        {0.0f, direction.yawRadians, 0.0f},
                        {settings.cellSize, settings.wallHeight, 1.0f},
                    },
                    direction.normal,
                    row,
                    column,
                });
            }
        }
    }

    return geometry;
}

} // namespace fps
