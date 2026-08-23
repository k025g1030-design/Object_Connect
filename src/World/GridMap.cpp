#include "RetroFPS/World/GridMap.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace fps {

GridMap::GridMap(
    std::vector<TileType> tiles,
    const std::size_t width,
    const std::size_t height,
    const GridCoordinate playerSpawnCell,
    std::vector<EnemySpawnPoint> enemySpawnPoints,
    const GridCoordinate nextMapExitCell)
    : tiles_(std::move(tiles)),
      width_(width),
      height_(height),
      playerSpawnCell_(playerSpawnCell),
      enemySpawnPoints_(std::move(enemySpawnPoints)),
      nextMapExitCell_(nextMapExitCell) {}

Float2 GridMap::GetSpawnPosition(const float cellSize) const {
    return GetCellCenter(playerSpawnCell_, cellSize);
}

Float2 GridMap::GetCellCenter(
    const GridCoordinate coordinate, const float cellSize) const {
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) {
        throw std::invalid_argument("cell size must be finite and greater than zero");
    }
    if (coordinate.row >= height_ || coordinate.column >= width_) {
        throw std::out_of_range("grid coordinate is outside the map");
    }

    return {
        (static_cast<float>(coordinate.column) + 0.5f) * cellSize,
        (static_cast<float>(coordinate.row) + 0.5f) * cellSize,
    };
}

std::optional<GridCoordinate> GridMap::TryGetCoordinateAtPosition(
    const Float2 position, const float cellSize) const {
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) {
        throw std::invalid_argument("cell size must be finite and greater than zero");
    }
    if (!std::isfinite(position.x) || !std::isfinite(position.z) ||
        position.x < 0.0f || position.z < 0.0f) {
        return std::nullopt;
    }

    const double column = std::floor(
        static_cast<double>(position.x) / static_cast<double>(cellSize));
    const double row = std::floor(
        static_cast<double>(position.z) / static_cast<double>(cellSize));
    if (column >= static_cast<double>(width_) || row >= static_cast<double>(height_)) {
        return std::nullopt;
    }

    return GridCoordinate{
        static_cast<std::size_t>(row),
        static_cast<std::size_t>(column),
    };
}

TileType GridMap::GetTile(const std::size_t row, const std::size_t column) const {
    if (row >= height_ || column >= width_) {
        throw std::out_of_range("grid cell is outside the map");
    }
    return tiles_[row * width_ + column];
}

bool GridMap::IsSolid(const std::ptrdiff_t row, const std::ptrdiff_t column) const noexcept {
    if (row < 0 || column < 0) {
        return true;
    }

    const auto unsignedRow = static_cast<std::size_t>(row);
    const auto unsignedColumn = static_cast<std::size_t>(column);
    if (unsignedRow >= height_ || unsignedColumn >= width_) {
        return true;
    }

    return tiles_[unsignedRow * width_ + unsignedColumn] == TileType::Wall;
}

bool GridMap::IsWalkable(
    const std::ptrdiff_t row, const std::ptrdiff_t column) const noexcept {
    if (row < 0 || column < 0) {
        return false;
    }

    const auto unsignedRow = static_cast<std::size_t>(row);
    const auto unsignedColumn = static_cast<std::size_t>(column);
    if (unsignedRow >= height_ || unsignedColumn >= width_) {
        return false;
    }

    return tiles_[unsignedRow * width_ + unsignedColumn] != TileType::Wall;
}

} // namespace fps
