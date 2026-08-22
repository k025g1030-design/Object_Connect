#include "RetroFPS/World/GridMap.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace fps {

GridMap::GridMap(std::vector<std::string> rows, const GridCoordinate spawnCell)
    : rows_(std::move(rows)), width_(rows_.front().size()), spawnCell_(spawnCell) {}

Float2 GridMap::GetSpawnPosition(const float cellSize) const {
    if (!std::isfinite(cellSize) || cellSize <= 0.0f) {
        throw std::invalid_argument("cell size must be finite and greater than zero");
    }

    return {
        (static_cast<float>(spawnCell_.column) + 0.5f) * cellSize,
        (static_cast<float>(spawnCell_.row) + 0.5f) * cellSize,
    };
}

char GridMap::GetCell(const std::size_t row, const std::size_t column) const {
    if (row >= rows_.size() || column >= width_) {
        throw std::out_of_range("grid cell is outside the map");
    }
    return rows_[row][column];
}

bool GridMap::IsSolid(const std::ptrdiff_t row, const std::ptrdiff_t column) const noexcept {
    if (row < 0 || column < 0) {
        return true;
    }

    const auto unsignedRow = static_cast<std::size_t>(row);
    const auto unsignedColumn = static_cast<std::size_t>(column);
    if (unsignedRow >= rows_.size() || unsignedColumn >= width_) {
        return true;
    }

    return rows_[unsignedRow][unsignedColumn] == '#';
}

bool GridMap::IsWalkable(
    const std::ptrdiff_t row, const std::ptrdiff_t column) const noexcept {
    if (row < 0 || column < 0) {
        return false;
    }

    const auto unsignedRow = static_cast<std::size_t>(row);
    const auto unsignedColumn = static_cast<std::size_t>(column);
    if (unsignedRow >= rows_.size() || unsignedColumn >= width_) {
        return false;
    }

    return rows_[unsignedRow][unsignedColumn] != '#';
}

} // namespace fps
