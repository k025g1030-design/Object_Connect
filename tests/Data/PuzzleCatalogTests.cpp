#include "TestSupport.hpp"

#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace object_connect::tests {
namespace {

using Json = nlohmann::json;

struct BundleStorage final {
    std::string catalog;
    std::string tileset;
    std::string nodeTypes;
    std::string level;

    [[nodiscard]] PuzzleJsonSources Sources() const {
        return {
            {"data/catalog.json", catalog},
            {"data/tileset.json", tileset},
            {"data/node_types.json", nodeTypes},
            {{"data/levels/first.json", level}},
        };
    }
};

[[nodiscard]] Json EmptyLayer() {
    Json layer = Json::array();
    for (std::size_t row = 0; row < kPuzzleGridRows; ++row) {
        Json values = Json::array();
        for (std::size_t column = 0; column < kPuzzleGridColumns; ++column) {
            values.push_back(0);
        }
        layer.push_back(std::move(values));
    }
    return layer;
}

[[nodiscard]] BundleStorage MakeValidBundle() {
    const Json catalog = {
        {"schema_version", 1},
        {"canvas", {{"width", 1280}, {"height", 720}, {"tile_size", 16}}},
        {"tileset", "tileset.json"},
        {"node_types", "node_types.json"},
        {"levels", Json::array({"levels/first.json"})},
    };
    const Json tileset = {
        {"schema_version", 1},
        {"atlas_path", "atlas.png"},
        {"atlas_columns", 3},
        {"atlas_rows", 1},
        {"tiles",
         Json::array({
             {{"id", 1}, {"name", "root_tile"}, {"atlas_column", 0},
              {"atlas_row", 0}},
             {{"id", 2}, {"name", "relay_tile"}, {"atlas_column", 1},
              {"atlas_row", 0}},
             {{"id", 3}, {"name", "goal_tile"}, {"atlas_column", 2},
              {"atlas_row", 0}},
         })},
    };
    const Json nodeTypes = {
        {"schema_version", 1},
        {"node_types",
         Json::array({
             {{"type_id", "root_type"}, {"display_name", "ROOT"},
              {"stamp", Json::array({Json::array({1})})},
              {"anchor", {{"column", 0}, {"row", 0}}}},
             {{"type_id", "relay_type"}, {"display_name", "RELAY"},
              {"stamp", Json::array({Json::array({2})})},
              {"anchor", {{"column", 0}, {"row", 0}}}},
             {{"type_id", "goal_type"}, {"display_name", "GOAL"},
              {"stamp", Json::array({Json::array({3})})},
              {"anchor", {{"column", 0}, {"row", 0}}}},
         })},
    };

    Json background = EmptyLayer();
    background[0][0] = 1;
    const Json level = {
        {"schema_version", 1},
        {"id", "first_level"},
        {"title", "FIRST LEVEL"},
        {"background_color", "#180A12"},
        {"rules",
         {{"total_length", 900.0},
          {"minimum_slack_ratio", 1.05},
          {"show_target_connections", true},
          {"vessel",
           {{"color", "#861B2BFF"},
            {"base_width", 16.0},
            {"tip_width", 8.0},
            {"width_variation", 0.16}}}}},
        {"layers", {{"background", std::move(background)},
                    {"obstacles", EmptyLayer()}}},
        {"nodes",
         Json::array({
             {{"id", "root"}, {"type_id", "root_type"}, {"column", 5},
              {"row", 5}, {"is_root", true}, {"is_goal", false},
              {"max_incoming", 0}, {"max_outgoing", 2}},
             {{"id", "relay"}, {"type_id", "relay_type"}, {"column", 20},
              {"row", 5}, {"is_root", false}, {"is_goal", false},
              {"max_incoming", 2}, {"max_outgoing", 2}},
             {{"id", "goal"}, {"type_id", "goal_type"}, {"column", 35},
              {"row", 5}, {"is_root", false}, {"is_goal", true},
              {"max_incoming", 2}, {"max_outgoing", 0}},
         })},
        {"connections",
         Json::array({
             {{"id", "root_to_relay"}, {"from", "root"}, {"to", "relay"},
              {"point_count", 8}, {"thickness_scale", 1.0},
              {"follow_delay_seconds", 0.0}, {"initial_direction_degrees", 0.0}},
             {{"id", "relay_to_goal"}, {"from", "relay"}, {"to", "goal"},
              {"point_count", 12}, {"thickness_scale", 0.8},
              {"follow_delay_seconds", 0.1}, {"initial_direction_degrees", 25.0}},
         })},
    };
    return {catalog.dump(), tileset.dump(), nodeTypes.dump(), level.dump()};
}

template <typename Mutator>
[[nodiscard]] BundleStorage MutateCatalog(BundleStorage bundle, Mutator&& mutator) {
    Json json = Json::parse(bundle.catalog);
    std::forward<Mutator>(mutator)(json);
    bundle.catalog = json.dump();
    return bundle;
}

template <typename Mutator>
[[nodiscard]] BundleStorage MutateTileset(BundleStorage bundle, Mutator&& mutator) {
    Json json = Json::parse(bundle.tileset);
    std::forward<Mutator>(mutator)(json);
    bundle.tileset = json.dump();
    return bundle;
}

template <typename Mutator>
[[nodiscard]] BundleStorage MutateNodeTypes(BundleStorage bundle, Mutator&& mutator) {
    Json json = Json::parse(bundle.nodeTypes);
    std::forward<Mutator>(mutator)(json);
    bundle.nodeTypes = json.dump();
    return bundle;
}

template <typename Mutator>
[[nodiscard]] BundleStorage MutateLevel(BundleStorage bundle, Mutator&& mutator) {
    Json json = Json::parse(bundle.level);
    std::forward<Mutator>(mutator)(json);
    bundle.level = json.dump();
    return bundle;
}

void ExpectRejected(TestContext& context, const BundleStorage& bundle,
                    const std::string_view expectedFilename,
                    const std::string_view expectedPointer,
                    const std::string_view expectedDetail,
                    const std::string_view description) {
    PuzzleDefinition sentinelDefinition;
    sentinelDefinition.id = "sentinel";
    PuzzleCatalog catalog{{std::move(sentinelDefinition)}};
    std::string error;
    context.Expect(!PuzzleCatalogLoader::Parse(bundle.Sources(), catalog, error),
                   description);
    context.Expect(error.find(expectedFilename) != std::string::npos &&
                       error.find(expectedPointer) != std::string::npos &&
                       error.find(expectedDetail) != std::string::npos,
                   "JSON diagnostic includes filename, JSON pointer, and detail");
    context.Expect(catalog.Find("sentinel") != nullptr,
                   "failed JSON parsing preserves the previous catalog transactionally");
}

void TestValidNormalizedCatalog(TestContext& context) {
    const BundleStorage bundle = MakeValidBundle();
    PuzzleCatalog catalog;
    std::string error{"stale"};
    context.Expect(PuzzleCatalogLoader::Parse(bundle.Sources(), catalog, error),
                   "valid schema-version-1 JSON bundle parses");
    context.Expect(error.empty(), "successful JSON parsing clears an old diagnostic");
    context.Expect(catalog.GetTileset().tiles.size() == 3 &&
                       catalog.GetNodeTypes().size() == 3 &&
                       catalog.GetPuzzles().size() == 1,
                   "catalog owns normalized tileset, node types, and levels");

    const PuzzleDefinition* const puzzle = catalog.Find("first_level");
    context.Expect(puzzle != nullptr, "normalized catalog finds a level by stable ID");
    if (puzzle == nullptr) {
        return;
    }
    context.Expect(puzzle->backgroundTiles.columns == 80 &&
                       puzzle->backgroundTiles.rows == 45 &&
                       puzzle->backgroundTiles.At(0, 0) == 1 &&
                       puzzle->obstacleTiles.At(0, 0) == 0,
                   "45x80 row-major tile layers normalize into TileGrid");
    context.Expect(puzzle->rootNodeIndices == std::vector<std::size_t>{0} &&
                       puzzle->goalNodeIndices == std::vector<std::size_t>{2},
                   "root and goal roles resolve to stable node indices");
    context.Expect(puzzle->nodes[1].typeIndex == 1 &&
                       puzzle->nodes[1].displayName == "RELAY" &&
                       puzzle->nodes[1].stamp.IsOccupied(0, 0) &&
                       puzzle->nodes[1].bounds == TileBounds{20, 5, 1, 1} &&
                       NearlyEqual(puzzle->nodes[1].anchorPosition.x, 328.0f) &&
                       NearlyEqual(puzzle->nodes[1].anchorPosition.y, 88.0f),
                   "node type, occupied mask, bounds, and anchor pixels are resolved");
    context.Expect(puzzle->connections[0].fromNodeIndex == 0 &&
                       puzzle->connections[0].toNodeIndex == 1 &&
                       puzzle->connections[1].id == "relay_to_goal",
                   "connection endpoint IDs resolve while author order remains stable");
    context.Expect(puzzle->nodes[0].maxOutgoing == 2 &&
                       puzzle->nodes[2].maxIncoming == 2,
                   "capacity may safely exceed authored candidate degree");
}

void TestStrictSchemaAndDiagnostics(TestContext& context) {
    ExpectRejected(
        context,
        MutateCatalog(MakeValidBundle(), [](Json& json) { json["extra"] = 1; }),
        "data/catalog.json", "#/extra", "unexpected key",
        "catalog rejects unknown root keys");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["rules"]["vessel"]["extra"] = true;
        }),
        "data/levels/first.json", "#/rules/vessel/extra", "unexpected key",
        "nested objects reject unknown keys");
    ExpectRejected(
        context,
        MutateCatalog(MakeValidBundle(), [](Json& json) {
            json["schema_version"] = 1.0;
        }),
        "data/catalog.json", "#/schema_version", "integer 1",
        "schema version must be an integer, not a numerically equal float");
    ExpectRejected(
        context,
        MutateCatalog(MakeValidBundle(), [](Json& json) {
            json["canvas"]["tile_size"] = 8;
        }),
        "data/catalog.json", "#/canvas", "exactly 1280x720",
        "catalog canvas is fixed to the approved 16-pixel grid");
    ExpectRejected(
        context,
        MutateCatalog(MakeValidBundle(), [](Json& json) {
            json["tileset"] = "../tileset.json";
        }),
        "data/catalog.json", "#/tileset", "must not contain",
        "catalog references cannot lexically escape through a parent component");
    ExpectRejected(
        context,
        MutateTileset(MakeValidBundle(), [](Json& json) {
            json["atlas_path"] = "C:/outside/atlas.png";
        }),
        "data/tileset.json", "#/atlas_path", "safe",
        "atlas references cannot use absolute drive paths");

    BundleStorage syntax = MakeValidBundle();
    syntax.level = "{\"schema_version\":";
    ExpectRejected(context, syntax, "data/levels/first.json", "#/",
                   "invalid JSON syntax", "malformed JSON is rejected transactionally");

    BundleStorage duplicateKey = MakeValidBundle();
    duplicateKey.catalog.insert(1, "\"schema_version\":1,");
    ExpectRejected(context, duplicateKey, "data/catalog.json", "#/",
                   "duplicate object key",
                   "strict JSON objects reject duplicate member names");

    BundleStorage wrongBundleName = MakeValidBundle();
    PuzzleJsonSources sources = wrongBundleName.Sources();
    sources.tileset.filename = "data/wrong.json";
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(!PuzzleCatalogLoader::Parse(sources, catalog, error) &&
                       error.find("data/catalog.json#/tileset") != std::string::npos,
                   "catalog-relative document references are enforced");
}

void TestTilesetAndStampValidation(TestContext& context) {
    ExpectRejected(
        context,
        MutateTileset(MakeValidBundle(), [](Json& json) {
            json["atlas_path"] = "atlas.jpg";
        }),
        "data/tileset.json", "#/atlas_path", ".png",
        "tileset atlas paths are safe PNG references");
    ExpectRejected(
        context,
        MutateTileset(MakeValidBundle(), [](Json& json) {
            json["tiles"][0]["id"] = 0;
        }),
        "data/tileset.json", "#/tiles/0/id", "reserved",
        "tile ID zero remains reserved for empty cells");
    ExpectRejected(
        context,
        MutateTileset(MakeValidBundle(), [](Json& json) {
            json["tiles"][0]["id"] = 65536;
        }),
        "data/tileset.json", "#/tiles/0/id", "supported range",
        "runtime tile IDs are bounded to 16 bits");
    ExpectRejected(
        context,
        MutateTileset(MakeValidBundle(), [](Json& json) {
            json["tiles"][1]["atlas_column"] = 0;
        }),
        "data/tileset.json", "#/tiles/1/atlas_column", "same atlas cell",
        "atlas cells cannot be assigned to two tile definitions");
    ExpectRejected(
        context,
        MutateNodeTypes(MakeValidBundle(), [](Json& json) {
            json["node_types"][0]["stamp"] = Json::array(
                {Json::array({1, 0}), Json::array({1})});
        }),
        "data/node_types.json", "#/node_types/0/stamp/1", "rectangle",
        "node stamps must be rectangular");
    ExpectRejected(
        context,
        MutateNodeTypes(MakeValidBundle(), [](Json& json) {
            json["node_types"][0]["stamp"] =
                Json::array({Json::array({0})});
        }),
        "data/node_types.json", "#/node_types/0/stamp", "occupied",
        "node stamps cannot be entirely transparent");
    ExpectRejected(
        context,
        MutateNodeTypes(MakeValidBundle(), [](Json& json) {
            json["node_types"][0]["stamp"] =
                Json::array({Json::array({1, 0})});
            json["node_types"][0]["anchor"]["column"] = 1;
        }),
        "data/node_types.json", "#/node_types/0/anchor", "occupied",
        "node type anchors must select occupied stamp cells");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["layers"]["background"][0][0] = 99;
        }),
        "data/levels/first.json", "#/layers/background/0/0", "not declared",
        "all layer and stamp tile references resolve through the tileset");
}

void TestMatricesPlacementAndOverlap(TestContext& context) {
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["layers"]["background"].erase(44);
        }),
        "data/levels/first.json", "#/layers/background", "45 rows",
        "level layers require exactly 45 rows");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["layers"]["obstacles"][0].erase(79);
        }),
        "data/levels/first.json", "#/layers/obstacles/0", "80 cells",
        "level layer rows require exactly 80 cells");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["nodes"][1]["column"] = 5;
            json["nodes"][1]["row"] = 5;
        }),
        "data/levels/first.json", "#/nodes/1", "overlaps node",
        "occupied cells from separate node stamps cannot overlap");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["layers"]["obstacles"][5][20] = 2;
        }),
        "data/levels/first.json", "#/nodes/1", "solid obstacle",
        "occupied node cells cannot overlap the solid obstacle layer");
    ExpectRejected(
        context,
        MutateNodeTypes(
            MutateLevel(MakeValidBundle(), [](Json& json) {
                json["nodes"][2]["column"] = 79;
            }),
            [](Json& json) {
                json["node_types"][2]["stamp"] =
                    Json::array({Json::array({3, 3})});
            }),
        "data/levels/first.json", "#/nodes/2/column", "fit inside",
        "every occupied stamp cell must remain inside the canvas");
}

void TestRolesDagReferencesAndBlocking(TestContext& context) {
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            for (Json& node : json["nodes"]) {
                node["is_root"] = false;
            }
        }),
        "data/levels/first.json", "#/nodes", "root node",
        "a level requires at least one root role");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["nodes"][1]["max_incoming"] = 0;
        }),
        "data/levels/first.json", "#/connections/0/to", "zero incoming",
        "an authored edge cannot enter a zero-capacity target");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["connections"][0]["to"] = "missing";
        }),
        "data/levels/first.json", "#/connections/0/to", "does not exist",
        "connection endpoint references are strict");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["connections"][0]["to"] = "root";
        }),
        "data/levels/first.json", "#/connections/0/to", "self-connections",
        "connection endpoints cannot refer to the same node");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            Json duplicate = json["connections"][0];
            duplicate["to"] = "goal";
            json["connections"].push_back(std::move(duplicate));
        }),
        "data/levels/first.json", "#/connections/2/id", "duplicate connection ID",
        "connection IDs remain unique independently of their endpoints");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            Json duplicate = json["connections"][0];
            duplicate["id"] = "same_pair_again";
            json["connections"].push_back(std::move(duplicate));
        }),
        "data/levels/first.json", "#/connections/2/to", "duplicate directed",
        "directed endpoint pairs remain unique even when edge IDs differ");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["nodes"][0]["is_goal"] = true;
            json["nodes"][0]["max_outgoing"] = 0;
        }),
        "data/levels/first.json", "#/connections/0/from", "zero outgoing",
        "an authored edge cannot leave a zero-capacity source");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["nodes"][1]["max_incoming"] = 33;
        }),
        "data/levels/first.json", "#/nodes/1/max_incoming", "supported range",
        "node capacities are bounded to the approved 0-32 range");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            Json reverse = json["connections"][0];
            reverse["id"] = "relay_to_root";
            reverse["from"] = "relay";
            reverse["to"] = "root";
            json["nodes"][0]["max_incoming"] = 1;
            json["connections"].push_back(std::move(reverse));
        }),
        "data/levels/first.json", "#/connections", "acyclic",
        "candidate graph cycles are rejected");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["nodes"].push_back(
                {{"id", "isolated_goal"}, {"type_id", "goal_type"},
                 {"column", 50}, {"row", 20}, {"is_root", false},
                 {"is_goal", true}, {"max_incoming", 1},
                 {"max_outgoing", 0}});
        }),
        "data/levels/first.json", "#/nodes/3", "not reachable",
        "every gameplay node and goal must be reachable from a root");
    ExpectRejected(
        context,
        MutateLevel(MakeValidBundle(), [](Json& json) {
            json["layers"]["obstacles"][5][12] = 2;
        }),
        "data/levels/first.json", "#/connections/0", "blocked",
        "authored edges blocked by solid tiles plus vessel clearance are rejected");

    const BundleStorage multiRole = MutateLevel(
        MakeValidBundle(), [](Json& json) {
            json["nodes"].push_back(
                {{"id", "second_root"}, {"type_id", "root_type"},
                 {"column", 5}, {"row", 12}, {"is_root", true},
                 {"is_goal", false}, {"max_incoming", 0},
                 {"max_outgoing", 1}});
            json["nodes"].push_back(
                {{"id", "second_goal"}, {"type_id", "goal_type"},
                 {"column", 35}, {"row", 12}, {"is_root", false},
                 {"is_goal", true}, {"max_incoming", 1},
                 {"max_outgoing", 0}});
            json["connections"].push_back(
                {{"id", "second_root_to_relay"}, {"from", "second_root"},
                 {"to", "relay"}, {"point_count", 10},
                 {"thickness_scale", 0.5}, {"follow_delay_seconds", 0.0},
                 {"initial_direction_degrees", 0.0}});
            json["connections"].push_back(
                {{"id", "relay_to_second_goal"}, {"from", "relay"},
                 {"to", "second_goal"}, {"point_count", 10},
                 {"thickness_scale", 0.5}, {"follow_delay_seconds", 0.0},
                 {"initial_direction_degrees", 0.0}});
        });
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(PuzzleCatalogLoader::Parse(multiRole.Sources(), catalog, error),
                   "multiple roots/goals and branch/merge remain valid in a DAG");
    const PuzzleDefinition* const puzzle = catalog.Find("first_level");
    context.Expect(puzzle != nullptr && puzzle->rootNodeIndices.size() == 2 &&
                       puzzle->goalNodeIndices.size() == 2,
                   "all resolved root and goal indices are retained");

    const BundleStorage decoupledRoles = MutateLevel(
        MakeValidBundle(), [](Json& json) {
            json["nodes"][2]["max_outgoing"] = 1;
            json["nodes"].push_back(
                {{"id", "optional_dead_end"}, {"type_id", "relay_type"},
                 {"column", 50}, {"row", 5}, {"is_root", false},
                 {"is_goal", false}, {"max_incoming", 1},
                 {"max_outgoing", 0}});
            json["connections"].push_back(
                {{"id", "goal_to_optional"}, {"from", "goal"},
                 {"to", "optional_dead_end"}, {"point_count", 10},
                 {"thickness_scale", 0.5}, {"follow_delay_seconds", 0.0},
                 {"initial_direction_degrees", 0.0}});
        });
    PuzzleCatalog decoupledCatalog;
    error.clear();
    context.Expect(PuzzleCatalogLoader::Parse(
                       decoupledRoles.Sources(), decoupledCatalog, error),
                   "roles do not imply source/sink capacity or forbid reachable dead ends");
    const PuzzleDefinition* const decoupledPuzzle =
        decoupledCatalog.Find("first_level");
    context.Expect(
        decoupledPuzzle != nullptr && decoupledPuzzle->nodes.size() == 4 &&
            decoupledPuzzle->nodes[0].isRoot &&
            decoupledPuzzle->nodes[0].maxIncoming == 0 &&
            decoupledPuzzle->nodes[2].isGoal &&
            decoupledPuzzle->nodes[2].maxOutgoing == 1 &&
            !decoupledPuzzle->nodes[3].isGoal &&
            decoupledPuzzle->nodes[3].maxOutgoing == 0 &&
            decoupledPuzzle->connections.back().fromNodeIndex == 2 &&
            decoupledPuzzle->connections.back().toNodeIndex == 3,
        "root zero-incoming, goal outgoing, and optional non-goal dead-end data survive normalization");
}

void WriteTextFile(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}

void WriteBundleFiles(const std::filesystem::path& root,
                      const BundleStorage& bundle) {
    std::filesystem::create_directories(root / "data" / "levels");
    WriteTextFile(root / "data" / "catalog.json", bundle.catalog);
    WriteTextFile(root / "data" / "tileset.json", bundle.tileset);
    WriteTextFile(root / "data" / "node_types.json", bundle.nodeTypes);
    WriteTextFile(root / "data" / "levels" / "first.json", bundle.level);
    WriteTextFile(root / "data" / "atlas.png", "atlas placeholder");
}

void TestDiskLoadingAndAtlasReference(TestContext& context) {
    const BundleStorage bundle = MakeValidBundle();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "ObjectConnectJsonLoaderTests";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    WriteBundleFiles(root, bundle);

    PuzzleCatalog catalog;
    std::string error;
    context.Expect(PuzzleCatalogLoader::Load(
                       "data/catalog.json", root.string(), catalog, error),
                   "disk loader resolves every document relative to catalog/resource root");
    context.Expect(catalog.GetTileset().atlasPath == "data/atlas.png",
                   "atlas path is normalized relative to the resource root");

    std::filesystem::remove(root / "data" / "atlas.png", cleanupError);
    PuzzleDefinition sentinelDefinition;
    sentinelDefinition.id = "sentinel";
    PuzzleCatalog sentinel{{std::move(sentinelDefinition)}};
    context.Expect(!PuzzleCatalogLoader::Load(
                       "data/catalog.json", root.string(), sentinel, error) &&
                       error.find("data/tileset.json#/atlas_path") !=
                           std::string::npos &&
                       sentinel.Find("sentinel") != nullptr,
                    "missing atlas fails the whole disk transaction with file and pointer");

    constexpr std::array<std::string_view, 4> jsonResources{
        "data/catalog.json",
        "data/tileset.json",
        "data/node_types.json",
        "data/levels/first.json",
    };
    for (const std::string_view resourceName : jsonResources) {
        std::filesystem::remove_all(root, cleanupError);
        WriteBundleFiles(root, bundle);
        const std::filesystem::path resourcePath =
            root / std::filesystem::path{resourceName};
        std::filesystem::remove(resourcePath, cleanupError);
        std::filesystem::create_directory(resourcePath, cleanupError);

        PuzzleDefinition directorySentinelDefinition;
        directorySentinelDefinition.id = "sentinel";
        PuzzleCatalog directorySentinel{{std::move(directorySentinelDefinition)}};
        error.clear();
        context.Expect(
            !PuzzleCatalogLoader::Load("data/catalog.json", root.string(),
                                       directorySentinel, error) &&
                error.find(std::string{resourceName} + "#/") != std::string::npos &&
                error.find("not a regular file") != std::string::npos &&
                directorySentinel.Find("sentinel") != nullptr,
            "every JSON document is validated as a regular file before reading");
    }

    std::filesystem::remove_all(root, cleanupError);
    WriteBundleFiles(root, bundle);
    const std::filesystem::path outsideRoot =
        std::filesystem::temp_directory_path() /
        "ObjectConnectJsonLoaderOutsideRoot";
    std::filesystem::remove_all(outsideRoot, cleanupError);
    std::filesystem::create_directories(outsideRoot);
    const std::filesystem::path outsideLevel = outsideRoot / "escaped_level.json";
    WriteTextFile(outsideLevel, bundle.level);
    const std::filesystem::path linkedLevel =
        root / "data" / "levels" / "first.json";
    std::filesystem::remove(linkedLevel, cleanupError);
    std::error_code linkError;
    std::filesystem::create_symlink(outsideLevel, linkedLevel, linkError);
    if (!linkError) {
        PuzzleDefinition linkSentinelDefinition;
        linkSentinelDefinition.id = "sentinel";
        PuzzleCatalog linkSentinel{{std::move(linkSentinelDefinition)}};
        error.clear();
        context.Expect(
            !PuzzleCatalogLoader::Load("data/catalog.json", root.string(),
                                       linkSentinel, error) &&
                error.find("data/levels/first.json#/") != std::string::npos &&
                error.find("outside the resource root") != std::string::npos &&
                linkSentinel.Find("sentinel") != nullptr,
            "a JSON symlink cannot escape the canonical resource root");
    }

    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::remove_all(outsideRoot, cleanupError);
}

void TestBundledJsonCatalog(TestContext& context) {
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(
        PuzzleCatalogLoader::Load("data/catalog.json",
                                  std::string{OBJECT_CONNECT_TEST_RESOURCE_ROOT},
                                  catalog, error),
        "the checked-in JSON catalog and every referenced resource load transactionally");
    if (!error.empty() || catalog.GetPuzzles().size() != 3) {
        context.Expect(false, "bundled JSON catalog contains exactly three valid levels");
        return;
    }
    context.Expect(catalog.GetTileset().tiles.size() == 8 &&
                       catalog.GetNodeTypes().size() == 6,
                   "bundled catalog owns its atlas tile and organ stamp definitions");
    context.Expect(catalog.GetPuzzles()[0].id == "first_link" &&
                       catalog.GetPuzzles()[1].id == "around_block" &&
                       catalog.GetPuzzles()[2].id == "clot_path",
                   "catalog level order follows catalog.json rather than directory order");
    const PuzzleDefinition& network = catalog.GetPuzzles()[2];
    const std::size_t repeatedLungs = static_cast<std::size_t>(std::count_if(
        network.nodes.begin(), network.nodes.end(),
        [](const NodeDefinition& node) { return node.typeId == "lung"; }));
    std::vector<std::size_t> candidateIncoming(network.nodes.size(), 0);
    std::vector<std::size_t> candidateOutgoing(network.nodes.size(), 0);
    for (const ConnectionDefinition& connection : network.connections) {
        ++candidateOutgoing[connection.fromNodeIndex];
        ++candidateIncoming[connection.toNodeIndex];
    }
    context.Expect(network.rootNodeIndices.size() == 2 &&
                       network.goalNodeIndices.size() == 2 && repeatedLungs >= 2 &&
                       std::ranges::any_of(candidateOutgoing,
                                           [](const std::size_t count) {
                                               return count > 1;
                                           }) &&
                       std::ranges::any_of(candidateIncoming,
                                           [](const std::size_t count) {
                                               return count > 1;
                                           }),
                   "the network sample covers multiple roots, repeated organs, "
                   "branching, merging, and multiple goals");
    for (const PuzzleDefinition& puzzle : catalog.GetPuzzles()) {
        context.Expect(!puzzle.rootNodeIndices.empty() &&
                           !puzzle.goalNodeIndices.empty() &&
                           puzzle.backgroundTiles.cells.size() ==
                               kPuzzleGridColumns * kPuzzleGridRows &&
                           puzzle.obstacleTiles.cells.size() ==
                               kPuzzleGridColumns * kPuzzleGridRows,
                       "every bundled level exposes normalized roles and complete tile layers");
    }
}

} // namespace

void RunPuzzleCatalogTests(TestContext& context) {
    TestValidNormalizedCatalog(context);
    TestStrictSchemaAndDiagnostics(context);
    TestTilesetAndStampValidation(context);
    TestMatricesPlacementAndOverlap(context);
    TestRolesDagReferencesAndBlocking(context);
    TestDiskLoadingAndAtlasReference(context);
    TestBundledJsonCatalog(context);
}

} // namespace object_connect::tests
