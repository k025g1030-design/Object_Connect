#include "TestSupport.hpp"

#include "ObjectConnect/Data/Csv.hpp"
#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace object_connect::tests {
namespace {

constexpr std::string_view kValidLevels =
    "puzzle_id,title,start_node_id,total_length,minimum_slack_ratio,background_color,show_target_connections,vessel_color,base_width,tip_width,width_variation\n"
    "first_puzzle,FIRST PUZZLE,start_node,500,1.05,#180A12,true,#B51F3E,#PLACEHOLDER,#PLACEHOLDER,#PLACEHOLDER\n";

[[nodiscard]] std::string ValidLevels() {
    std::string result{kValidLevels};
    const auto replace = [&result](const std::string_view value) {
        const std::size_t position = result.find("#PLACEHOLDER");
        if (position != std::string::npos) {
            result.replace(position, std::string_view{"#PLACEHOLDER"}.size(), value);
        }
    };
    replace("20");
    replace("7");
    replace("0.12");
    return result;
}

constexpr std::string_view kValidNodes =
    "puzzle_id,node_id,label,x,y,radius,color\n"
    "first_puzzle,start_node,A,100,100,20,#FF405C\n"
    "first_puzzle,middle_node,B,300,100,20,#FF8A60FF\n"
    "first_puzzle,end_node,C,500,200,20,#FFD166\n"
    "first_puzzle,decoy_node,X,1120,620,20,#777777\n";

// Deliberately reversed to prove that loader validation does not rewrite author order.
constexpr std::string_view kValidConnections =
    "puzzle_id,from_node_id,to_node_id,point_count,thickness_scale,follow_delay_seconds,initial_direction_degrees\n"
    "first_puzzle,middle_node,end_node,12,0.85,0.08,25\n"
    "first_puzzle,start_node,middle_node,8,1.0,0,-15\n";

constexpr std::string_view kValidObstacles =
    "puzzle_id,obstacle_id,shape,center_x,center_y,width,height,radius,color\n"
    "first_puzzle,wall_one,rectangle,760,300,100,80,,#51263A\n"
    "first_puzzle,cyst_one,circle,920,500,,,35,#7A2038CC\n";

[[nodiscard]] PuzzleCsvSources ValidSources(const std::string& levels) {
    return {levels, kValidNodes, kValidConnections, kValidObstacles};
}

[[nodiscard]] std::string ReplaceOnce(const std::string_view source,
                                      const std::string_view target,
                                      const std::string_view replacement) {
    std::string result{source};
    const std::size_t position = result.find(target);
    if (position != std::string::npos) {
        result.replace(position, target.size(), replacement);
    }
    return result;
}

void ExpectRejected(TestContext& context, const PuzzleCsvSources& sources,
                    const std::string_view expectedError,
                    const std::string_view description) {
    PuzzleDefinition sentinelDefinition;
    sentinelDefinition.id = "sentinel";
    PuzzleCatalog sentinel{{std::move(sentinelDefinition)}};
    std::string error;
    context.Expect(!PuzzleCatalogLoader::Parse(sources, sentinel, error), description);
    context.Expect(error.find(expectedError) != std::string::npos,
                   "rejected puzzle data reports the expected diagnostic category");
    context.Expect(sentinel.Find("sentinel") != nullptr,
                   "failed parsing preserves the caller's previous catalog");
}

void TestCsvReader(TestContext& context) {
    const data::CsvParseResult parsed = data::Csv::Parse(
        "\xEF\xBB\xBFname,value\r\n\"A, \"\"B\"\"\",1\r\n\"two\r\nlines\",2\r\n");
    context.Expect(parsed.Succeeded(),
                   "puzzle CSV accepts BOM, CRLF, quoted commas, escaped quotes and newlines");
    if (parsed.document.has_value()) {
        context.Expect(parsed.document->records.size() == 2,
                       "puzzle CSV ignores a final line ending");
        context.Expect(parsed.document->records[0].fields[0] == "A, \"B\"",
                       "puzzle CSV unescapes a doubled quote");
        context.Expect(parsed.document->records[1].fields[0] == "two\nlines",
                       "puzzle CSV normalizes a quoted CRLF");
    }

    context.Expect(!data::Csv::Parse("a,b\n1\n").Succeeded(),
                   "puzzle CSV rejects ragged records");
    context.Expect(!data::Csv::Parse("a\n\"unterminated\n").Succeeded(),
                   "puzzle CSV rejects unterminated quotes");
    context.Expect(!data::Csv::Parse("a\r1").Succeeded(),
                   "puzzle CSV rejects a bare carriage return");
}

void TestValidCatalogAndTopology(TestContext& context) {
    const std::string levels = ValidLevels();
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(PuzzleCatalogLoader::Parse(ValidSources(levels), catalog, error),
                   "valid four-table puzzle data parses");
    context.Expect(error.empty(), "successful puzzle parsing clears the diagnostic");
    context.Expect(catalog.GetPuzzles().size() == 1,
                   "catalog owns the configured puzzle");
    const PuzzleDefinition* const puzzle = catalog.Find("first_puzzle");
    context.Expect(puzzle != nullptr, "catalog finds a puzzle by ID");
    if (puzzle == nullptr) {
        return;
    }
    context.Expect(puzzle->nodes.size() == 4,
                   "catalog retains an unconnected decoy node");
    context.Expect(puzzle->startNodeIndex == 0,
                   "catalog resolves the start-node index");
    context.Expect(puzzle->connections.size() == 2,
                   "catalog owns every path connection");
    if (puzzle->connections.size() == 2) {
        context.Expect(puzzle->connections[0].fromNodeId == "middle_node" &&
                           puzzle->connections[0].toNodeId == "end_node",
                       "connection validation preserves CSV author order");
        context.Expect(puzzle->connections[1].fromNodeId == "start_node" &&
                           puzzle->connections[1].toNodeId == "middle_node",
                       "a valid graph need not begin with its start edge in storage order");
        context.Expect(puzzle->connections[1].fromNodeIndex == 0 &&
                           puzzle->connections[1].toNodeIndex == 1,
                       "connection node IDs resolve to stable indices");
    }
    context.Expect(puzzle->obstacles.size() == 2,
                   "rectangle and circle obstacles parse together");
    context.Expect(puzzle->backgroundColor.a == 1.0f,
                   "six-digit colors default alpha to one");
    context.Expect(puzzle->nodes[1].color.a == 1.0f,
                   "eight-digit FF color alpha parses to one");
}

void TestHeadersScalarsColorsAndAscii(TestContext& context) {
    const std::string levels = ValidLevels();
    const std::string wrongHeader = ReplaceOnce(levels, "total_length", "length");
    ExpectRejected(context, ValidSources(wrongHeader), "header must be exactly",
                   "renamed level header is rejected");
    PuzzleCatalog diagnosticCatalog;
    std::string diagnostic;
    context.Expect(!PuzzleCatalogLoader::Parse(ValidSources(wrongHeader),
                                               diagnosticCatalog, diagnostic) &&
                       diagnostic.find("levels.csv line 1, column") !=
                           std::string::npos,
                   "header diagnostics identify filename, row, and column");

    PuzzleCsvSources syntaxError = ValidSources(levels);
    syntaxError.levels = "puzzle_id\n\"unterminated\n";
    context.Expect(!PuzzleCatalogLoader::Parse(syntaxError, diagnosticCatalog,
                                               diagnostic) &&
                       diagnostic.find("levels.csv: line 2, column 1") !=
                           std::string::npos,
                   "CSV syntax diagnostics identify filename, row, and column");

    const std::string nonFinite = ReplaceOnce(levels, ",500,1.05,", ",nan,1.05,");
    ExpectRejected(context, ValidSources(nonFinite), "finite decimal",
                   "non-finite puzzle length is rejected");

    const std::string looseBoolean = ReplaceOnce(levels, ",true,#B51F3E", ",True,#B51F3E");
    ExpectRejected(context, ValidSources(looseBoolean), "exactly 'true' or 'false'",
                   "puzzle booleans are strict and lowercase");

    const std::string badColor = ReplaceOnce(levels, "#180A12", "180A12");
    ExpectRejected(context, ValidSources(badColor), "#RRGGBB",
                   "colors require a hash and six or eight hex digits");

    const std::string nonAscii = ReplaceOnce(levels, "FIRST PUZZLE", "PUZZLE \xE2\x98\x85");
    ExpectRejected(context, ValidSources(nonAscii), "printable ASCII",
                   "display text rejects non-ASCII glyphs unsupported by DebugText");

    const std::string uniformWidth =
        ReplaceOnce(levels, ",20,7,0.12", ",14,14,0");
    PuzzleCatalog uniformCatalog;
    context.Expect(PuzzleCatalogLoader::Parse(ValidSources(uniformWidth),
                                               uniformCatalog, diagnostic),
                   "equal base and tip widths support a uniform vessel ribbon");

    const std::string narrowBase =
        ReplaceOnce(levels, ",20,7,0.12", ",7,8,0.12");
    ExpectRejected(context, ValidSources(narrowBase), "greater than or equal",
                   "vessel width cannot grow from its root toward its tip");

    const std::string excessiveVariation =
        ReplaceOnce(levels, ",20,7,0.12", ",20,7,1.0");
    ExpectRejected(context, ValidSources(excessiveVariation), "range [0, 1)",
                   "width variation cannot collapse or invert the vessel");

    const std::string badPointCount =
        ReplaceOnce(kValidConnections, "end_node,12,", "end_node,13,");
    ExpectRejected(context,
                   {levels, kValidNodes, badPointCount, kValidObstacles},
                   "between 8 and 12", "tentacle point counts stay in the MVP range");

    const std::string unsafeLength =
        ReplaceOnce(levels, ",500,1.05,", ",100001,1.05,");
    ExpectRejected(context, ValidSources(unsafeLength), "must not exceed 100000",
                   "finite numeric values still respect runtime-safe ranges");

    const std::string unsafeDelay =
        ReplaceOnce(kValidConnections, ",0.08,25", ",1.01,25");
    ExpectRejected(context, {levels, kValidNodes, unsafeDelay, kValidObstacles},
                   "must not exceed 1 second",
                   "following history cannot grow beyond its configured safe range");
}

void TestReferencesAndDag(TestContext& context) {
    const std::string levels = ValidLevels();
    const std::string unknownNode =
        ReplaceOnce(kValidConnections, "middle_node,end_node", "middle_node,missing_node");
    ExpectRejected(context, {levels, kValidNodes, unknownNode, kValidObstacles},
                   "to-node ID does not exist", "unknown connection nodes are rejected");

    const std::string selfConnection =
        ReplaceOnce(kValidConnections, "middle_node,end_node", "middle_node,middle_node");
    ExpectRejected(context, {levels, kValidNodes, selfConnection, kValidObstacles},
                   "must not link a node to itself", "self connections are rejected");

    const std::string branchAndMerge = std::string{kValidConnections} +
        "first_puzzle,start_node,end_node,10,1,0,0\n";
    PuzzleCatalog dagCatalog;
    std::string dagError;
    context.Expect(
        PuzzleCatalogLoader::Parse(
            {levels, kValidNodes, branchAndMerge, kValidObstacles},
            dagCatalog, dagError),
        "a reachable DAG accepts multiple outgoing and multiple incoming connections");
    const PuzzleDefinition* const dag = dagCatalog.Find("first_puzzle");
    context.Expect(dag != nullptr && dag->connections.size() == 3,
                   "the accepted branch-and-merge graph retains every candidate edge");
    if (dag != nullptr && dag->connections.size() == 3) {
        context.Expect(dag->connections[2].fromNodeId == "start_node" &&
                           dag->connections[2].toNodeId == "end_node",
                       "DAG validation leaves sibling edge priority in author order");
    }

    const std::string duplicateEdge = std::string{kValidConnections} +
        "first_puzzle,start_node,middle_node,10,1,0,0\n";
    ExpectRejected(context, {levels, kValidNodes, duplicateEdge, kValidObstacles},
                   "duplicate directed", "duplicate directed edges are rejected explicitly");

    const std::string cycle =
        std::string{kValidConnections} +
        "first_puzzle,end_node,middle_node,10,1,0,0\n";
    ExpectRejected(context, {levels, kValidNodes, cycle, kValidObstacles}, "cycle",
                   "a reachable directed cycle is rejected anywhere in the graph");

    const std::string disconnected = std::string{kValidConnections} +
        "first_puzzle,decoy_node,end_node,10,1,0,0\n";
    ExpectRejected(context, {levels, kValidNodes, disconnected, kValidObstacles},
                   "reachable from start_node_id",
                   "every candidate edge must be reachable from the configured start");

    const std::string multipleTerminals = std::string{kValidConnections} +
        "first_puzzle,start_node,decoy_node,10,1,0,0\n";
    ExpectRejected(context, {levels, kValidNodes, multipleTerminals, kValidObstacles},
                   "exactly one reachable terminal node",
                   "the reachable graph has one abstract completion endpoint");

    const std::string noConnections =
        "puzzle_id,from_node_id,to_node_id,point_count,thickness_scale,follow_delay_seconds,initial_direction_degrees\n";
    ExpectRejected(context, {levels, kValidNodes, noConnections, kValidObstacles},
                   "at least one connection", "a puzzle cannot be trivially empty");
}

void TestCanvasObstaclesAndLength(TestContext& context) {
    const std::string levels = ValidLevels();
    const std::string outsideNode = ReplaceOnce(kValidNodes, "100,100,20", "10,100,20");
    ExpectRejected(context, {levels, outsideNode, kValidConnections, kValidObstacles},
                   "inside the 1280x720 canvas", "node circles must fit in the canvas");

    const std::string overlappingNodes =
        ReplaceOnce(kValidNodes, "300,100,20", "130,100,20");
    ExpectRejected(context,
                   {levels, overlappingNodes, kValidConnections, kValidObstacles},
                   "overlaps node", "node hit circles must not overlap");

    const std::string wrongUnusedShapeField =
        ReplaceOnce(kValidObstacles, "100,80,,#", "100,80,0,#");
    ExpectRejected(context,
                   {levels, kValidNodes, kValidConnections, wrongUnusedShapeField},
                   "radius must be empty", "unused rectangle radius must stay empty");

    const std::string outsideObstacle =
        ReplaceOnce(kValidObstacles, "920,500,,,35", "1270,500,,,35");
    ExpectRejected(context, {levels, kValidNodes, kValidConnections, outsideObstacle},
                   "inside the 1280x720 canvas",
                   "obstacle bounds must fit in the canvas");

    const std::string overlapsNode =
        ReplaceOnce(kValidObstacles, "920,500,,,35", "500,200,,,35");
    ExpectRejected(context, {levels, kValidNodes, kValidConnections, overlapsNode},
                   "overlaps node", "obstacles must not cover an interactive node");

    const std::string blocksConnection =
        ReplaceOnce(kValidObstacles, "760,300,100,80", "400,150,20,20");
    ExpectRejected(context,
                   {levels, kValidNodes, kValidConnections, blocksConnection},
                   "intersects an obstacle",
                   "required vessel routes include their rendered-width clearance");

    const std::string blocksOnlyAfterPixelSnap =
        ReplaceOnce(kValidObstacles, "760,300,100,80", "200,121,20,20");
    ExpectRejected(
        context,
        {levels, kValidNodes, kValidConnections, blocksOnlyAfterPixelSnap},
        "intersects an obstacle",
        "route clearance includes the extra cell used by pixel-grid snapping");

    const std::string tooShort = ReplaceOnce(levels, ",500,1.05,", ",440,1.05,");
    ExpectRejected(context, ValidSources(tooShort), "minimum_slack_ratio",
                   "total vessel budget covers the shortest completion path plus slack");

    const std::string branchAndMerge = std::string{kValidConnections} +
        "first_puzzle,start_node,end_node,10,1,0,0\n";
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(
        PuzzleCatalogLoader::Parse(
            {levels, kValidNodes, branchAndMerge, kValidObstacles}, catalog, error),
        "unused candidate edges do not consume the route length budget");
}

void TestBundledFourTableCatalog(TestContext& context) {
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(
        PuzzleCatalogLoader::Load(PuzzleDataPaths{},
                                  std::string{OBJECT_CONNECT_TEST_RESOURCE_ROOT},
                                  catalog, error),
        "the deployed four-table puzzle catalog loads as one transaction");
    if (!error.empty() || catalog.GetPuzzles().size() != 3) {
        context.Expect(false, "bundled catalog contains exactly three valid puzzles");
        return;
    }

    context.Expect(catalog.GetPuzzles()[0].id == "first_link" &&
                       catalog.GetPuzzles()[0].totalLength == 700.0f,
                   "first_link uses its one global 700-pixel length budget");
    context.Expect(catalog.GetPuzzles()[1].id == "around_block" &&
                       catalog.GetPuzzles()[1].connections.size() == 3 &&
                       catalog.GetPuzzles()[1].obstacles.size() == 1,
                   "around_block loads its ordered route and central obstacle");
    context.Expect(catalog.GetPuzzles()[2].id == "clot_path" &&
                       catalog.GetPuzzles()[2].connections.size() == 8 &&
                       catalog.GetPuzzles()[2].obstacles.size() == 2 &&
                       !catalog.GetPuzzles()[2].showTargetConnections,
                   "clot_path loads a route-choice graph without answer hints");

    bool allConnectionsUseOneThickness = true;
    bool allPuzzlesUsePixelCrimson = true;
    for (const PuzzleDefinition& puzzle : catalog.GetPuzzles()) {
        allPuzzlesUsePixelCrimson =
            allPuzzlesUsePixelCrimson && NearlyEqual(puzzle.baseWidth, 16.0f) &&
            NearlyEqual(puzzle.tipWidth, 16.0f) &&
            NearlyEqual(puzzle.widthVariation, 0.16f) &&
            NearlyEqual(puzzle.vesselColor.r, 134.0f / 255.0f) &&
            NearlyEqual(puzzle.vesselColor.g, 27.0f / 255.0f) &&
            NearlyEqual(puzzle.vesselColor.b, 43.0f / 255.0f);
        for (const ConnectionDefinition& connection : puzzle.connections) {
            allConnectionsUseOneThickness =
                allConnectionsUseOneThickness &&
                NearlyEqual(connection.thicknessScale, 1.0f);
        }
    }
    context.Expect(allPuzzlesUsePixelCrimson,
                   "bundled puzzles use an equal-ended rough crimson vessel style");
    context.Expect(allConnectionsUseOneThickness,
                   "bundled route segments keep the same thickness across nodes");
}

} // namespace

void RunPuzzleCatalogTests(TestContext& context) {
    TestCsvReader(context);
    TestValidCatalogAndTopology(context);
    TestHeadersScalarsColorsAndAscii(context);
    TestReferencesAndDag(context);
    TestCanvasObstaclesAndLength(context);
    TestBundledFourTableCatalog(context);
}

} // namespace object_connect::tests
