#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Math/Color.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace object_connect {

enum class PuzzleRenderLayer {
    BackgroundTiles,
    TargetHints,
    PixelVessels,
    SolidTiles,
    SourcePulse,
    NodeStamps,
};

inline constexpr std::array<PuzzleRenderLayer, 6> kPuzzleRenderLayerOrder = {
    PuzzleRenderLayer::BackgroundTiles,
    PuzzleRenderLayer::TargetHints,
    PuzzleRenderLayer::PixelVessels,
    PuzzleRenderLayer::SolidTiles,
    PuzzleRenderLayer::SourcePulse,
    PuzzleRenderLayer::NodeStamps,
};

enum class TileSamplingMode {
    NearestClamp,
};

inline constexpr TileSamplingMode kPuzzleTileSamplingMode =
    TileSamplingMode::NearestClamp;

struct TileMeshVertex final {
    Vec2 position{};
    Vec2 uv{};
    Color tint{};

    [[nodiscard]] bool operator==(const TileMeshVertex&) const noexcept = default;
};

// Builds six triangle-list vertices for every nonzero tile. Tile ID zero is
// always empty. When cellTints is empty every tile uses opaque white; otherwise
// it must contain exactly one tint per grid cell.
[[nodiscard]] bool BuildTileMesh(
    const TileGrid& grid, const TilesetDefinition& tileset, Vec2 origin,
    float tileSize, std::vector<TileMeshVertex>& output, std::string& error,
    std::span<const Color> cellTints = {}, Color defaultTint = {});

// Convenience overload for resolved node stamps. The stamp uses one uniform
// state tint while retaining its irregular zero/nonzero tile layout.
[[nodiscard]] bool BuildTileStampMesh(
    const TileStamp& stamp, const TilesetDefinition& tileset, Vec2 origin,
    float tileSize, Color tint, std::vector<TileMeshVertex>& output,
    std::string& error);

// Returns an amortized capacity suitable for a reusable CPU/GPU tile buffer.
// Existing sufficient capacity is preserved; growth starts at 256 vertices and
// doubles without overflowing.
[[nodiscard]] std::size_t GrowTileMeshCapacity(
    std::size_t currentCapacity, std::size_t requiredCapacity) noexcept;

} // namespace object_connect
