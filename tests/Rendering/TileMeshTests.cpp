#include "TestSupport.hpp"

#include "ObjectConnect/Rendering/TileMesh.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace object_connect::tests {
namespace {

[[nodiscard]] TilesetDefinition MakeTileset() {
    TilesetDefinition tileset;
    tileset.atlasPath = "tiles/atlas.png";
    tileset.atlasColumns = 4;
    tileset.atlasRows = 2;
    tileset.tiles = {
        TileDefinition{1, "heart", 0, 0},
        TileDefinition{2, "bone", 3, 1},
    };
    return tileset;
}

[[nodiscard]] bool IsFiniteVertex(const TileMeshVertex& vertex) noexcept {
    return IsFinite(vertex.position) && IsFinite(vertex.uv) &&
           std::isfinite(vertex.tint.r) && std::isfinite(vertex.tint.g) &&
           std::isfinite(vertex.tint.b) && std::isfinite(vertex.tint.a);
}

void TestAtlasQuadsAndTints(TestContext& context) {
    TileGrid grid;
    grid.columns = 3;
    grid.rows = 2;
    grid.cells = {1, 0, 2, 0, 1, 0};
    const std::vector<Color> tints = {
        {1.0f, 0.0f, 0.0f, 1.0f},
        {},
        {0.0f, 0.0f, 1.0f, 0.5f},
        {},
        {0.0f, 1.0f, 0.0f, 1.0f},
        {},
    };

    std::vector<TileMeshVertex> vertices;
    std::string error;
    context.Expect(BuildTileMesh(grid, MakeTileset(), {10.0f, 20.0f}, 16.0f,
                                 vertices, error, tints),
                   "valid tile grid builds a CPU mesh");
    context.Expect(error.empty(), "successful tile mesh build clears its error");
    context.Expect(vertices.size() == 18,
                   "three nonzero tiles generate six vertices each");
    if (vertices.size() != 18) {
        return;
    }

    context.Expect(vertices[0].position == Vec2{10.0f, 20.0f} &&
                       vertices[1].position == Vec2{26.0f, 20.0f} &&
                       vertices[2].position == Vec2{10.0f, 36.0f},
                   "first tile uses a clockwise triangle-list quad");
    context.Expect(vertices[0].uv == Vec2{0.0f, 0.0f} &&
                       vertices[1].uv == Vec2{0.25f, 0.0f} &&
                       vertices[2].uv == Vec2{0.0f, 0.5f},
                   "tile one maps to its exact atlas cell");
    context.Expect(vertices[0].tint == tints[0] && vertices[5].tint == tints[0],
                   "every vertex in a tile quad receives its cell tint");

    context.Expect(vertices[6].position == Vec2{42.0f, 20.0f},
                   "ID zero is skipped without collapsing later cell positions");
    context.Expect(vertices[6].uv == Vec2{0.75f, 0.5f} &&
                       vertices[11].uv == Vec2{1.0f, 1.0f},
                   "last atlas cell maps to normalized UV range [0.75,1] x [0.5,1]");
    context.Expect(vertices[6].tint == tints[2] &&
                       vertices[12].tint == tints[4],
                   "separate occupied cells keep separate tints");

    bool allFinite = true;
    for (const TileMeshVertex& vertex : vertices) {
        allFinite = allFinite && IsFiniteVertex(vertex);
    }
    context.Expect(allFinite, "tile mesh produces only finite positions, UVs, and tints");
}

void TestDefaultTintAndEmptyGrid(TestContext& context) {
    TileGrid grid;
    grid.columns = 1;
    grid.rows = 1;
    grid.cells = {1};

    std::vector<TileMeshVertex> vertices;
    std::string error;
    context.Expect(BuildTileMesh(grid, MakeTileset(), {}, 16.0f, vertices, error),
                   "tile mesh supports an omitted per-cell tint array");
    context.Expect(vertices.size() == 6 && vertices.front().tint == Color{},
                   "omitted tints default every tile to opaque white");

    vertices.push_back({});
    context.Expect(BuildTileMesh(TileGrid{}, TilesetDefinition{}, {}, 16.0f,
                                 vertices, error),
                   "canonical empty grid builds without requiring an atlas");
    context.Expect(vertices.empty(), "empty grid clears prior output and emits no vertices");

    TileStamp stamp;
    stamp.columns = 2;
    stamp.rows = 1;
    stamp.cells = {0, 2};
    stamp.occupiedMask = {0, 1};
    const Color inactiveTint{0.35f, 0.35f, 0.4f, 1.0f};
    context.Expect(BuildTileStampMesh(stamp, MakeTileset(), {32.0f, 48.0f},
                                      16.0f, inactiveTint, vertices, error),
                   "resolved node stamp builds through the shared tile mesh path");
    context.Expect(vertices.size() == 6 &&
                       vertices.front().position == Vec2{48.0f, 48.0f} &&
                       vertices.front().tint == inactiveTint,
                   "stamp zero cells stay empty and state tint reaches its occupied tile");
}

void TestInvalidMeshInput(TestContext& context) {
    TileGrid grid;
    grid.columns = 1;
    grid.rows = 1;
    grid.cells = {9};

    std::vector<TileMeshVertex> vertices(1);
    std::string error;
    context.Expect(!BuildTileMesh(grid, MakeTileset(), {}, 16.0f, vertices, error),
                   "unknown nonzero tile ID is rejected");
    context.Expect(vertices.empty() && !error.empty(),
                   "failed mesh build never exposes partial or stale vertices");

    grid.cells = {1};
    TilesetDefinition badAtlas = MakeTileset();
    badAtlas.tiles.front().atlasColumn = badAtlas.atlasColumns;
    context.Expect(!BuildTileMesh(grid, badAtlas, {}, 16.0f, vertices, error),
                   "tile atlas coordinate outside its grid is rejected");

    const std::vector<Color> wrongTintCount(2);
    context.Expect(!BuildTileMesh(grid, MakeTileset(), {}, 16.0f, vertices,
                                  error, wrongTintCount),
                   "per-cell tint count must match the tile grid");

    const float nan = (std::numeric_limits<float>::quiet_NaN)();
    const std::vector<Color> invalidTint{{nan, 1.0f, 1.0f, 1.0f}};
    context.Expect(!BuildTileMesh(grid, MakeTileset(), {}, 16.0f, vertices,
                                  error, invalidTint),
                   "non-finite tint is rejected");

    TileGrid malformed = grid;
    malformed.columns = 2;
    context.Expect(!BuildTileMesh(malformed, MakeTileset(), {}, 16.0f,
                                  vertices, error),
                   "grid dimension and cell-count mismatch is rejected");
    context.Expect(!BuildTileMesh(grid, MakeTileset(), {}, 0.0f, vertices, error),
                   "non-positive mesh tile size is rejected");
}

void TestCapacityGrowth(TestContext& context) {
    context.Expect(GrowTileMeshCapacity(0, 1) == 256,
                   "first tile buffer allocation uses a small reusable capacity");
    context.Expect(GrowTileMeshCapacity(256, 256) == 256,
                   "sufficient tile buffer capacity is preserved");
    context.Expect(GrowTileMeshCapacity(256, 257) == 512,
                   "tile buffer capacity doubles when it is exhausted");
    context.Expect(GrowTileMeshCapacity(300, 301) == 600,
                   "non-power-of-two existing capacity still grows amortized");

    const std::size_t maximum = (std::numeric_limits<std::size_t>::max)();
    context.Expect(GrowTileMeshCapacity(maximum / 2 + 1, maximum) == maximum,
                   "tile buffer growth saturates safely instead of overflowing");
}

void TestRenderContract(TestContext& context) {
    constexpr std::array expected = {
        PuzzleRenderLayer::BackgroundTiles,
        PuzzleRenderLayer::TargetHints,
        PuzzleRenderLayer::PixelVessels,
        PuzzleRenderLayer::SolidTiles,
        PuzzleRenderLayer::SourcePulse,
        PuzzleRenderLayer::NodeStamps,
    };
    context.Expect(kPuzzleRenderLayerOrder == expected,
                   "headless render contract preserves the required layer order");
    context.Expect(kPuzzleTileSamplingMode == TileSamplingMode::NearestClamp,
                   "headless render contract requires nearest clamp tile sampling");
}

} // namespace

void RunTileMeshTests(TestContext& context) {
    TestAtlasQuadsAndTints(context);
    TestDefaultTintAndEmptyGrid(context);
    TestInvalidMeshInput(context);
    TestCapacityGrowth(context);
    TestRenderContract(context);
}

} // namespace object_connect::tests
