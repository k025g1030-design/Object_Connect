#include "../TestSupport.hpp"

#include "ObjectConnect/Data/Csv.hpp"
#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace object_connect::tests {
namespace {

constexpr std::string_view kLevelsHeader =
    "level_id,level_name,map_path,next_level_id,total_length,minimum_slack_ratio,"
    "background_color,vessel_color,base_width,tip_width,width_variation\n";
constexpr std::string_view kMapHeader =
    "instance_id,source_preset_id,node_type,texture_path,width_tiles,height_tiles,"
    "display_name,tile_x,tile_y,max_incoming,max_outgoing,max_outgoing_length\n";
constexpr std::string_view kNodePresetsHeader =
    "preset_id,node_type,texture_path,width_tiles,height_tiles,display_name,"
    "max_incoming,max_outgoing,max_outgoing_length\n";

[[nodiscard]] std::string ValidLevels() {
    return std::string{kLevelsHeader} +
           "alpha,ALPHA,data/maps/alpha.csv,beta,100,,,,,,\n"
           "beta,BETA,data/maps/beta.csv,missing_next,0,0,#010203,#04050607,0,2,0.5\n";
}

[[nodiscard]] std::string ValidPresets() {
    return std::string{kNodePresetsHeader} +
           "organ_follow,follow,,1,1,ORGAN,1,1,40\n"
           "brain_end,end,white1x1.png,3,3,BRAIN,2,,\n"
           "bone_dead,dead,,8,4,,,,\n";
}

[[nodiscard]] std::string ValidAlphaMap() {
    return std::string{kMapHeader} +
           "heart,,root,,2,3,HEART,4,5,,2,50\n"
           "resting,organ_follow,,,,,,,,,,\n"
           "brain,brain_end,,,,,,20,8,,,\n"
           "bone,bone_dead,,,,,,10,10,,,\n";
}

[[nodiscard]] std::string ValidBetaMap() {
    return std::string{kMapHeader} +
           "only_follow,,follow,,1,1,,999999,999999,,,\n";
}

[[nodiscard]] std::array<PuzzleMapCsvSource, 2> ValidMaps(
    const std::string_view alpha,
    const std::string_view beta) {
    return {{{"data/maps/alpha.csv", alpha}, {"data/maps/beta.csv", beta}}};
}

[[nodiscard]] PuzzleCatalog ParseValidCatalog(TestContext& context) {
    const std::string levels = ValidLevels();
    const std::string nodes = ValidPresets();
    const std::string alpha = ValidAlphaMap();
    const std::string beta = ValidBetaMap();
    const auto maps = ValidMaps(alpha, beta);
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(
        PuzzleCatalogLoader::Parse(
            {levels, nodes, std::span<const PuzzleMapCsvSource>{maps}}, catalog, error),
        "valid level, node, and map CSV files parse as one puzzle catalog transaction");
    context.Expect(error.empty(), "successful puzzle parsing clears the error");
    return catalog;
}

void ExpectCatalogRejectedWithNodes(
    TestContext& context,
    const std::string_view levels,
    const std::string_view nodes,
    const std::span<const PuzzleMapCsvSource> maps,
    const std::string_view expectedError,
    const std::string_view description) {
    PuzzleDefinition sentinel;
    sentinel.id = "sentinel";
    PuzzleCatalog catalog{std::vector<PuzzleDefinition>{std::move(sentinel)}};
    std::string error;
    context.Expect(!PuzzleCatalogLoader::Parse({levels, nodes, maps}, catalog, error),
                   description);
    context.Expect(error.find(expectedError) != std::string::npos,
                   "rejected puzzle data explains the failing field");
    context.Expect(catalog.GetPuzzles().size() == 1 &&
                       catalog.GetPuzzles().front().id == "sentinel",
                   "failed puzzle parsing preserves the previous catalog");
}

void ExpectCatalogRejected(TestContext& context,
                           const std::string_view levels,
                           const std::span<const PuzzleMapCsvSource> maps,
                           const std::string_view expectedError,
                           const std::string_view description) {
    const std::string nodes = ValidPresets();
    ExpectCatalogRejectedWithNodes(context, levels, nodes, maps, expectedError,
                                   description);
}

void ExpectPresetRejected(TestContext& context,
                          const std::string_view csv,
                          const std::string_view expectedError,
                          const std::string_view description) {
    NodePresetDefinition sentinel;
    sentinel.id = "sentinel";
    NodePresetCatalog catalog{
        std::vector<NodePresetDefinition>{std::move(sentinel)}};
    std::string error;
    context.Expect(!NodePresetCatalogLoader::Parse(csv, catalog, error), description);
    context.Expect(error.find(expectedError) != std::string::npos,
                   "rejected preset data explains the failing field");
    context.Expect(catalog.GetPresets().size() == 1 &&
                       catalog.GetPresets().front().id == "sentinel",
                   "failed preset parsing preserves the previous catalog");
}

[[nodiscard]] std::string ReplaceOnce(std::string text,
                                      const std::string_view oldText,
                                      const std::string_view newText) {
    const std::size_t position = text.find(oldText);
    if (position != std::string::npos) {
        text.replace(position, oldText.size(), newText);
    }
    return text;
}

void TestCsvReader(TestContext& context) {
    const data::CsvParseResult parsed =
        data::Csv::Parse("id,name,value\r\na,\"Heart, Main\",1\r\n");
    context.Expect(parsed.document.has_value(),
                   "shared CSV reader accepts CRLF and quoted commas");
    if (parsed.document.has_value()) {
        context.Expect(parsed.document->records.size() == 1 &&
                           parsed.document->records[0].fields[1] == "Heart, Main",
                       "shared CSV reader preserves quoted display text");
    }

    const data::CsvParseResult wrongWidth = data::Csv::Parse("a,b\n1\n");
    context.Expect(!wrongWidth.document.has_value() &&
                       wrongWidth.error.find("expected 2") != std::string::npos,
                   "shared CSV reader rejects a structurally short row");
}

void TestValidCatalogAndDefaults(TestContext& context) {
    const PuzzleCatalog catalog = ParseValidCatalog(context);
    context.Expect(catalog.GetPuzzles().size() == 2,
                   "level row order is preserved");
    if (catalog.GetPuzzles().size() != 2) {
        return;
    }

    const PuzzleDefinition& alpha = catalog.GetPuzzles()[0];
    context.Expect(alpha.id == "alpha" && alpha.title == "ALPHA" &&
                       alpha.mapPath == "data/maps/alpha.csv" &&
                       alpha.nextLevelId == std::optional<std::string>{"beta"},
                   "level identity, map path, and next-level metadata are retained");
    context.Expect(NearlyEqual(alpha.totalLength, 100.0f) &&
                       NearlyEqual(alpha.minimumSlackRatio, 1.05f) &&
                       NearlyEqual(alpha.backgroundColor.r, 18.0f / 255.0f) &&
                       NearlyEqual(alpha.vesselColor.r, 134.0f / 255.0f) &&
                       NearlyEqual(alpha.baseWidth, 16.0f) &&
                       NearlyEqual(alpha.tipWidth, 16.0f) &&
                       NearlyEqual(alpha.widthVariation, 0.16f),
                   "the six optional level-style fields use their documented defaults");
    context.Expect(alpha.nodes.size() == 4,
                   "a map CSV supplies the complete runtime node list");
    if (alpha.nodes.size() != 4) {
        return;
    }

    const NodeDefinition& root = alpha.nodes[0];
    context.Expect(root.type == NodeType::Root && root.sourcePresetId.empty() &&
                       root.texturePath.empty() && root.maxIncoming == 0 &&
                       root.maxOutgoing == 2 && NearlyEqual(root.maxOutgoingLength, 50.0f),
                   "blank preset and texture are valid and blank capabilities become zero");
    context.Expect(root.tilePosition == std::optional<TilePosition>{{4, 5}} &&
                       root.HasPlacement(),
                   "paired tile coordinates create a placement");
    context.Expect(root.GetPixelSize() == Vec2{32.0f, 48.0f} &&
                       root.GetTopLeftPosition() == std::optional<Vec2>{{64.0f, 80.0f}} &&
                       root.GetCenterPosition() == std::optional<Vec2>{{80.0f, 104.0f}},
                   "16-pixel tiles define node size, top-left, and center coordinates");

    const NodeDefinition& hidden = alpha.nodes[1];
    context.Expect(hidden.type == NodeType::Follow &&
                       hidden.sourcePresetId == "organ_follow" &&
                       hidden.displayName == "ORGAN" &&
                       hidden.widthTiles == 1 && hidden.heightTiles == 1 &&
                       hidden.maxIncoming == 1 && hidden.maxOutgoing == 1 &&
                       NearlyEqual(hidden.maxOutgoingLength, 40.0f) &&
                       !hidden.HasPlacement() &&
                       !hidden.GetTopLeftPosition().has_value() &&
                       !hidden.GetCenterPosition().has_value(),
                   "blank map fields inherit their node preset while placement remains map-only");

    const NodeDefinition& brain = alpha.nodes[2];
    context.Expect(brain.type == NodeType::End &&
                       brain.texturePath == "white1x1.png" &&
                       brain.displayName == "BRAIN" && brain.maxIncoming == 2,
                   "node preset texture, name, type, size, and capacity reach runtime data");

    const PuzzleDefinition& beta = catalog.GetPuzzles()[1];
    context.Expect(beta.nextLevelId ==
                       std::optional<std::string>{"missing_next"} &&
                       NearlyEqual(beta.totalLength, 0.0f) &&
                       NearlyEqual(beta.minimumSlackRatio, 0.0f) &&
                       NearlyEqual(beta.baseWidth, 0.0f) &&
                       beta.nodes.size() == 1 && beta.nodes[0].type == NodeType::Follow,
                   "unknown next levels, zero values, missing roots, and missing ends are accepted");
    context.Expect(beta.nodes[0].tilePosition ==
                       std::optional<TilePosition>{{999999, 999999}},
                   "the data loader does not impose canvas bounds");
}

void TestStructuralAndScalarErrors(TestContext& context) {
    const std::string levels = ValidLevels();
    const std::string alpha = ValidAlphaMap();
    const std::string beta = ValidBetaMap();
    auto maps = ValidMaps(alpha, beta);

    const std::string wrongHeader = ReplaceOnce(
        levels, "level_id,level_name", "puzzle_id,level_name");
    ExpectCatalogRejected(context, wrongHeader, maps, "header must be exactly",
                          "levels.csv uses one exact header");

    const std::string duplicateLevel = levels +
        "alpha,DUPLICATE,data/maps/alpha.csv,,1,,,,,,\n";
    ExpectCatalogRejected(context, duplicateLevel, maps, "duplicate level ID",
                          "duplicate level IDs are rejected");

    const std::string badLevelId = ReplaceOnce(levels, "alpha,ALPHA", "Alpha,ALPHA");
    ExpectCatalogRejected(context, badLevelId, maps, "lower_snake_case",
                          "level IDs use lower_snake_case");

    const std::string unsafeMapPath =
        ReplaceOnce(levels, "data/maps/alpha.csv", "../alpha.csv");
    ExpectCatalogRejected(context, unsafeMapPath, maps, "remain within",
                          "map paths cannot escape the resource root");

    const std::string badPresetType =
        ReplaceOnce(ValidPresets(), "organ_follow,follow", "organ_follow,other");
    ExpectCatalogRejectedWithNodes(
        context, levels, badPresetType, maps, "expected exactly 'root'",
        "nodes.csv participates in the same transactional runtime catalog load");

    const std::string negativeTotal = ReplaceOnce(levels, ",100,,,,,,", ",-1,,,,,,");
    ExpectCatalogRejected(context, negativeTotal, maps, "finite non-negative",
                          "negative total length is rejected without testing solvability");

    const std::string badType = ReplaceOnce(alpha, ",root,,", ",organ,,");
    maps = ValidMaps(badType, beta);
    ExpectCatalogRejected(context, levels, maps, "expected exactly 'root'",
                          "node_type accepts only the four runtime roles");

    const std::string unknownPreset =
        ReplaceOnce(alpha, "resting,organ_follow", "resting,missing_preset");
    maps = ValidMaps(unknownPreset, beta);
    ExpectCatalogRejected(context, levels, maps, "unknown node preset ID",
                          "map preset references must resolve through nodes.csv");

    const std::string unsafeTexture = ReplaceOnce(
        alpha, "brain,brain_end,,,,,,20,8,,,",
        "brain,brain_end,,../outside.png,,,BRAIN,20,8,,,");
    maps = ValidMaps(unsafeTexture, beta);
    ExpectCatalogRejected(context, levels, maps, "remain within",
                          "non-empty texture paths cannot escape the resource root");

    const std::string halfPlacement =
        ReplaceOnce(alpha, "HEART,4,5", "HEART,4,");
    maps = ValidMaps(halfPlacement, beta);
    ExpectCatalogRejected(context, levels, maps, "must either both be set",
                          "half-specified placement is rejected");

    const std::string badCapability =
        ReplaceOnce(alpha, ",,2,50", ",,-2,50");
    maps = ValidMaps(badCapability, beta);
    ExpectCatalogRejected(context, levels, maps, "non-negative 32-bit integer",
                          "capability counts must parse as non-negative integers");

    const std::string duplicateNode = alpha +
        "heart,,follow,,1,1,,,,,,\n";
    maps = ValidMaps(duplicateNode, beta);
    ExpectCatalogRejected(context, levels, maps, "duplicate node instance ID",
                          "node instance IDs are unique within one map");

    const std::array<PuzzleMapCsvSource, 1> missingMap{{
        {"data/maps/alpha.csv", alpha},
    }};
    ExpectCatalogRejected(context, levels, missingMap, "no map CSV source",
                          "every level requires its referenced map document");
}

void TestNodePresetCatalog(TestContext& context) {
    const std::string presets = std::string{kNodePresetsHeader} +
        "heart,root,,3,4,HEART,,2,80\n"
        "bone,dead,white1x1.png,0,0,,,,\n";

    NodePresetCatalog catalog;
    std::string error;
    context.Expect(NodePresetCatalogLoader::Parse(presets, catalog, error),
                   "authoring presets parse independently from puzzle maps");
    context.Expect(error.empty() && catalog.GetPresets().size() == 2,
                   "valid node preset parsing commits the complete catalog");
    const NodePresetDefinition* heart = catalog.Find("heart");
    context.Expect(heart != nullptr && heart->type == NodeType::Root &&
                       heart->texturePath.empty() && heart->widthTiles == 3 &&
                       heart->heightTiles == 4 && heart->maxIncoming == 0 &&
                       heart->maxOutgoing == 2 &&
                       NearlyEqual(heart->maxOutgoingLength, 80.0f),
                   "preset fields and blank numeric defaults are retained");

    const std::string duplicate = presets +
        "heart,follow,,1,1,,,,\n";
    ExpectPresetRejected(context, duplicate, "duplicate preset ID",
                         "preset IDs must be unique");

    const std::string invalidType =
        ReplaceOnce(presets, "heart,root", "heart,other");
    ExpectPresetRejected(context, invalidType, "expected exactly 'root'",
                         "preset node type uses the same four roles as map instances");
}

void TestIndependentDiskLoad(TestContext& context) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "ObjectConnect_DataLayerTests";
    std::error_code filesystemError;
    static_cast<void>(std::filesystem::remove_all(root, filesystemError));
    filesystemError.clear();
    static_cast<void>(std::filesystem::create_directories(root / "data/maps",
                                                          filesystemError));
    context.Expect(!filesystemError, "temporary data resource directory is created");
    if (filesystemError) {
        return;
    }

    {
        std::ofstream file(root / "data/levels.csv", std::ios::binary);
        file << kLevelsHeader
             << "empty,EMPTY,data/maps/empty.csv,unknown,0,,,,,,\n";
    }
    {
        std::ofstream file(root / "data/nodes.csv", std::ios::binary);
        file << kNodePresetsHeader;
    }
    {
        std::ofstream file(root / "data/maps/empty.csv", std::ios::binary);
        file << kMapHeader;
    }

    PuzzleCatalog catalog;
    std::string error;
    context.Expect(PuzzleCatalogLoader::Load(
                       PuzzleDataPaths{}, root.generic_string(), catalog, error),
                   "puzzle disk load consumes levels, nodes, and referenced map CSV files");
    context.Expect(error.empty() && catalog.GetPuzzles().size() == 1 &&
                       catalog.GetPuzzles()[0].nodes.empty(),
                   "a structurally valid map may contain no root, end, or other nodes");

    static_cast<void>(std::filesystem::remove(root / "data/nodes.csv",
                                              filesystemError));
    context.Expect(!PuzzleCatalogLoader::Load(
                       PuzzleDataPaths{}, root.generic_string(), catalog, error),
                   "the runtime catalog rejects a missing nodes.csv file");
    context.Expect(catalog.GetPuzzles().size() == 1,
                   "a failed three-file transaction preserves the prior runtime catalog");

    filesystemError.clear();
    static_cast<void>(std::filesystem::remove_all(root, filesystemError));
    context.Expect(!filesystemError, "temporary data resource directory is removed");
}

void TestBundledCatalogs(TestContext& context) {
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(PuzzleCatalogLoader::Load(
                       PuzzleDataPaths{},
                       std::string{OBJECT_CONNECT_TEST_RESOURCE_ROOT},
                       catalog, error),
                   "the bundled levels and per-level map CSV files load");
    if (!error.empty() || catalog.GetPuzzles().size() != 3) {
        context.Fail("bundled puzzle catalog contains exactly three levels");
        return;
    }

    context.Expect(catalog.GetPuzzles()[0].id == "first_link" &&
                       catalog.GetPuzzles()[0].nodes.size() == 2 &&
                       catalog.GetPuzzles()[0].nodes.front().type == NodeType::Root &&
                       catalog.GetPuzzles()[0].nodes.front().texturePath ==
                           "white1x1.png" &&
                       catalog.GetPuzzles()[0].nodes.front().maxOutgoing == 1 &&
                       NearlyEqual(
                           catalog.GetPuzzles()[0].nodes.front().maxOutgoingLength,
                           700.0f) &&
                       catalog.GetPuzzles()[0].nodes.back().type == NodeType::End,
                   "first_link inherits preset visuals while map capacities override defaults");
    context.Expect(catalog.GetPuzzles()[1].nodes.size() == 5 &&
                       catalog.GetPuzzles()[1].nodes.back().type == NodeType::Dead,
                   "around_block converts its old obstacle into a dead node rectangle");
    context.Expect(catalog.GetPuzzles()[2].nodes.size() == 9 &&
                       catalog.GetPuzzles()[2].nodes[6].type == NodeType::End &&
                       catalog.GetPuzzles()[2].nodes[7].type == NodeType::Dead &&
                       catalog.GetPuzzles()[2].nodes[8].type == NodeType::Dead,
                   "clot_path migrates intermediate nodes and both old obstacles");

    NodePresetCatalog presets;
    context.Expect(NodePresetCatalogLoader::Load(
                       NodePresetDataPaths{},
                       std::string{OBJECT_CONNECT_TEST_RESOURCE_ROOT},
                       presets, error),
                   "the bundled runtime node presets also load independently for tools");
    context.Expect(error.empty() && presets.GetPresets().size() == 4,
                   "bundled presets cover root, follow, end, and dead authoring roles");
}

} // namespace

void RunPuzzleCatalogTests(TestContext& context) {
    TestCsvReader(context);
    TestValidCatalogAndDefaults(context);
    TestStructuralAndScalarErrors(context);
    TestNodePresetCatalog(context);
    TestIndependentDiskLoad(context);
    TestBundledCatalogs(context);
}

} // namespace object_connect::tests
