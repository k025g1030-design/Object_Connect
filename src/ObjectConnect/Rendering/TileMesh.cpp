#include "ObjectConnect/Rendering/TileMesh.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace object_connect {
namespace {

constexpr std::size_t kVerticesPerTile = 6;
constexpr std::size_t kInitialMeshCapacity = 256;

[[nodiscard]] bool IsFiniteColor(const Color color) noexcept {
    return std::isfinite(color.r) && std::isfinite(color.g) &&
           std::isfinite(color.b) && std::isfinite(color.a);
}

[[nodiscard]] bool TryGetCellCount(const TileGrid& grid,
                                   std::size_t& count) noexcept {
    count = 0;
    if (grid.columns == 0 || grid.rows == 0) {
        return grid.columns == 0 && grid.rows == 0 && grid.cells.empty();
    }
    if (grid.columns > (std::numeric_limits<std::size_t>::max)() / grid.rows) {
        return false;
    }
    count = grid.columns * grid.rows;
    return grid.cells.size() == count;
}

[[nodiscard]] bool TryGetVertexCount(const TileGrid& grid,
                                     std::size_t& count) noexcept {
    const std::size_t occupied = static_cast<std::size_t>(
        std::count_if(grid.cells.begin(), grid.cells.end(),
                      [](const TileId id) noexcept { return id != 0; }));
    if (occupied > (std::numeric_limits<std::size_t>::max)() /
                       kVerticesPerTile) {
        return false;
    }
    count = occupied * kVerticesPerTile;
    return true;
}

void AppendQuad(std::vector<TileMeshVertex>& vertices,
                const Vec2 topLeft, const Vec2 bottomRight,
                const Vec2 uvTopLeft, const Vec2 uvBottomRight,
                const Color tint) {
    const Vec2 topRight{bottomRight.x, topLeft.y};
    const Vec2 bottomLeft{topLeft.x, bottomRight.y};
    const Vec2 uvTopRight{uvBottomRight.x, uvTopLeft.y};
    const Vec2 uvBottomLeft{uvTopLeft.x, uvBottomRight.y};

    vertices.push_back({topLeft, uvTopLeft, tint});
    vertices.push_back({topRight, uvTopRight, tint});
    vertices.push_back({bottomLeft, uvBottomLeft, tint});
    vertices.push_back({bottomLeft, uvBottomLeft, tint});
    vertices.push_back({topRight, uvTopRight, tint});
    vertices.push_back({bottomRight, uvBottomRight, tint});
}

} // namespace

bool BuildTileMesh(const TileGrid& grid, const TilesetDefinition& tileset,
                   const Vec2 origin, const float tileSize,
                   std::vector<TileMeshVertex>& output, std::string& error,
                   const std::span<const Color> cellTints,
                   const Color defaultTint) {
    error.clear();
    std::vector<TileMeshVertex> next;

    std::size_t cellCount = 0;
    if (!TryGetCellCount(grid, cellCount)) {
        error = "Tile mesh grid dimensions do not match its cell count.";
        output.clear();
        return false;
    }
    if (!IsFinite(origin) || !std::isfinite(tileSize) || tileSize <= 0.0f) {
        error = "Tile mesh origin and tile size must be finite, with a positive size.";
        output.clear();
        return false;
    }
    if (!cellTints.empty() && cellTints.size() != cellCount) {
        error = "Tile mesh tint count must match the grid cell count.";
        output.clear();
        return false;
    }
    if (!IsFiniteColor(defaultTint)) {
        error = "Tile mesh default tint must contain only finite values.";
        output.clear();
        return false;
    }
    if (cellCount == 0) {
        output.clear();
        return true;
    }
    if (tileset.atlasColumns == 0 || tileset.atlasRows == 0) {
        error = "Tile mesh atlas dimensions must be greater than zero.";
        output.clear();
        return false;
    }

    std::size_t vertexCount = 0;
    if (!TryGetVertexCount(grid, vertexCount)) {
        error = "Tile mesh vertex count exceeds the supported size.";
        output.clear();
        return false;
    }
    next.reserve(vertexCount);

    const double atlasColumns = static_cast<double>(tileset.atlasColumns);
    const double atlasRows = static_cast<double>(tileset.atlasRows);
    const double maximumFloat = (std::numeric_limits<float>::max)();
    for (std::size_t row = 0; row < grid.rows; ++row) {
        for (std::size_t column = 0; column < grid.columns; ++column) {
            const std::size_t index = row * grid.columns + column;
            const TileId id = grid.cells[index];
            if (id == 0) {
                continue;
            }

            const TileDefinition* const tile = tileset.Find(id);
            if (tile == nullptr) {
                error = "Tile mesh references unknown tile ID " +
                        std::to_string(id) + ".";
                output.clear();
                return false;
            }
            if (tile->atlasColumn >= tileset.atlasColumns ||
                tile->atlasRow >= tileset.atlasRows) {
                error = "Tile mesh atlas coordinate is outside the tileset for tile ID " +
                        std::to_string(id) + ".";
                output.clear();
                return false;
            }

            const Color tint = cellTints.empty() ? defaultTint : cellTints[index];
            if (!IsFiniteColor(tint)) {
                error = "Tile mesh tint must contain only finite values.";
                output.clear();
                return false;
            }

            const double left = static_cast<double>(origin.x) +
                                static_cast<double>(column) * tileSize;
            const double top = static_cast<double>(origin.y) +
                               static_cast<double>(row) * tileSize;
            const double right = left + tileSize;
            const double bottom = top + tileSize;
            const double u0 = static_cast<double>(tile->atlasColumn) / atlasColumns;
            const double v0 = static_cast<double>(tile->atlasRow) / atlasRows;
            const double u1 = static_cast<double>(tile->atlasColumn + 1) / atlasColumns;
            const double v1 = static_cast<double>(tile->atlasRow + 1) / atlasRows;
            if (!std::isfinite(left) || !std::isfinite(top) ||
                !std::isfinite(right) || !std::isfinite(bottom) ||
                std::abs(left) > maximumFloat || std::abs(top) > maximumFloat ||
                std::abs(right) > maximumFloat || std::abs(bottom) > maximumFloat ||
                !std::isfinite(u0) || !std::isfinite(v0) ||
                !std::isfinite(u1) || !std::isfinite(v1)) {
                error = "Tile mesh generated a non-finite vertex.";
                output.clear();
                return false;
            }

            AppendQuad(next,
                       {static_cast<float>(left), static_cast<float>(top)},
                       {static_cast<float>(right), static_cast<float>(bottom)},
                       {static_cast<float>(u0), static_cast<float>(v0)},
                       {static_cast<float>(u1), static_cast<float>(v1)}, tint);
        }
    }

    output = std::move(next);
    return true;
}

bool BuildTileStampMesh(const TileStamp& stamp,
                        const TilesetDefinition& tileset,
                        const Vec2 origin, const float tileSize,
                        const Color tint,
                        std::vector<TileMeshVertex>& output,
                        std::string& error) {
    TileGrid grid;
    grid.columns = stamp.columns;
    grid.rows = stamp.rows;
    grid.cells = stamp.cells;
    return BuildTileMesh(grid, tileset, origin, tileSize, output, error, {}, tint);
}

std::size_t GrowTileMeshCapacity(const std::size_t currentCapacity,
                                 const std::size_t requiredCapacity) noexcept {
    if (requiredCapacity <= currentCapacity) {
        return currentCapacity;
    }

    std::size_t capacity = (std::max)(currentCapacity, kInitialMeshCapacity);
    while (capacity < requiredCapacity) {
        if (capacity > (std::numeric_limits<std::size_t>::max)() / 2) {
            return requiredCapacity;
        }
        capacity *= 2;
    }
    return capacity;
}

} // namespace object_connect
