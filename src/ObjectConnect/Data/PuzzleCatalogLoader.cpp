#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"

#include "ObjectConnect/Data/Csv.hpp"
#include "ObjectConnect/Geometry/Geometry2D.hpp"
#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace object_connect {
namespace {

constexpr std::string_view kLevelsCatalogName{"levels.csv"};
constexpr std::string_view kNodesCatalogName{"nodes.csv"};
constexpr std::string_view kConnectionsCatalogName{"connections.csv"};
constexpr std::string_view kObstaclesCatalogName{"obstacles.csv"};
constexpr float kCanvasWidth = 1280.0f;
constexpr float kCanvasHeight = 720.0f;
constexpr std::size_t kMinimumPointCount = 8;
constexpr std::size_t kMaximumPointCount = 12;
constexpr std::size_t kMaximumIdentifierLength = 64;
constexpr std::size_t kMaximumDisplayTextLength = 64;
constexpr float kMaximumTotalLength = 100000.0f;
constexpr float kMaximumSlackRatio = 4.0f;
constexpr float kMaximumVesselWidth = 256.0f;
constexpr float kMaximumThicknessScale = 8.0f;
constexpr float kMaximumFollowDelaySeconds = 1.0f;

constexpr std::array<std::string_view, 11> kLevelsHeader{
    "puzzle_id",          "title",          "start_node_id", "total_length",
    "minimum_slack_ratio", "background_color", "show_target_connections",
    "vessel_color",       "base_width",     "tip_width",     "width_variation",
};
constexpr std::array<std::string_view, 7> kNodesHeader{
    "puzzle_id", "node_id", "label", "x", "y", "radius", "color",
};
constexpr std::array<std::string_view, 7> kConnectionsHeader{
    "puzzle_id", "from_node_id", "to_node_id", "point_count", "thickness_scale",
    "follow_delay_seconds", "initial_direction_degrees",
};
constexpr std::array<std::string_view, 9> kObstaclesHeader{
    "puzzle_id", "obstacle_id", "shape", "center_x", "center_y", "width", "height",
    "radius", "color",
};

class PuzzleDataError final : public std::runtime_error {
public:
    explicit PuzzleDataError(const std::string& message)
        : std::runtime_error(message) {}
};

template <std::size_t Size>
[[nodiscard]] std::string JoinHeader(const std::array<std::string_view, Size>& header) {
    std::string result;
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (index != 0) {
            result.push_back(',');
        }
        result += header[index];
    }
    return result;
}

template <std::size_t Size>
void ValidateHeader(const data::CsvDocument& document,
                    const std::array<std::string_view, Size>& expected,
                    const std::string_view catalogName) {
    std::size_t mismatch = 0;
    const std::size_t sharedSize = (std::min)(document.header.size(), expected.size());
    while (mismatch < sharedSize && document.header[mismatch] == expected[mismatch]) {
        ++mismatch;
    }
    if (mismatch == sharedSize && document.header.size() == expected.size()) {
        return;
    }
    throw PuzzleDataError(std::string{catalogName} + " line 1, column " +
                          std::to_string(mismatch + 1) +
                          ": header must be exactly: " + JoinHeader(expected));
}

template <std::size_t Size>
[[noreturn]] void ThrowFieldError(const std::string_view catalogName,
                                  const std::size_t lineNumber,
                                  const std::string_view fieldName,
                                  const std::array<std::string_view, Size>& header,
                                  const std::string& detail) {
    std::size_t column = 0;
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (header[index] == fieldName) {
            column = index + 1;
            break;
        }
    }
    throw PuzzleDataError(std::string{catalogName} + " line " +
                          std::to_string(lineNumber) + ", column " +
                          std::to_string(column) + ", field '" + std::string{fieldName} +
                          "': " + detail);
}

[[nodiscard]] std::string ParseDefinitionId(const std::string& text,
                                            const std::string_view catalogName,
                                            const std::size_t lineNumber,
                                            const std::string_view fieldName,
                                            const auto& header) {
    if (text.empty() || text.size() > kMaximumIdentifierLength ||
        text.front() < 'a' || text.front() > 'z' || text.back() == '_') {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "ID must be at most 64 characters, use lower_snake_case, and begin "
                        "with a lowercase letter");
    }
    bool previousUnderscore = false;
    for (const char character : text) {
        const bool lowercaseLetter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        const bool underscore = character == '_';
        if ((!lowercaseLetter && !digit && !underscore) ||
            (underscore && previousUnderscore)) {
            ThrowFieldError(catalogName, lineNumber, fieldName, header,
                            "ID must use lower_snake_case and contain no repeated underscores");
        }
        previousUnderscore = underscore;
    }
    return text;
}

[[nodiscard]] std::string ParseAsciiText(const std::string& text,
                                         const std::string_view catalogName,
                                         const std::size_t lineNumber,
                                         const std::string_view fieldName,
                                         const auto& header) {
    if (text.empty() || text.size() > kMaximumDisplayTextLength) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "text must contain between 1 and 64 characters");
    }
    bool hasVisibleCharacter = false;
    for (const unsigned char character : text) {
        if (character < 0x20u || character > 0x7Eu) {
            ThrowFieldError(catalogName, lineNumber, fieldName, header,
                            "text must contain printable ASCII characters only");
        }
        hasVisibleCharacter = hasVisibleCharacter || character != static_cast<unsigned char>(' ');
    }
    if (!hasVisibleCharacter) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "text must contain at least one visible character");
    }
    return text;
}

[[nodiscard]] float ParseFloat(const std::string& text,
                               const std::string_view catalogName,
                               const std::size_t lineNumber,
                               const std::string_view fieldName,
                               const auto& header) {
    float value = 0.0f;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result =
        std::from_chars(begin, end, value, std::chars_format::general);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end ||
        !std::isfinite(value)) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "expected a finite decimal number");
    }
    return value;
}

[[nodiscard]] std::size_t ParseSize(const std::string& text,
                                    const std::string_view catalogName,
                                    const std::size_t lineNumber,
                                    const std::string_view fieldName,
                                    const auto& header) {
    std::uint64_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value, 10);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end ||
        value > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)())) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "expected a non-negative integer that fits size_t");
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] bool ParseBoolean(const std::string& text,
                                const std::string_view catalogName,
                                const std::size_t lineNumber,
                                const std::string_view fieldName,
                                const auto& header) {
    if (text == "true") {
        return true;
    }
    if (text == "false") {
        return false;
    }
    ThrowFieldError(catalogName, lineNumber, fieldName, header,
                    "expected exactly 'true' or 'false'");
}

[[nodiscard]] int ParseHexDigit(const char character) noexcept {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

[[nodiscard]] Color ParseColor(const std::string& text,
                               const std::string_view catalogName,
                               const std::size_t lineNumber,
                               const std::string_view fieldName,
                               const auto& header) {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "expected #RRGGBB or #RRGGBBAA");
    }

    std::array<int, 4> channels{0, 0, 0, 255};
    const std::size_t channelCount = text.size() == 9 ? 4 : 3;
    for (std::size_t index = 0; index < channelCount; ++index) {
        const int high = ParseHexDigit(text[1 + index * 2]);
        const int low = ParseHexDigit(text[2 + index * 2]);
        if (high < 0 || low < 0) {
            ThrowFieldError(catalogName, lineNumber, fieldName, header,
                            "expected #RRGGBB or #RRGGBBAA with hexadecimal digits");
        }
        channels[index] = high * 16 + low;
    }
    constexpr float divisor = 255.0f;
    return {
        static_cast<float>(channels[0]) / divisor,
        static_cast<float>(channels[1]) / divisor,
        static_cast<float>(channels[2]) / divisor,
        static_cast<float>(channels[3]) / divisor,
    };
}

void ValidatePositive(const float value, const std::string_view catalogName,
                      const std::size_t lineNumber, const std::string_view fieldName,
                      const auto& header) {
    if (value <= 0.0f) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "value must be greater than zero");
    }
}

void ValidateNonNegative(const float value, const std::string_view catalogName,
                         const std::size_t lineNumber, const std::string_view fieldName,
                         const auto& header) {
    if (value < 0.0f) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "value must be non-negative");
    }
}

struct RawConnection final {
    ConnectionDefinition definition;
    std::size_t lineNumber = 0;
};

struct WorkingPuzzle final {
    PuzzleDefinition definition;
    std::size_t levelLineNumber = 0;
    std::unordered_map<std::string, std::size_t> nodeIndices;
    std::vector<std::size_t> nodeLineNumbers;
    std::vector<RawConnection> rawConnections;
    std::unordered_set<std::string> obstacleIds;
};

[[nodiscard]] bool CirclesOverlap(const Vec2 leftCenter, const float leftRadius,
                                  const Vec2 rightCenter, const float rightRadius) noexcept {
    const double deltaX = static_cast<double>(leftCenter.x) - rightCenter.x;
    const double deltaY = static_cast<double>(leftCenter.y) - rightCenter.y;
    const double combinedRadius = static_cast<double>(leftRadius) + rightRadius;
    return deltaX * deltaX + deltaY * deltaY <= combinedRadius * combinedRadius;
}

void ValidateNodeInsideCanvas(const NodeDefinition& node, const std::size_t lineNumber) {
    if (node.position.x - node.radius < 0.0f ||
        node.position.y - node.radius < 0.0f ||
        node.position.x + node.radius > kCanvasWidth ||
        node.position.y + node.radius > kCanvasHeight) {
        ThrowFieldError(kNodesCatalogName, lineNumber, "radius", kNodesHeader,
                        "node circle must fit completely inside the 1280x720 canvas");
    }
}

void ValidateObstacleInsideCanvas(const ObstacleDefinition& obstacle,
                                  const std::size_t lineNumber) {
    if (obstacle.shape == ObstacleShape::Rectangle) {
        const float halfWidth = obstacle.width * 0.5f;
        const float halfHeight = obstacle.height * 0.5f;
        if (obstacle.center.x - halfWidth < 0.0f ||
            obstacle.center.y - halfHeight < 0.0f ||
            obstacle.center.x + halfWidth > kCanvasWidth ||
            obstacle.center.y + halfHeight > kCanvasHeight) {
            ThrowFieldError(kObstaclesCatalogName, lineNumber, "width",
                            kObstaclesHeader,
                            "rectangle must fit completely inside the 1280x720 canvas");
        }
        return;
    }
    if (obstacle.center.x - obstacle.radius < 0.0f ||
        obstacle.center.y - obstacle.radius < 0.0f ||
        obstacle.center.x + obstacle.radius > kCanvasWidth ||
        obstacle.center.y + obstacle.radius > kCanvasHeight) {
        ThrowFieldError(kObstaclesCatalogName, lineNumber, "radius",
                        kObstaclesHeader,
                        "circle must fit completely inside the 1280x720 canvas");
    }
}

[[nodiscard]] std::vector<WorkingPuzzle> ParseLevels(const data::CsvDocument& document) {
    ValidateHeader(document, kLevelsHeader, kLevelsCatalogName);
    if (document.records.empty()) {
        throw PuzzleDataError("levels.csv must contain at least one puzzle");
    }

    std::vector<WorkingPuzzle> puzzles;
    puzzles.reserve(document.records.size());
    std::unordered_set<std::string> puzzleIds;
    for (const data::CsvRecord& record : document.records) {
        WorkingPuzzle working;
        PuzzleDefinition& puzzle = working.definition;
        puzzle.id = ParseDefinitionId(record.fields[0], kLevelsCatalogName,
                                      record.lineNumber, kLevelsHeader[0], kLevelsHeader);
        if (!puzzleIds.insert(puzzle.id).second) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber, kLevelsHeader[0],
                            kLevelsHeader, "duplicate puzzle ID");
        }
        puzzle.title = ParseAsciiText(record.fields[1], kLevelsCatalogName,
                                      record.lineNumber, kLevelsHeader[1], kLevelsHeader);
        puzzle.startNodeId = ParseDefinitionId(record.fields[2], kLevelsCatalogName,
                                               record.lineNumber, kLevelsHeader[2],
                                               kLevelsHeader);
        puzzle.totalLength = ParseFloat(record.fields[3], kLevelsCatalogName,
                                        record.lineNumber, kLevelsHeader[3], kLevelsHeader);
        ValidatePositive(puzzle.totalLength, kLevelsCatalogName, record.lineNumber,
                         kLevelsHeader[3], kLevelsHeader);
        if (puzzle.totalLength > kMaximumTotalLength) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber, kLevelsHeader[3],
                            kLevelsHeader, "value must not exceed 100000 pixels");
        }
        puzzle.minimumSlackRatio =
            ParseFloat(record.fields[4], kLevelsCatalogName, record.lineNumber,
                       kLevelsHeader[4], kLevelsHeader);
        if (puzzle.minimumSlackRatio < 1.0f ||
            puzzle.minimumSlackRatio > kMaximumSlackRatio) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber, kLevelsHeader[4],
                            kLevelsHeader, "value must be in the range [1, 4]");
        }
        puzzle.backgroundColor = ParseColor(record.fields[5], kLevelsCatalogName,
                                            record.lineNumber, kLevelsHeader[5],
                                            kLevelsHeader);
        puzzle.showTargetConnections =
            ParseBoolean(record.fields[6], kLevelsCatalogName, record.lineNumber,
                         kLevelsHeader[6], kLevelsHeader);
        puzzle.vesselColor = ParseColor(record.fields[7], kLevelsCatalogName,
                                        record.lineNumber, kLevelsHeader[7], kLevelsHeader);
        puzzle.baseWidth = ParseFloat(record.fields[8], kLevelsCatalogName,
                                      record.lineNumber, kLevelsHeader[8], kLevelsHeader);
        puzzle.tipWidth = ParseFloat(record.fields[9], kLevelsCatalogName,
                                     record.lineNumber, kLevelsHeader[9], kLevelsHeader);
        ValidatePositive(puzzle.baseWidth, kLevelsCatalogName, record.lineNumber,
                         kLevelsHeader[8], kLevelsHeader);
        ValidatePositive(puzzle.tipWidth, kLevelsCatalogName, record.lineNumber,
                         kLevelsHeader[9], kLevelsHeader);
        if (puzzle.baseWidth > kMaximumVesselWidth ||
            puzzle.tipWidth > kMaximumVesselWidth) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber, kLevelsHeader[8],
                            kLevelsHeader, "vessel widths must not exceed 256 pixels");
        }
        if (puzzle.baseWidth < puzzle.tipWidth) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber, kLevelsHeader[8],
                            kLevelsHeader,
                            "base_width must be greater than or equal to tip_width");
        }
        puzzle.widthVariation =
            ParseFloat(record.fields[10], kLevelsCatalogName, record.lineNumber,
                       kLevelsHeader[10], kLevelsHeader);
        if (puzzle.widthVariation < 0.0f || puzzle.widthVariation >= 1.0f) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber, kLevelsHeader[10],
                            kLevelsHeader, "value must be in the range [0, 1)");
        }
        working.levelLineNumber = record.lineNumber;
        puzzles.push_back(std::move(working));
    }
    return puzzles;
}

[[nodiscard]] std::unordered_map<std::string, std::size_t> BuildPuzzleIndex(
    const std::vector<WorkingPuzzle>& puzzles) {
    std::unordered_map<std::string, std::size_t> indices;
    indices.reserve(puzzles.size());
    for (std::size_t index = 0; index < puzzles.size(); ++index) {
        indices.emplace(puzzles[index].definition.id, index);
    }
    return indices;
}

[[nodiscard]] WorkingPuzzle& RequirePuzzle(
    std::vector<WorkingPuzzle>& puzzles,
    const std::unordered_map<std::string, std::size_t>& puzzleIndices,
    const std::string& puzzleId, const std::string_view catalogName,
    const std::size_t lineNumber, const auto& header) {
    const auto found = puzzleIndices.find(puzzleId);
    if (found == puzzleIndices.end()) {
        ThrowFieldError(catalogName, lineNumber, header[0], header,
                        "referenced puzzle ID does not exist");
    }
    return puzzles[found->second];
}

void ParseNodes(const data::CsvDocument& document, std::vector<WorkingPuzzle>& puzzles,
                const std::unordered_map<std::string, std::size_t>& puzzleIndices) {
    ValidateHeader(document, kNodesHeader, kNodesCatalogName);
    for (const data::CsvRecord& record : document.records) {
        const std::string puzzleId = ParseDefinitionId(
            record.fields[0], kNodesCatalogName, record.lineNumber, kNodesHeader[0],
            kNodesHeader);
        WorkingPuzzle& puzzle = RequirePuzzle(puzzles, puzzleIndices, puzzleId,
                                              kNodesCatalogName, record.lineNumber,
                                              kNodesHeader);
        NodeDefinition node;
        node.id = ParseDefinitionId(record.fields[1], kNodesCatalogName,
                                    record.lineNumber, kNodesHeader[1], kNodesHeader);
        if (puzzle.nodeIndices.contains(node.id)) {
            ThrowFieldError(kNodesCatalogName, record.lineNumber, kNodesHeader[1],
                            kNodesHeader, "duplicate node ID within puzzle");
        }
        node.label = ParseAsciiText(record.fields[2], kNodesCatalogName,
                                    record.lineNumber, kNodesHeader[2], kNodesHeader);
        node.position.x = ParseFloat(record.fields[3], kNodesCatalogName,
                                     record.lineNumber, kNodesHeader[3], kNodesHeader);
        node.position.y = ParseFloat(record.fields[4], kNodesCatalogName,
                                     record.lineNumber, kNodesHeader[4], kNodesHeader);
        node.radius = ParseFloat(record.fields[5], kNodesCatalogName,
                                 record.lineNumber, kNodesHeader[5], kNodesHeader);
        ValidatePositive(node.radius, kNodesCatalogName, record.lineNumber,
                         kNodesHeader[5], kNodesHeader);
        node.color = ParseColor(record.fields[6], kNodesCatalogName,
                                record.lineNumber, kNodesHeader[6], kNodesHeader);
        ValidateNodeInsideCanvas(node, record.lineNumber);

        const std::size_t nodeIndex = puzzle.definition.nodes.size();
        puzzle.nodeIndices.emplace(node.id, nodeIndex);
        puzzle.definition.nodes.push_back(std::move(node));
        puzzle.nodeLineNumbers.push_back(record.lineNumber);
    }

    for (WorkingPuzzle& puzzle : puzzles) {
        if (puzzle.definition.nodes.size() < 2) {
            ThrowFieldError(kLevelsCatalogName, puzzle.levelLineNumber, kLevelsHeader[0],
                            kLevelsHeader, "puzzle must define at least two nodes");
        }
        const auto start = puzzle.nodeIndices.find(puzzle.definition.startNodeId);
        if (start == puzzle.nodeIndices.end()) {
            ThrowFieldError(kLevelsCatalogName, puzzle.levelLineNumber,
                            kLevelsHeader[2], kLevelsHeader,
                            "start node ID does not exist in nodes.csv");
        }
        puzzle.definition.startNodeIndex = start->second;

        for (std::size_t right = 0; right < puzzle.definition.nodes.size(); ++right) {
            for (std::size_t left = 0; left < right; ++left) {
                const NodeDefinition& leftNode = puzzle.definition.nodes[left];
                const NodeDefinition& rightNode = puzzle.definition.nodes[right];
                if (CirclesOverlap(leftNode.position, leftNode.radius,
                                   rightNode.position, rightNode.radius)) {
                    ThrowFieldError(kNodesCatalogName, puzzle.nodeLineNumbers[right],
                                    kNodesHeader[5], kNodesHeader,
                                    "node circle overlaps node '" + leftNode.id + "'");
                }
            }
        }
    }
}

void ParseObstacles(const data::CsvDocument& document,
                    std::vector<WorkingPuzzle>& puzzles,
                    const std::unordered_map<std::string, std::size_t>& puzzleIndices) {
    ValidateHeader(document, kObstaclesHeader, kObstaclesCatalogName);
    for (const data::CsvRecord& record : document.records) {
        const std::string puzzleId = ParseDefinitionId(
            record.fields[0], kObstaclesCatalogName, record.lineNumber,
            kObstaclesHeader[0], kObstaclesHeader);
        WorkingPuzzle& puzzle = RequirePuzzle(puzzles, puzzleIndices, puzzleId,
                                              kObstaclesCatalogName, record.lineNumber,
                                              kObstaclesHeader);
        ObstacleDefinition obstacle;
        obstacle.id = ParseDefinitionId(record.fields[1], kObstaclesCatalogName,
                                        record.lineNumber, kObstaclesHeader[1],
                                        kObstaclesHeader);
        if (!puzzle.obstacleIds.insert(obstacle.id).second) {
            ThrowFieldError(kObstaclesCatalogName, record.lineNumber,
                            kObstaclesHeader[1], kObstaclesHeader,
                            "duplicate obstacle ID within puzzle");
        }
        if (record.fields[2] == "rectangle") {
            obstacle.shape = ObstacleShape::Rectangle;
        } else if (record.fields[2] == "circle") {
            obstacle.shape = ObstacleShape::Circle;
        } else {
            ThrowFieldError(kObstaclesCatalogName, record.lineNumber,
                            kObstaclesHeader[2], kObstaclesHeader,
                            "expected exactly 'rectangle' or 'circle'");
        }
        obstacle.center.x = ParseFloat(record.fields[3], kObstaclesCatalogName,
                                       record.lineNumber, kObstaclesHeader[3],
                                       kObstaclesHeader);
        obstacle.center.y = ParseFloat(record.fields[4], kObstaclesCatalogName,
                                       record.lineNumber, kObstaclesHeader[4],
                                       kObstaclesHeader);
        if (obstacle.shape == ObstacleShape::Rectangle) {
            obstacle.width = ParseFloat(record.fields[5], kObstaclesCatalogName,
                                        record.lineNumber, kObstaclesHeader[5],
                                        kObstaclesHeader);
            obstacle.height = ParseFloat(record.fields[6], kObstaclesCatalogName,
                                         record.lineNumber, kObstaclesHeader[6],
                                         kObstaclesHeader);
            ValidatePositive(obstacle.width, kObstaclesCatalogName, record.lineNumber,
                             kObstaclesHeader[5], kObstaclesHeader);
            ValidatePositive(obstacle.height, kObstaclesCatalogName, record.lineNumber,
                             kObstaclesHeader[6], kObstaclesHeader);
            if (!record.fields[7].empty()) {
                ThrowFieldError(kObstaclesCatalogName, record.lineNumber,
                                kObstaclesHeader[7], kObstaclesHeader,
                                "rectangle radius must be empty");
            }
        } else {
            if (!record.fields[5].empty() || !record.fields[6].empty()) {
                ThrowFieldError(kObstaclesCatalogName, record.lineNumber,
                                !record.fields[5].empty() ? kObstaclesHeader[5]
                                                          : kObstaclesHeader[6],
                                kObstaclesHeader,
                                "circle width and height must be empty");
            }
            obstacle.radius = ParseFloat(record.fields[7], kObstaclesCatalogName,
                                         record.lineNumber, kObstaclesHeader[7],
                                         kObstaclesHeader);
            ValidatePositive(obstacle.radius, kObstaclesCatalogName, record.lineNumber,
                             kObstaclesHeader[7], kObstaclesHeader);
        }
        obstacle.color = ParseColor(record.fields[8], kObstaclesCatalogName,
                                    record.lineNumber, kObstaclesHeader[8],
                                    kObstaclesHeader);
        ValidateObstacleInsideCanvas(obstacle, record.lineNumber);

        for (const NodeDefinition& node : puzzle.definition.nodes) {
            const bool overlaps = obstacle.shape == ObstacleShape::Rectangle
                                      ? CircleOverlapsRectangle(
                                            node.position, node.radius, obstacle.center,
                                            obstacle.width, obstacle.height)
                                      : CirclesOverlap(node.position, node.radius,
                                                       obstacle.center, obstacle.radius);
            if (overlaps) {
                ThrowFieldError(kObstaclesCatalogName, record.lineNumber,
                                kObstaclesHeader[1], kObstaclesHeader,
                                "obstacle overlaps node '" + node.id + "'");
            }
        }
        puzzle.definition.obstacles.push_back(std::move(obstacle));
    }
}

void ParseConnections(const data::CsvDocument& document,
                      std::vector<WorkingPuzzle>& puzzles,
                      const std::unordered_map<std::string, std::size_t>& puzzleIndices) {
    ValidateHeader(document, kConnectionsHeader, kConnectionsCatalogName);
    for (const data::CsvRecord& record : document.records) {
        const std::string puzzleId = ParseDefinitionId(
            record.fields[0], kConnectionsCatalogName, record.lineNumber,
            kConnectionsHeader[0], kConnectionsHeader);
        WorkingPuzzle& puzzle = RequirePuzzle(puzzles, puzzleIndices, puzzleId,
                                              kConnectionsCatalogName, record.lineNumber,
                                              kConnectionsHeader);
        RawConnection raw;
        ConnectionDefinition& connection = raw.definition;
        connection.fromNodeId = ParseDefinitionId(
            record.fields[1], kConnectionsCatalogName, record.lineNumber,
            kConnectionsHeader[1], kConnectionsHeader);
        connection.toNodeId = ParseDefinitionId(
            record.fields[2], kConnectionsCatalogName, record.lineNumber,
            kConnectionsHeader[2], kConnectionsHeader);
        const auto fromNode = puzzle.nodeIndices.find(connection.fromNodeId);
        if (fromNode == puzzle.nodeIndices.end()) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[1], kConnectionsHeader,
                            "referenced from-node ID does not exist");
        }
        const auto toNode = puzzle.nodeIndices.find(connection.toNodeId);
        if (toNode == puzzle.nodeIndices.end()) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[2], kConnectionsHeader,
                            "referenced to-node ID does not exist");
        }
        if (connection.fromNodeId == connection.toNodeId) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[2], kConnectionsHeader,
                            "connection must not link a node to itself");
        }
        connection.fromNodeIndex = fromNode->second;
        connection.toNodeIndex = toNode->second;
        connection.pointCount = ParseSize(record.fields[3], kConnectionsCatalogName,
                                          record.lineNumber, kConnectionsHeader[3],
                                          kConnectionsHeader);
        if (connection.pointCount < kMinimumPointCount ||
            connection.pointCount > kMaximumPointCount) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[3], kConnectionsHeader,
                            "point_count must be between 8 and 12 inclusive");
        }
        connection.thicknessScale =
            ParseFloat(record.fields[4], kConnectionsCatalogName, record.lineNumber,
                       kConnectionsHeader[4], kConnectionsHeader);
        ValidatePositive(connection.thicknessScale, kConnectionsCatalogName,
                         record.lineNumber, kConnectionsHeader[4], kConnectionsHeader);
        if (connection.thicknessScale > kMaximumThicknessScale) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[4], kConnectionsHeader,
                            "thickness_scale must not exceed 8");
        }
        connection.followDelaySeconds =
            ParseFloat(record.fields[5], kConnectionsCatalogName, record.lineNumber,
                       kConnectionsHeader[5], kConnectionsHeader);
        ValidateNonNegative(connection.followDelaySeconds, kConnectionsCatalogName,
                            record.lineNumber, kConnectionsHeader[5],
                            kConnectionsHeader);
        if (connection.followDelaySeconds > kMaximumFollowDelaySeconds) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[5], kConnectionsHeader,
                            "follow_delay_seconds must not exceed 1 second");
        }
        connection.initialDirectionDegrees =
            ParseFloat(record.fields[6], kConnectionsCatalogName, record.lineNumber,
                       kConnectionsHeader[6], kConnectionsHeader);
        if (connection.initialDirectionDegrees < -360.0f ||
            connection.initialDirectionDegrees > 360.0f) {
            ThrowFieldError(kConnectionsCatalogName, record.lineNumber,
                            kConnectionsHeader[6], kConnectionsHeader,
                            "initial_direction_degrees must be in the range [-360, 360]");
        }
        raw.lineNumber = record.lineNumber;
        puzzle.rawConnections.push_back(std::move(raw));
    }
}

void ValidateAndStoreConnections(WorkingPuzzle& puzzle) {
    if (puzzle.rawConnections.empty()) {
        ThrowFieldError(kLevelsCatalogName, puzzle.levelLineNumber, kLevelsHeader[0],
                        kLevelsHeader, "puzzle must define at least one connection");
    }

    const std::size_t nodeCount = puzzle.definition.nodes.size();
    // The CSV describes candidate routes. More than one edge may leave or enter
    // a node, while PuzzleBoard still commits only one edge from its current tip.
    std::vector<std::vector<std::size_t>> outgoing(nodeCount);
    std::vector<std::size_t> incomingCounts(nodeCount, 0);
    std::vector<bool> connectedNodes(nodeCount, false);
    std::unordered_set<std::string> directedEdges;
    directedEdges.reserve(puzzle.rawConnections.size());
    for (std::size_t index = 0; index < puzzle.rawConnections.size(); ++index) {
        const RawConnection& raw = puzzle.rawConnections[index];
        const ConnectionDefinition& connection = raw.definition;
        const std::string edgeKey = connection.fromNodeId + "\n" + connection.toNodeId;
        if (!directedEdges.insert(edgeKey).second) {
            ThrowFieldError(kConnectionsCatalogName, raw.lineNumber,
                            kConnectionsHeader[2], kConnectionsHeader,
                            "duplicate directed connection");
        }
        outgoing[connection.fromNodeIndex].push_back(index);
        ++incomingCounts[connection.toNodeIndex];
        connectedNodes[connection.fromNodeIndex] = true;
        connectedNodes[connection.toNodeIndex] = true;
    }

    // Every authored edge must belong to the graph reachable from the start.
    // Isolated nodes are allowed as harmless visual/authoring decoys.
    std::vector<bool> reachableNodes(nodeCount, false);
    std::vector<bool> reachableConnections(puzzle.rawConnections.size(), false);
    std::vector<std::size_t> reachableFrontier;
    reachableFrontier.reserve(nodeCount);
    reachableNodes[puzzle.definition.startNodeIndex] = true;
    reachableFrontier.push_back(puzzle.definition.startNodeIndex);
    std::size_t reachableConnectionCount = 0;
    for (std::size_t cursor = 0; cursor < reachableFrontier.size(); ++cursor) {
        const std::size_t nodeIndex = reachableFrontier[cursor];
        for (const std::size_t connectionIndex : outgoing[nodeIndex]) {
            if (!reachableConnections[connectionIndex]) {
                reachableConnections[connectionIndex] = true;
                ++reachableConnectionCount;
            }
            const std::size_t toNodeIndex =
                puzzle.rawConnections[connectionIndex].definition.toNodeIndex;
            if (!reachableNodes[toNodeIndex]) {
                reachableNodes[toNodeIndex] = true;
                reachableFrontier.push_back(toNodeIndex);
            }
        }
    }
    if (reachableConnectionCount != puzzle.rawConnections.size()) {
        for (std::size_t index = 0; index < reachableConnections.size(); ++index) {
            if (!reachableConnections[index]) {
                ThrowFieldError(kConnectionsCatalogName,
                                puzzle.rawConnections[index].lineNumber,
                                kConnectionsHeader[1], kConnectionsHeader,
                                "every connection must be reachable from start_node_id");
            }
        }
    }

    // Kahn's algorithm gives a readable cycle check and an order that the
    // shortest-path validation below can reuse. CSV edge order is not changed.
    std::vector<std::size_t> remainingIncoming = incomingCounts;
    std::vector<std::size_t> readyNodes;
    readyNodes.reserve(nodeCount);
    std::size_t connectedNodeCount = 0;
    for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        if (!connectedNodes[nodeIndex]) {
            continue;
        }
        ++connectedNodeCount;
        if (remainingIncoming[nodeIndex] == 0) {
            readyNodes.push_back(nodeIndex);
        }
    }

    std::vector<std::size_t> topologicalNodes;
    topologicalNodes.reserve(connectedNodeCount);
    for (std::size_t cursor = 0; cursor < readyNodes.size(); ++cursor) {
        const std::size_t nodeIndex = readyNodes[cursor];
        topologicalNodes.push_back(nodeIndex);
        for (const std::size_t connectionIndex : outgoing[nodeIndex]) {
            const std::size_t toNodeIndex =
                puzzle.rawConnections[connectionIndex].definition.toNodeIndex;
            --remainingIncoming[toNodeIndex];
            if (remainingIncoming[toNodeIndex] == 0) {
                readyNodes.push_back(toNodeIndex);
            }
        }
    }
    if (topologicalNodes.size() != connectedNodeCount) {
        ThrowFieldError(kConnectionsCatalogName,
                        puzzle.rawConnections.front().lineNumber,
                        kConnectionsHeader[0], kConnectionsHeader,
                        "connection topology contains a directed cycle");
    }

    // One abstract terminal keeps completion data-driven without hard-coding a
    // story role such as "brain" into the core.
    std::size_t terminalNodeIndex = nodeCount;
    std::size_t terminalCount = 0;
    for (std::size_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        if (connectedNodes[nodeIndex] && outgoing[nodeIndex].empty()) {
            terminalNodeIndex = nodeIndex;
            ++terminalCount;
        }
    }
    if (terminalCount != 1) {
        ThrowFieldError(kConnectionsCatalogName,
                        puzzle.rawConnections.front().lineNumber,
                        kConnectionsHeader[0], kConnectionsHeader,
                        "connection topology must have exactly one reachable terminal node");
    }

    // Every selectable edge must be geometrically valid, even when another edge
    // offers a shorter route through the puzzle.
    std::vector<double> connectionLengths(puzzle.rawConnections.size(), 0.0);
    for (std::size_t index = 0; index < puzzle.rawConnections.size(); ++index) {
        const RawConnection& raw = puzzle.rawConnections[index];
        const ConnectionDefinition& connection = raw.definition;
        const NodeDefinition& from = puzzle.definition.nodes[connection.fromNodeIndex];
        const NodeDefinition& to = puzzle.definition.nodes[connection.toNodeIndex];
        const double deltaX = static_cast<double>(to.position.x) - from.position.x;
        const double deltaY = static_cast<double>(to.position.y) - from.position.y;
        connectionLengths[index] = std::sqrt(deltaX * deltaX + deltaY * deltaY);
        if (!std::isfinite(connectionLengths[index])) {
            ThrowFieldError(kConnectionsCatalogName,
                            raw.lineNumber,
                            kConnectionsHeader[2], kConnectionsHeader,
                            "connection length must remain finite");
        }

        const float maximumWidth =
            (std::max)(puzzle.definition.baseWidth, puzzle.definition.tipWidth);
        // Vertex snapping can move an edge by part of one pixel cell. Reserve a
        // full cell so loader validation remains conservative at every angle.
        const float clearance =
            maximumWidth * 0.5f * connection.thicknessScale +
            kDefaultTentaclePixelGridSize;
        if (IsConnectionBlocked(from.position, to.position,
                                puzzle.definition.obstacles, clearance)) {
            ThrowFieldError(kConnectionsCatalogName,
                            raw.lineNumber,
                            kConnectionsHeader[2], kConnectionsHeader,
                            "required connection intersects an obstacle after vessel clearance");
        }
    }

    // Candidate edges are alternatives, so only the cheapest complete route is
    // required to fit the global budget. Unselected edges consume no length.
    std::vector<double> shortestLengths(nodeCount,
                                        (std::numeric_limits<double>::infinity)());
    shortestLengths[puzzle.definition.startNodeIndex] = 0.0;
    for (const std::size_t nodeIndex : topologicalNodes) {
        for (const std::size_t connectionIndex : outgoing[nodeIndex]) {
            const std::size_t toNodeIndex =
                puzzle.rawConnections[connectionIndex].definition.toNodeIndex;
            shortestLengths[toNodeIndex] =
                (std::min)(shortestLengths[toNodeIndex],
                           shortestLengths[nodeIndex] +
                               connectionLengths[connectionIndex]);
        }
    }
    const double requiredLength =
        shortestLengths[terminalNodeIndex] *
        static_cast<double>(puzzle.definition.minimumSlackRatio);
    if (!std::isfinite(requiredLength) ||
        static_cast<double>(puzzle.definition.totalLength) < requiredLength) {
        ThrowFieldError(kLevelsCatalogName, puzzle.levelLineNumber, kLevelsHeader[3],
                        kLevelsHeader,
                        "total_length must cover the shortest start-to-terminal path "
                        "length multiplied by "
                        "minimum_slack_ratio");
    }

    puzzle.definition.connections.reserve(puzzle.rawConnections.size());
    for (RawConnection& raw : puzzle.rawConnections) {
        puzzle.definition.connections.push_back(std::move(raw.definition));
    }
}

[[nodiscard]] PuzzleCatalog BuildCatalog(const data::CsvDocument& levels,
                                         const data::CsvDocument& nodes,
                                         const data::CsvDocument& connections,
                                         const data::CsvDocument& obstacles) {
    std::vector<WorkingPuzzle> working = ParseLevels(levels);
    const std::unordered_map<std::string, std::size_t> puzzleIndices =
        BuildPuzzleIndex(working);
    ParseNodes(nodes, working, puzzleIndices);
    ParseObstacles(obstacles, working, puzzleIndices);
    ParseConnections(connections, working, puzzleIndices);

    std::vector<PuzzleDefinition> definitions;
    definitions.reserve(working.size());
    for (WorkingPuzzle& puzzle : working) {
        ValidateAndStoreConnections(puzzle);
        definitions.push_back(std::move(puzzle.definition));
    }
    return PuzzleCatalog{std::move(definitions)};
}

[[nodiscard]] const data::CsvDocument& RequireDocument(
    const data::CsvParseResult& result, const std::string_view label) {
    if (!result.document.has_value()) {
        throw PuzzleDataError(std::string{label} + ": " + result.error);
    }
    return *result.document;
}

[[nodiscard]] std::filesystem::path ValidateCatalogPath(
    const std::string& text, const std::filesystem::path& resourceRoot,
    const std::string_view label) {
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find('\r') != std::string::npos || text.find('\n') != std::string::npos ||
        text.find(':') != std::string::npos) {
        throw PuzzleDataError(std::string{label} +
                              " catalog path must be a non-empty relative path");
    }
    const std::filesystem::path relative{text};
    if (relative.has_root_path() || relative.has_root_name() || relative.filename().empty()) {
        throw PuzzleDataError(std::string{label} +
                              " catalog path must be relative to the resource root");
    }
    for (const std::filesystem::path& component : relative) {
        if (component.empty() || component == "." || component == "..") {
            throw PuzzleDataError(std::string{label} +
                                  " catalog path must remain within the resource root");
        }
    }
    return resourceRoot / relative;
}

} // namespace

bool PuzzleCatalogLoader::Load(const PuzzleDataPaths& paths,
                               const std::string& resourceRoot,
                               PuzzleCatalog& catalog,
                               std::string& error) {
    error.clear();
    try {
        const std::filesystem::path root{resourceRoot};
        std::error_code filesystemError;
        if (resourceRoot.empty() || !std::filesystem::is_directory(root, filesystemError)) {
            std::string detail = "puzzle-data resource root is not a directory: " +
                                 root.generic_string();
            if (filesystemError) {
                detail += " (" + filesystemError.message() + ")";
            }
            throw PuzzleDataError(detail);
        }
        const data::CsvParseResult levels =
            data::Csv::Load(ValidateCatalogPath(paths.levels, root, "levels"));
        const data::CsvParseResult nodes =
            data::Csv::Load(ValidateCatalogPath(paths.nodes, root, "nodes"));
        const data::CsvParseResult connections =
            data::Csv::Load(ValidateCatalogPath(paths.connections, root, "connections"));
        const data::CsvParseResult obstacles =
            data::Csv::Load(ValidateCatalogPath(paths.obstacles, root, "obstacles"));

        PuzzleCatalog parsed = BuildCatalog(
            RequireDocument(levels, kLevelsCatalogName),
            RequireDocument(nodes, kNodesCatalogName),
            RequireDocument(connections, kConnectionsCatalogName),
            RequireDocument(obstacles, kObstaclesCatalogName));
        catalog = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool PuzzleCatalogLoader::Parse(const PuzzleCsvSources& sources,
                                PuzzleCatalog& catalog,
                                std::string& error) {
    error.clear();
    try {
        const data::CsvParseResult levels = data::Csv::Parse(sources.levels);
        const data::CsvParseResult nodes = data::Csv::Parse(sources.nodes);
        const data::CsvParseResult connections = data::Csv::Parse(sources.connections);
        const data::CsvParseResult obstacles = data::Csv::Parse(sources.obstacles);
        PuzzleCatalog parsed = BuildCatalog(
            RequireDocument(levels, kLevelsCatalogName),
            RequireDocument(nodes, kNodesCatalogName),
            RequireDocument(connections, kConnectionsCatalogName),
            RequireDocument(obstacles, kObstaclesCatalogName));
        catalog = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace object_connect
