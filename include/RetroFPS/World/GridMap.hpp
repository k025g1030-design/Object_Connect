#pragma once

#include "RetroFPS/Math/Vector.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace fps {

class GridMapLoader;

struct GridCoordinate {
    std::size_t row = 0;
    std::size_t column = 0;
};

class GridMap final {
public:
    [[nodiscard]] std::size_t GetWidth() const noexcept { return width_; }
    [[nodiscard]] std::size_t GetHeight() const noexcept { return rows_.size(); }
    [[nodiscard]] GridCoordinate GetSpawnCell() const noexcept { return spawnCell_; }
    [[nodiscard]] Float2 GetSpawnPosition(float cellSize = 1.0f) const;

    [[nodiscard]] char GetCell(std::size_t row, std::size_t column) const;
    [[nodiscard]] bool IsSolid(std::ptrdiff_t row, std::ptrdiff_t column) const noexcept;
    [[nodiscard]] bool IsWalkable(std::ptrdiff_t row, std::ptrdiff_t column) const noexcept;

private:
    friend class GridMapLoader;

    GridMap(std::vector<std::string> rows, GridCoordinate spawnCell);

    std::vector<std::string> rows_;
    std::size_t width_ = 0;
    GridCoordinate spawnCell_{};
};

} // namespace fps
