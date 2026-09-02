#pragma once

#include "ObjectConnect/Math/Color.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace object_connect {

inline constexpr float kPuzzleTileSize = 16.0f;

enum class NodeType : std::uint8_t {
    Root,
    Follow,
    End,
    Dead,
};

struct TilePosition final {
    std::uint32_t x = 0;
    std::uint32_t y = 0;

    [[nodiscard]] bool operator==(const TilePosition&) const noexcept = default;
};

struct NodeDefinition final {
    std::string id;
    std::string sourcePresetId;
    NodeType type = NodeType::Follow;
    std::string texturePath;
    std::uint32_t widthTiles = 0;
    std::uint32_t heightTiles = 0;
    std::string displayName;
    std::optional<TilePosition> tilePosition;
    std::uint32_t maxIncoming = 0;
    std::uint32_t maxOutgoing = 0;
    float maxOutgoingLength = 0.0f;

    [[nodiscard]] bool HasPlacement() const noexcept {
        return tilePosition.has_value();
    }
    [[nodiscard]] Vec2 GetPixelSize() const noexcept {
        return {
            static_cast<float>(widthTiles) * kPuzzleTileSize,
            static_cast<float>(heightTiles) * kPuzzleTileSize,
        };
    }
    [[nodiscard]] std::optional<Vec2> GetTopLeftPosition() const noexcept {
        if (!tilePosition.has_value()) {
            return std::nullopt;
        }
        return Vec2{
            static_cast<float>(tilePosition->x) * kPuzzleTileSize,
            static_cast<float>(tilePosition->y) * kPuzzleTileSize,
        };
    }
    [[nodiscard]] std::optional<Vec2> GetCenterPosition() const noexcept {
        const std::optional<Vec2> topLeft = GetTopLeftPosition();
        if (!topLeft.has_value()) {
            return std::nullopt;
        }
        const Vec2 size = GetPixelSize();
        return Vec2{topLeft->x + size.x * 0.5f, topLeft->y + size.y * 0.5f};
    }
};

struct NodePresetDefinition final {
    std::string id;
    NodeType type = NodeType::Follow;
    std::string texturePath;
    std::uint32_t widthTiles = 0;
    std::uint32_t heightTiles = 0;
    std::string displayName;
    std::uint32_t maxIncoming = 0;
    std::uint32_t maxOutgoing = 0;
    float maxOutgoingLength = 0.0f;
};

struct PuzzleDefinition final {
    std::string id;
    std::string title;
    std::string mapPath;
    std::optional<std::string> nextLevelId;
    float totalLength = 0.0f;
    float minimumSlackRatio = 1.05f;
    Color backgroundColor{18.0f / 255.0f, 11.0f / 255.0f,
                          16.0f / 255.0f, 1.0f};
    Color vesselColor{134.0f / 255.0f, 27.0f / 255.0f,
                      43.0f / 255.0f, 1.0f};
    float baseWidth = 16.0f;
    float tipWidth = 16.0f;
    float widthVariation = 0.16f;
    std::vector<NodeDefinition> nodes;
};

class PuzzleCatalog final {
public:
    PuzzleCatalog() = default;
    explicit PuzzleCatalog(std::vector<PuzzleDefinition> puzzles)
        : puzzles_(std::move(puzzles)) {}

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
    std::vector<PuzzleDefinition> puzzles_;
};

class NodePresetCatalog final {
public:
    NodePresetCatalog() = default;
    explicit NodePresetCatalog(std::vector<NodePresetDefinition> presets)
        : presets_(std::move(presets)) {}

    [[nodiscard]] const std::vector<NodePresetDefinition>& GetPresets() const noexcept {
        return presets_;
    }
    [[nodiscard]] const NodePresetDefinition* Find(
        const std::string_view id) const noexcept {
        for (const NodePresetDefinition& preset : presets_) {
            if (preset.id == id) {
                return &preset;
            }
        }
        return nullptr;
    }

private:
    std::vector<NodePresetDefinition> presets_;
};

struct PuzzleDataPaths final {
    std::string levels{"data/levels.csv"};
    std::string nodes{"data/nodes.csv"};
};

struct NodePresetDataPaths final {
    std::string nodes{"data/nodes.csv"};
};

} // namespace object_connect
