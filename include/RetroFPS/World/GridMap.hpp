#pragma once

#include "RetroFPS/Math/Vector.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace fps {

class GridMapLoader;

enum class TileType : std::uint8_t {
    Floor,
    Wall,
    PlayerSpawn,
    MeleeEnemySpawn,
    RangedEnemySpawn,
    NextMapExit,
};

enum class EnemyKind : std::uint8_t {
    Melee,
    Ranged,
};

struct GridCoordinate {
    std::size_t row = 0;
    std::size_t column = 0;

    [[nodiscard]] bool operator==(const GridCoordinate&) const noexcept = default;
};

struct EnemySpawnPoint {
    EnemyKind kind = EnemyKind::Melee;
    GridCoordinate cell{};

    [[nodiscard]] bool operator==(const EnemySpawnPoint&) const noexcept = default;
};

class GridMap final {
public:
    [[nodiscard]] std::size_t GetWidth() const noexcept { return width_; }
    [[nodiscard]] std::size_t GetHeight() const noexcept { return height_; }
    [[nodiscard]] GridCoordinate GetPlayerSpawnCell() const noexcept {
        return playerSpawnCell_;
    }
    [[nodiscard]] const std::vector<EnemySpawnPoint>& GetEnemySpawnPoints() const noexcept {
        return enemySpawnPoints_;
    }
    [[nodiscard]] GridCoordinate GetNextMapExitCell() const noexcept {
        return nextMapExitCell_;
    }

    // Compatibility accessors for the existing player initialization path.
    [[nodiscard]] GridCoordinate GetSpawnCell() const noexcept {
        return GetPlayerSpawnCell();
    }
    [[nodiscard]] Float2 GetSpawnPosition(float cellSize = 1.0f) const;
    [[nodiscard]] Float2 GetCellCenter(
        GridCoordinate coordinate, float cellSize = 1.0f) const;
    [[nodiscard]] std::optional<GridCoordinate> TryGetCoordinateAtPosition(
        Float2 position, float cellSize = 1.0f) const;

    [[nodiscard]] TileType GetTile(std::size_t row, std::size_t column) const;
    [[nodiscard]] bool IsSolid(std::ptrdiff_t row, std::ptrdiff_t column) const noexcept;
    [[nodiscard]] bool IsWalkable(std::ptrdiff_t row, std::ptrdiff_t column) const noexcept;

private:
    friend class GridMapLoader;

    GridMap(
        std::vector<TileType> tiles,
        std::size_t width,
        std::size_t height,
        GridCoordinate playerSpawnCell,
        std::vector<EnemySpawnPoint> enemySpawnPoints,
        GridCoordinate nextMapExitCell);

    std::vector<TileType> tiles_;
    std::size_t width_ = 0;
    std::size_t height_ = 0;
    GridCoordinate playerSpawnCell_{};
    std::vector<EnemySpawnPoint> enemySpawnPoints_;
    GridCoordinate nextMapExitCell_{};
};

} // namespace fps
