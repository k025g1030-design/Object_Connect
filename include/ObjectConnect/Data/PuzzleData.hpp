#pragma once

#include "ObjectConnect/Math/Color.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace object_connect {

inline constexpr std::size_t kPuzzleCanvasWidth = 1280;
inline constexpr std::size_t kPuzzleCanvasHeight = 720;
inline constexpr std::size_t kPuzzleTileSize = 16;
inline constexpr std::size_t kPuzzleGridColumns =
    kPuzzleCanvasWidth / kPuzzleTileSize;
inline constexpr std::size_t kPuzzleGridRows =
    kPuzzleCanvasHeight / kPuzzleTileSize;

using TileId = std::uint16_t;

struct TileCoordinate final {
    std::size_t column = 0;
    std::size_t row = 0;

    [[nodiscard]] bool operator==(const TileCoordinate&) const noexcept = default;
};

struct TileBounds final {
    std::size_t column = 0;
    std::size_t row = 0;
    std::size_t columns = 0;
    std::size_t rows = 0;

    [[nodiscard]] bool operator==(const TileBounds&) const noexcept = default;
};

struct TileGrid final {
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::vector<TileId> cells;

    [[nodiscard]] TileId At(const std::size_t column,
                            const std::size_t row) const {
        if (column >= columns || row >= rows) {
            throw std::out_of_range("tile coordinate is outside the grid");
        }
        const std::size_t index = row * columns + column;
        if (index >= cells.size()) {
            throw std::out_of_range("tile grid storage is incomplete");
        }
        return cells[index];
    }

    [[nodiscard]] bool Empty() const noexcept {
        return columns == 0 || rows == 0 || cells.empty();
    }
};

struct TileStamp final {
    std::size_t columns = 0;
    std::size_t rows = 0;
    std::vector<TileId> cells;
    std::vector<std::uint8_t> occupiedMask;
    TileCoordinate anchor{};

    [[nodiscard]] TileId At(const std::size_t column,
                            const std::size_t row) const {
        if (column >= columns || row >= rows) {
            throw std::out_of_range("tile coordinate is outside the stamp");
        }
        const std::size_t index = row * columns + column;
        if (index >= cells.size()) {
            throw std::out_of_range("tile stamp storage is incomplete");
        }
        return cells[index];
    }

    [[nodiscard]] bool IsOccupied(const std::size_t column,
                                  const std::size_t row) const noexcept {
        if (column >= columns || row >= rows) {
            return false;
        }
        const std::size_t index = row * columns + column;
        return index < occupiedMask.size() && occupiedMask[index] != 0;
    }
};

struct TileDefinition final {
    TileId id = 0;
    std::string name;
    std::size_t atlasColumn = 0;
    std::size_t atlasRow = 0;
};

struct TilesetDefinition final {
    std::string atlasPath;
    std::size_t atlasColumns = 0;
    std::size_t atlasRows = 0;
    std::vector<TileDefinition> tiles;

    [[nodiscard]] const TileDefinition* Find(const TileId id) const noexcept {
        for (const TileDefinition& tile : tiles) {
            if (tile.id == id) {
                return &tile;
            }
        }
        return nullptr;
    }
};

struct NodeTypeDefinition final {
    std::string typeId;
    std::string displayName;
    TileStamp stamp;
};

struct NodeDefinition final {
    std::string id;
    std::string typeId;
    std::size_t typeIndex = 0;
    std::string displayName;
    TileCoordinate origin{};
    TileStamp stamp;
    TileBounds bounds{};
    Vec2 anchorPosition{};
    bool isRoot = false;
    bool isGoal = false;
    std::size_t maxIncoming = 0;
    std::size_t maxOutgoing = 0;
};

struct ConnectionDefinition final {
    std::string id;
    std::string fromNodeId;
    std::string toNodeId;
    std::size_t fromNodeIndex = 0;
    std::size_t toNodeIndex = 0;
    std::size_t pointCount = 10;
    float thicknessScale = 1.0f;
    float followDelaySeconds = 0.0f;
    float initialDirectionDegrees = 0.0f;
};

struct PuzzleDefinition final {
    std::string id;
    std::string title;
    float totalLength = 0.0f;
    float minimumSlackRatio = 1.05f;
    Color backgroundColor{};
    bool showTargetConnections = true;
    Color vesselColor{134.0f / 255.0f, 27.0f / 255.0f,
                      43.0f / 255.0f, 1.0f};
    float baseWidth = 16.0f;
    float tipWidth = 16.0f;
    float widthVariation = 0.16f;

    TileGrid backgroundTiles;
    TileGrid obstacleTiles;
    std::vector<NodeDefinition> nodes;
    std::vector<ConnectionDefinition> connections;
    std::vector<std::size_t> rootNodeIndices;
    std::vector<std::size_t> goalNodeIndices;
};

class PuzzleCatalog final {
public:
    PuzzleCatalog() = default;
    explicit PuzzleCatalog(std::vector<PuzzleDefinition> puzzles)
        : puzzles_(std::move(puzzles)) {}
    PuzzleCatalog(TilesetDefinition tileset,
                  std::vector<NodeTypeDefinition> nodeTypes,
                  std::vector<PuzzleDefinition> puzzles)
        : tileset_(std::move(tileset)),
          nodeTypes_(std::move(nodeTypes)),
          puzzles_(std::move(puzzles)) {}

    [[nodiscard]] const TilesetDefinition& GetTileset() const noexcept {
        return tileset_;
    }
    [[nodiscard]] const std::vector<NodeTypeDefinition>& GetNodeTypes() const noexcept {
        return nodeTypes_;
    }
    [[nodiscard]] const NodeTypeDefinition* FindNodeType(
        const std::string_view typeId) const noexcept {
        for (const NodeTypeDefinition& type : nodeTypes_) {
            if (type.typeId == typeId) {
                return &type;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const std::vector<PuzzleDefinition>& GetPuzzles() const noexcept {
        return puzzles_;
    }
    [[nodiscard]] const PuzzleDefinition* Find(const std::string_view id) const noexcept {
        for (const PuzzleDefinition& puzzle : puzzles_) {
            if (puzzle.id == id) {
                return &puzzle;
            }
        }
        return nullptr;
    }
    [[nodiscard]] bool Empty() const noexcept { return puzzles_.empty(); }

private:
    TilesetDefinition tileset_;
    std::vector<NodeTypeDefinition> nodeTypes_;
    std::vector<PuzzleDefinition> puzzles_;
};

} // namespace object_connect
