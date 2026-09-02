#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <queue>
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

using Json = nlohmann::json;

constexpr std::size_t kMaximumIdentifierLength = 64;
constexpr std::size_t kMaximumDisplayTextLength = 64;
constexpr std::size_t kMaximumPathLength = 512;
constexpr std::size_t kMaximumLevels = 128;
constexpr std::size_t kMaximumTiles = 65535;
constexpr std::size_t kMaximumAtlasDimension = 4096;
constexpr std::size_t kMaximumNodeTypes = 512;
constexpr std::size_t kMaximumStampColumns = kPuzzleGridColumns;
constexpr std::size_t kMaximumStampRows = kPuzzleGridRows;
constexpr std::size_t kMaximumStampCells =
    kPuzzleGridColumns * kPuzzleGridRows;
constexpr std::size_t kMaximumNodes = 512;
constexpr std::size_t kMaximumConnections = 4096;
constexpr std::size_t kMaximumNodeCapacity = 32;
constexpr std::uintmax_t kMaximumJsonFileBytes = 16u * 1024u * 1024u;
constexpr float kMaximumTotalLength = 100000.0f;
constexpr float kMaximumSlackRatio = 4.0f;
constexpr float kMaximumVesselWidth = 256.0f;
constexpr float kMaximumThicknessScale = 8.0f;
constexpr float kMaximumFollowDelaySeconds = 1.0f;
constexpr float kMaximumDirectionDegrees = 360.0f;
constexpr float kObstacleClearancePadding = 2.0f;

class PuzzleDataError final : public std::runtime_error {
public:
    explicit PuzzleDataError(const std::string& message)
        : std::runtime_error(message) {}
};

[[nodiscard]] std::string EscapePointerToken(const std::string_view token) {
    std::string escaped;
    escaped.reserve(token.size());
    for (const char character : token) {
        if (character == '~') {
            escaped += "~0";
        } else if (character == '/') {
            escaped += "~1";
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

[[nodiscard]] std::string ChildPointer(const std::string_view parent,
                                       const std::string_view child) {
    std::string result{parent};
    result.push_back('/');
    result += EscapePointerToken(child);
    return result;
}

[[nodiscard]] std::string IndexPointer(const std::string_view parent,
                                       const std::size_t index) {
    return ChildPointer(parent, std::to_string(index));
}

[[noreturn]] void ThrowDataError(const std::string_view filename,
                                 const std::string_view pointer,
                                 const std::string& detail) {
    const std::string effectiveFilename =
        filename.empty() ? std::string{"<memory>"} : std::string{filename};
    const std::string effectivePointer =
        pointer.empty() ? std::string{"/"} : std::string{pointer};
    throw PuzzleDataError(effectiveFilename + "#" + effectivePointer + ": " + detail);
}

[[nodiscard]] Json ParseJsonDocument(const PuzzleJsonSource& source) {
    if (source.contents.size() > kMaximumJsonFileBytes) {
        ThrowDataError(source.filename, "/", "JSON document exceeds the 16 MiB limit");
    }
    try {
        std::vector<std::unordered_set<std::string>> objectKeys;
        const Json::parser_callback_t rejectDuplicateKeys =
            [&objectKeys, &source](const int depth,
                                   const Json::parse_event_t event,
                                   Json& parsed) {
                if (event == Json::parse_event_t::object_start) {
                    const std::size_t objectDepth = static_cast<std::size_t>(depth);
                    if (objectKeys.size() <= objectDepth) {
                        objectKeys.resize(objectDepth + 1);
                    }
                    objectKeys[objectDepth].clear();
                } else if (event == Json::parse_event_t::key) {
                    const std::size_t keyDepth = static_cast<std::size_t>(depth);
                    if (keyDepth == 0 || objectKeys.size() < keyDepth) {
                        ThrowDataError(source.filename, "/",
                                       "invalid JSON object nesting");
                    }
                    const std::string key = parsed.get<std::string>();
                    if (!objectKeys[keyDepth - 1].insert(key).second) {
                        ThrowDataError(source.filename, "/",
                                       "duplicate object key '" + key + "'");
                    }
                }
                return true;
            };
        return Json::parse(source.contents.begin(), source.contents.end(),
                           rejectDuplicateKeys);
    } catch (const Json::parse_error& exception) {
        ThrowDataError(source.filename, "/",
                       std::string{"invalid JSON syntax: "} + exception.what());
    }
}

void RequireExactKeys(const Json& value,
                      const std::initializer_list<std::string_view> expected,
                      const PuzzleJsonSource& source,
                      const std::string_view pointer) {
    if (!value.is_object()) {
        ThrowDataError(source.filename, pointer, "expected an object");
    }

    for (const std::string_view key : expected) {
        if (!value.contains(std::string{key})) {
            ThrowDataError(source.filename, ChildPointer(pointer, key),
                           "missing required key");
        }
    }
    for (auto iterator = value.cbegin(); iterator != value.cend(); ++iterator) {
        const bool known = std::any_of(
            expected.begin(), expected.end(), [&iterator](const std::string_view key) {
                return iterator.key() == key;
            });
        if (!known) {
            ThrowDataError(source.filename, ChildPointer(pointer, iterator.key()),
                           "unexpected key");
        }
    }
}

void RequireArray(const Json& value, const PuzzleJsonSource& source,
                  const std::string_view pointer) {
    if (!value.is_array()) {
        ThrowDataError(source.filename, pointer, "expected an array");
    }
}

[[nodiscard]] std::uint64_t ReadUnsigned(const Json& value,
                                         const std::uint64_t maximum,
                                         const PuzzleJsonSource& source,
                                         const std::string_view pointer) {
    if (!value.is_number_integer()) {
        ThrowDataError(source.filename, pointer, "expected an integer");
    }

    std::uint64_t result = 0;
    if (value.is_number_unsigned()) {
        result = value.get<std::uint64_t>();
    } else {
        const std::int64_t signedValue = value.get<std::int64_t>();
        if (signedValue < 0) {
            ThrowDataError(source.filename, pointer,
                           "expected a non-negative integer");
        }
        result = static_cast<std::uint64_t>(signedValue);
    }
    if (result > maximum) {
        ThrowDataError(source.filename, pointer,
                       "integer exceeds the supported range");
    }
    return result;
}

[[nodiscard]] std::size_t ReadSize(const Json& value, const std::size_t maximum,
                                   const PuzzleJsonSource& source,
                                   const std::string_view pointer) {
    return static_cast<std::size_t>(ReadUnsigned(value, maximum, source, pointer));
}

[[nodiscard]] float ReadFiniteNumber(const Json& value,
                                     const PuzzleJsonSource& source,
                                     const std::string_view pointer) {
    if (!value.is_number()) {
        ThrowDataError(source.filename, pointer, "expected a number");
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed) ||
        parsed < -static_cast<double>((std::numeric_limits<float>::max)()) ||
        parsed > static_cast<double>((std::numeric_limits<float>::max)())) {
        ThrowDataError(source.filename, pointer, "expected a finite float value");
    }
    return static_cast<float>(parsed);
}

[[nodiscard]] bool ReadBoolean(const Json& value,
                               const PuzzleJsonSource& source,
                               const std::string_view pointer) {
    if (!value.is_boolean()) {
        ThrowDataError(source.filename, pointer, "expected a boolean");
    }
    return value.get<bool>();
}

[[nodiscard]] std::string ReadString(const Json& value,
                                     const PuzzleJsonSource& source,
                                     const std::string_view pointer) {
    if (!value.is_string()) {
        ThrowDataError(source.filename, pointer, "expected a string");
    }
    return value.get<std::string>();
}

[[nodiscard]] std::string ReadId(const Json& value,
                                 const PuzzleJsonSource& source,
                                 const std::string_view pointer) {
    const std::string text = ReadString(value, source, pointer);
    if (text.empty() || text.size() > kMaximumIdentifierLength ||
        text.front() < 'a' || text.front() > 'z' || text.back() == '_') {
        ThrowDataError(source.filename, pointer,
                       "ID must be 1-64 characters of lower_snake_case");
    }
    bool previousUnderscore = false;
    for (const char character : text) {
        const bool lowercase = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        const bool underscore = character == '_';
        if ((!lowercase && !digit && !underscore) ||
            (underscore && previousUnderscore)) {
            ThrowDataError(source.filename, pointer,
                           "ID must be lower_snake_case without repeated underscores");
        }
        previousUnderscore = underscore;
    }
    return text;
}

[[nodiscard]] std::string ReadDisplayText(const Json& value,
                                          const PuzzleJsonSource& source,
                                          const std::string_view pointer) {
    const std::string text = ReadString(value, source, pointer);
    if (text.empty() || text.size() > kMaximumDisplayTextLength) {
        ThrowDataError(source.filename, pointer,
                       "text must contain between 1 and 64 characters");
    }
    bool visible = false;
    for (const unsigned char character : text) {
        if (character < 0x20u || character > 0x7eu) {
            ThrowDataError(source.filename, pointer,
                           "text must contain printable ASCII characters only");
        }
        visible = visible || character != static_cast<unsigned char>(' ');
    }
    if (!visible) {
        ThrowDataError(source.filename, pointer,
                       "text must contain a visible character");
    }
    return text;
}

[[nodiscard]] std::string ReadRelativePath(const Json& value,
                                           const PuzzleJsonSource& source,
                                           const std::string_view pointer) {
    const std::string path = ReadString(value, source, pointer);
    if (path.empty() || path.size() > kMaximumPathLength || path.front() == '/' ||
        path.back() == '/' || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos) {
        ThrowDataError(source.filename, pointer,
                       "path must be a safe, non-empty relative path using '/' separators");
    }

    std::size_t componentStart = 0;
    while (componentStart < path.size()) {
        const std::size_t separator = path.find('/', componentStart);
        const std::size_t componentEnd =
            separator == std::string::npos ? path.size() : separator;
        const std::string_view component{path.data() + componentStart,
                                         componentEnd - componentStart};
        if (component.empty() || component == "." || component == "..") {
            ThrowDataError(source.filename, pointer,
                           "path must not contain empty, '.' or '..' components");
        }
        for (const unsigned char character : component) {
            const bool letter =
                (character >= static_cast<unsigned char>('a') &&
                 character <= static_cast<unsigned char>('z')) ||
                (character >= static_cast<unsigned char>('A') &&
                 character <= static_cast<unsigned char>('Z'));
            const bool digit = character >= static_cast<unsigned char>('0') &&
                               character <= static_cast<unsigned char>('9');
            const bool punctuation =
                character == static_cast<unsigned char>('_') ||
                character == static_cast<unsigned char>('-') ||
                character == static_cast<unsigned char>('.');
            if (!letter && !digit && !punctuation) {
                ThrowDataError(source.filename, pointer,
                               "path components may contain only ASCII letters, digits, '_', '-' and '.'");
            }
        }
        componentStart = componentEnd + 1;
    }
    return path;
}

[[nodiscard]] int HexDigit(const char character) noexcept {
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

[[nodiscard]] Color ReadColor(const Json& value,
                              const PuzzleJsonSource& source,
                              const std::string_view pointer) {
    const std::string text = ReadString(value, source, pointer);
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#') {
        ThrowDataError(source.filename, pointer,
                       "expected #RRGGBB or #RRGGBBAA");
    }

    std::array<int, 4> channels{0, 0, 0, 255};
    const std::size_t channelCount = text.size() == 9 ? 4 : 3;
    for (std::size_t index = 0; index < channelCount; ++index) {
        const int high = HexDigit(text[1 + index * 2]);
        const int low = HexDigit(text[2 + index * 2]);
        if (high < 0 || low < 0) {
            ThrowDataError(source.filename, pointer,
                           "color contains a non-hexadecimal digit");
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

void ValidateSchemaVersion(const Json& root, const PuzzleJsonSource& source) {
    const std::string pointer = "/schema_version";
    if (!root.at("schema_version").is_number_integer() ||
        ReadUnsigned(root.at("schema_version"), 1, source, pointer) != 1) {
        ThrowDataError(source.filename, pointer,
                       "schema_version must be the integer 1");
    }
}

[[nodiscard]] std::string NormalizeName(const std::string_view name) {
    return std::filesystem::path{std::string{name}}.lexically_normal().generic_string();
}

[[nodiscard]] std::string ResolveName(const std::string_view baseFilename,
                                      const std::string_view relativePath) {
    const std::filesystem::path base{std::string{baseFilename}};
    return (base.parent_path() / std::filesystem::path{std::string{relativePath}})
        .lexically_normal()
        .generic_string();
}

struct CatalogManifest final {
    std::string tilesetPath;
    std::string nodeTypesPath;
    std::vector<std::string> levelPaths;
};

[[nodiscard]] CatalogManifest ParseCatalogManifest(const Json& root,
                                                   const PuzzleJsonSource& source) {
    RequireExactKeys(root,
                     {"schema_version", "canvas", "tileset", "node_types", "levels"},
                     source, "");
    ValidateSchemaVersion(root, source);

    const Json& canvas = root.at("canvas");
    RequireExactKeys(canvas, {"width", "height", "tile_size"}, source,
                     "/canvas");
    const std::size_t width = ReadSize(canvas.at("width"), kPuzzleCanvasWidth,
                                       source, "/canvas/width");
    const std::size_t height = ReadSize(canvas.at("height"), kPuzzleCanvasHeight,
                                        source, "/canvas/height");
    const std::size_t tileSize = ReadSize(canvas.at("tile_size"), kPuzzleTileSize,
                                          source, "/canvas/tile_size");
    if (width != kPuzzleCanvasWidth || height != kPuzzleCanvasHeight ||
        tileSize != kPuzzleTileSize) {
        ThrowDataError(source.filename, "/canvas",
                       "canvas must be exactly 1280x720 with tile_size 16");
    }

    CatalogManifest manifest;
    manifest.tilesetPath =
        ReadRelativePath(root.at("tileset"), source, "/tileset");
    manifest.nodeTypesPath =
        ReadRelativePath(root.at("node_types"), source, "/node_types");
    if (!manifest.tilesetPath.ends_with(".json")) {
        ThrowDataError(source.filename, "/tileset",
                       "tileset path must reference a lowercase .json file");
    }
    if (!manifest.nodeTypesPath.ends_with(".json")) {
        ThrowDataError(source.filename, "/node_types",
                       "node_types path must reference a lowercase .json file");
    }
    if (manifest.tilesetPath == manifest.nodeTypesPath) {
        ThrowDataError(source.filename, "/node_types",
                       "tileset and node_types must reference different documents");
    }
    const Json& levels = root.at("levels");
    RequireArray(levels, source, "/levels");
    if (levels.empty() || levels.size() > kMaximumLevels) {
        ThrowDataError(source.filename, "/levels",
                       "levels must contain between 1 and 128 paths");
    }
    std::unordered_set<std::string> uniquePaths;
    uniquePaths.insert(manifest.tilesetPath);
    uniquePaths.insert(manifest.nodeTypesPath);
    manifest.levelPaths.reserve(levels.size());
    for (std::size_t index = 0; index < levels.size(); ++index) {
        const std::string pointer = IndexPointer("/levels", index);
        std::string path = ReadRelativePath(levels[index], source, pointer);
        if (!path.ends_with(".json")) {
            ThrowDataError(source.filename, pointer,
                           "level path must reference a lowercase .json file");
        }
        if (!uniquePaths.insert(path).second) {
            ThrowDataError(source.filename, pointer,
                           "document path is referenced more than once");
        }
        manifest.levelPaths.push_back(std::move(path));
    }
    return manifest;
}

[[nodiscard]] TilesetDefinition ParseTileset(
    const Json& root, const PuzzleJsonSource& source,
    std::unordered_set<TileId>& knownTileIds) {
    RequireExactKeys(root,
                     {"schema_version", "atlas_path", "atlas_columns", "atlas_rows",
                      "tiles"},
                     source, "");
    ValidateSchemaVersion(root, source);

    TilesetDefinition tileset;
    const std::string atlasRelative =
        ReadRelativePath(root.at("atlas_path"), source, "/atlas_path");
    if (!atlasRelative.ends_with(".png")) {
        ThrowDataError(source.filename, "/atlas_path",
                       "atlas_path must reference a lowercase .png file");
    }
    tileset.atlasPath = ResolveName(source.filename, atlasRelative);
    tileset.atlasColumns =
        ReadSize(root.at("atlas_columns"), kMaximumAtlasDimension, source,
                 "/atlas_columns");
    tileset.atlasRows =
        ReadSize(root.at("atlas_rows"), kMaximumAtlasDimension, source,
                 "/atlas_rows");
    if (tileset.atlasColumns == 0 || tileset.atlasRows == 0) {
        ThrowDataError(source.filename, "/atlas_columns",
                       "atlas dimensions must be greater than zero");
    }

    const Json& tiles = root.at("tiles");
    RequireArray(tiles, source, "/tiles");
    if (tiles.empty() || tiles.size() > kMaximumTiles) {
        ThrowDataError(source.filename, "/tiles",
                       "tiles must contain between 1 and 65535 definitions");
    }

    std::unordered_set<std::string> names;
    std::unordered_set<std::size_t> atlasCells;
    tileset.tiles.reserve(tiles.size());
    knownTileIds.reserve(tiles.size());
    for (std::size_t index = 0; index < tiles.size(); ++index) {
        const std::string pointer = IndexPointer("/tiles", index);
        const Json& tileJson = tiles[index];
        RequireExactKeys(tileJson,
                         {"id", "name", "atlas_column", "atlas_row"},
                         source, pointer);

        TileDefinition tile;
        const std::string idPointer = ChildPointer(pointer, "id");
        const std::uint64_t tileId = ReadUnsigned(
            tileJson.at("id"), (std::numeric_limits<TileId>::max)(), source,
            idPointer);
        if (tileId == 0) {
            ThrowDataError(source.filename, idPointer,
                           "tile ID 0 is reserved for empty cells");
        }
        tile.id = static_cast<TileId>(tileId);
        if (!knownTileIds.insert(tile.id).second) {
            ThrowDataError(source.filename, idPointer, "duplicate tile ID");
        }

        const std::string namePointer = ChildPointer(pointer, "name");
        tile.name = ReadId(tileJson.at("name"), source, namePointer);
        if (!names.insert(tile.name).second) {
            ThrowDataError(source.filename, namePointer, "duplicate tile name");
        }

        tile.atlasColumn = ReadSize(tileJson.at("atlas_column"),
                                   tileset.atlasColumns, source,
                                   ChildPointer(pointer, "atlas_column"));
        tile.atlasRow = ReadSize(tileJson.at("atlas_row"), tileset.atlasRows,
                                source, ChildPointer(pointer, "atlas_row"));
        if (tile.atlasColumn >= tileset.atlasColumns ||
            tile.atlasRow >= tileset.atlasRows) {
            ThrowDataError(source.filename, ChildPointer(pointer, "atlas_column"),
                           "atlas cell is outside atlas dimensions");
        }
        const std::size_t atlasIndex =
            tile.atlasRow * tileset.atlasColumns + tile.atlasColumn;
        if (!atlasCells.insert(atlasIndex).second) {
            ThrowDataError(source.filename, ChildPointer(pointer, "atlas_column"),
                           "multiple tiles reference the same atlas cell");
        }
        tileset.tiles.push_back(std::move(tile));
    }
    return tileset;
}

[[nodiscard]] TileId ReadTileId(const Json& value,
                                const std::unordered_set<TileId>& knownTileIds,
                                const PuzzleJsonSource& source,
                                const std::string_view pointer) {
    const std::uint64_t parsed = ReadUnsigned(
        value, (std::numeric_limits<TileId>::max)(), source, pointer);
    const TileId id = static_cast<TileId>(parsed);
    if (id != 0 && !knownTileIds.contains(id)) {
        ThrowDataError(source.filename, pointer,
                       "tile ID is not declared by the tileset");
    }
    return id;
}

[[nodiscard]] std::vector<NodeTypeDefinition> ParseNodeTypes(
    const Json& root, const PuzzleJsonSource& source,
    const std::unordered_set<TileId>& knownTileIds) {
    RequireExactKeys(root, {"schema_version", "node_types"}, source, "");
    ValidateSchemaVersion(root, source);

    const Json& types = root.at("node_types");
    RequireArray(types, source, "/node_types");
    if (types.empty() || types.size() > kMaximumNodeTypes) {
        ThrowDataError(source.filename, "/node_types",
                       "node_types must contain between 1 and 512 definitions");
    }

    std::unordered_set<std::string> typeIds;
    std::vector<NodeTypeDefinition> result;
    result.reserve(types.size());
    for (std::size_t typeIndex = 0; typeIndex < types.size(); ++typeIndex) {
        const std::string pointer = IndexPointer("/node_types", typeIndex);
        const Json& typeJson = types[typeIndex];
        RequireExactKeys(typeJson,
                         {"type_id", "display_name", "stamp", "anchor"},
                         source, pointer);

        NodeTypeDefinition type;
        const std::string typeIdPointer = ChildPointer(pointer, "type_id");
        type.typeId = ReadId(typeJson.at("type_id"), source, typeIdPointer);
        if (!typeIds.insert(type.typeId).second) {
            ThrowDataError(source.filename, typeIdPointer,
                           "duplicate node type ID");
        }
        type.displayName =
            ReadDisplayText(typeJson.at("display_name"), source,
                            ChildPointer(pointer, "display_name"));

        const std::string stampPointer = ChildPointer(pointer, "stamp");
        const Json& stamp = typeJson.at("stamp");
        RequireArray(stamp, source, stampPointer);
        if (stamp.empty() || stamp.size() > kMaximumStampRows) {
            ThrowDataError(source.filename, stampPointer,
                           "stamp must contain between 1 and 45 rows");
        }
        if (!stamp.front().is_array()) {
            ThrowDataError(source.filename, IndexPointer(stampPointer, 0),
                           "stamp rows must be arrays");
        }
        const std::size_t columns = stamp.front().size();
        if (columns == 0 || columns > kMaximumStampColumns ||
            columns * stamp.size() > kMaximumStampCells) {
            ThrowDataError(source.filename, stampPointer,
                           "stamp must be rectangular, non-empty, at most 80x45, and contain at most 3600 cells");
        }

        type.stamp.columns = columns;
        type.stamp.rows = stamp.size();
        type.stamp.cells.reserve(columns * stamp.size());
        type.stamp.occupiedMask.reserve(columns * stamp.size());
        std::size_t occupiedCount = 0;
        for (std::size_t row = 0; row < stamp.size(); ++row) {
            const std::string rowPointer = IndexPointer(stampPointer, row);
            if (!stamp[row].is_array()) {
                ThrowDataError(source.filename, rowPointer,
                               "stamp rows must be arrays");
            }
            if (stamp[row].size() != columns) {
                ThrowDataError(source.filename, rowPointer,
                               "stamp rows must form a rectangle");
            }
            for (std::size_t column = 0; column < columns; ++column) {
                const std::string cellPointer = IndexPointer(rowPointer, column);
                const TileId id =
                    ReadTileId(stamp[row][column], knownTileIds, source, cellPointer);
                type.stamp.cells.push_back(id);
                const std::uint8_t occupied = id == 0 ? 0u : 1u;
                type.stamp.occupiedMask.push_back(occupied);
                occupiedCount += occupied;
            }
        }
        if (occupiedCount == 0) {
            ThrowDataError(source.filename, stampPointer,
                           "stamp must contain at least one occupied tile");
        }

        const std::string anchorPointer = ChildPointer(pointer, "anchor");
        const Json& anchor = typeJson.at("anchor");
        RequireExactKeys(anchor, {"column", "row"}, source, anchorPointer);
        type.stamp.anchor.column =
            ReadSize(anchor.at("column"), columns, source,
                     ChildPointer(anchorPointer, "column"));
        type.stamp.anchor.row =
            ReadSize(anchor.at("row"), type.stamp.rows, source,
                     ChildPointer(anchorPointer, "row"));
        if (type.stamp.anchor.column >= columns ||
            type.stamp.anchor.row >= type.stamp.rows) {
            ThrowDataError(source.filename, anchorPointer,
                           "anchor must be inside the stamp");
        }
        if (!type.stamp.IsOccupied(type.stamp.anchor.column,
                                   type.stamp.anchor.row)) {
            ThrowDataError(source.filename, anchorPointer,
                           "anchor must select an occupied stamp cell");
        }
        result.push_back(std::move(type));
    }
    return result;
}

[[nodiscard]] TileGrid ParseFixedGrid(
    const Json& matrix, const PuzzleJsonSource& source,
    const std::string_view pointer,
    const std::unordered_set<TileId>& knownTileIds) {
    RequireArray(matrix, source, pointer);
    if (matrix.size() != kPuzzleGridRows) {
        ThrowDataError(source.filename, pointer,
                       "tile layer must contain exactly 45 rows");
    }

    TileGrid result;
    result.columns = kPuzzleGridColumns;
    result.rows = kPuzzleGridRows;
    result.cells.reserve(kPuzzleGridColumns * kPuzzleGridRows);
    for (std::size_t row = 0; row < matrix.size(); ++row) {
        const std::string rowPointer = IndexPointer(pointer, row);
        if (!matrix[row].is_array()) {
            ThrowDataError(source.filename, rowPointer,
                           "tile layer rows must be arrays");
        }
        if (matrix[row].size() != kPuzzleGridColumns) {
            ThrowDataError(source.filename, rowPointer,
                           "tile layer rows must contain exactly 80 cells");
        }
        for (std::size_t column = 0; column < matrix[row].size(); ++column) {
            const std::string cellPointer = IndexPointer(rowPointer, column);
            result.cells.push_back(ReadTileId(matrix[row][column], knownTileIds,
                                              source, cellPointer));
        }
    }
    return result;
}

[[nodiscard]] bool IsConnectionBlockedByTiles(const Vec2 start, const Vec2 end,
                                              const TileGrid& obstacles,
                                              const float clearance) noexcept {
    const float expandedSize =
        static_cast<float>(kPuzzleTileSize) + clearance * 2.0f;
    for (std::size_t row = 0; row < obstacles.rows; ++row) {
        for (std::size_t column = 0; column < obstacles.columns; ++column) {
            if (obstacles.cells[row * obstacles.columns + column] == 0) {
                continue;
            }
            const Vec2 center{
                (static_cast<float>(column) + 0.5f) *
                    static_cast<float>(kPuzzleTileSize),
                (static_cast<float>(row) + 0.5f) *
                    static_cast<float>(kPuzzleTileSize),
            };
            if (SegmentIntersectsRectangle(start, end, center, expandedSize,
                                           expandedSize)) {
                return true;
            }
        }
    }
    return false;
}

[[nodiscard]] float ReadNumberInRange(const Json& value, const float minimum,
                                      const float maximum,
                                      const PuzzleJsonSource& source,
                                      const std::string_view pointer,
                                      const bool includeMinimum = true,
                                      const bool includeMaximum = true) {
    const float parsed = ReadFiniteNumber(value, source, pointer);
    const bool below = includeMinimum ? parsed < minimum : parsed <= minimum;
    const bool above = includeMaximum ? parsed > maximum : parsed >= maximum;
    if (below || above) {
        ThrowDataError(source.filename, pointer,
                       "number is outside the supported range");
    }
    return parsed;
}

struct ParsedNodeMetadata final {
    std::string pointer;
};

[[nodiscard]] PuzzleDefinition ParseLevel(
    const Json& root, const PuzzleJsonSource& source,
    const std::unordered_set<TileId>& knownTileIds,
    const std::vector<NodeTypeDefinition>& nodeTypes,
    const std::unordered_map<std::string, std::size_t>& nodeTypeIndices) {
    RequireExactKeys(root,
                     {"schema_version", "id", "title", "background_color", "rules",
                      "layers", "nodes", "connections"},
                     source, "");
    ValidateSchemaVersion(root, source);

    PuzzleDefinition puzzle;
    puzzle.id = ReadId(root.at("id"), source, "/id");
    puzzle.title = ReadDisplayText(root.at("title"), source, "/title");
    puzzle.backgroundColor =
        ReadColor(root.at("background_color"), source, "/background_color");

    const Json& rules = root.at("rules");
    RequireExactKeys(rules,
                     {"total_length", "minimum_slack_ratio",
                      "show_target_connections", "vessel"},
                     source, "/rules");
    puzzle.totalLength = ReadNumberInRange(
        rules.at("total_length"), 0.0f, kMaximumTotalLength, source,
        "/rules/total_length", false, true);
    puzzle.minimumSlackRatio = ReadNumberInRange(
        rules.at("minimum_slack_ratio"), 1.0f, kMaximumSlackRatio, source,
        "/rules/minimum_slack_ratio");
    puzzle.showTargetConnections =
        ReadBoolean(rules.at("show_target_connections"), source,
                    "/rules/show_target_connections");

    const Json& vessel = rules.at("vessel");
    RequireExactKeys(vessel,
                     {"color", "base_width", "tip_width", "width_variation"},
                     source, "/rules/vessel");
    puzzle.vesselColor =
        ReadColor(vessel.at("color"), source, "/rules/vessel/color");
    puzzle.baseWidth = ReadNumberInRange(
        vessel.at("base_width"), 0.0f, kMaximumVesselWidth, source,
        "/rules/vessel/base_width", false, true);
    puzzle.tipWidth = ReadNumberInRange(
        vessel.at("tip_width"), 0.0f, kMaximumVesselWidth, source,
        "/rules/vessel/tip_width", false, true);
    if (puzzle.baseWidth < puzzle.tipWidth) {
        ThrowDataError(source.filename, "/rules/vessel/base_width",
                       "base_width must be greater than or equal to tip_width");
    }
    puzzle.widthVariation = ReadNumberInRange(
        vessel.at("width_variation"), 0.0f, 1.0f, source,
        "/rules/vessel/width_variation", true, false);

    const Json& layers = root.at("layers");
    RequireExactKeys(layers, {"background", "obstacles"}, source, "/layers");
    puzzle.backgroundTiles = ParseFixedGrid(
        layers.at("background"), source, "/layers/background", knownTileIds);
    puzzle.obstacleTiles = ParseFixedGrid(
        layers.at("obstacles"), source, "/layers/obstacles", knownTileIds);

    const Json& nodes = root.at("nodes");
    RequireArray(nodes, source, "/nodes");
    if (nodes.empty() || nodes.size() > kMaximumNodes) {
        ThrowDataError(source.filename, "/nodes",
                       "nodes must contain between 1 and 512 definitions");
    }

    std::unordered_map<std::string, std::size_t> nodeIndices;
    nodeIndices.reserve(nodes.size());
    std::vector<ParsedNodeMetadata> nodeMetadata;
    nodeMetadata.reserve(nodes.size());
    std::vector<std::size_t> occupiedOwners(
        kPuzzleGridColumns * kPuzzleGridRows,
        (std::numeric_limits<std::size_t>::max)());
    puzzle.nodes.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const std::string pointer = IndexPointer("/nodes", index);
        const Json& nodeJson = nodes[index];
        RequireExactKeys(nodeJson,
                         {"id", "type_id", "column", "row", "is_root", "is_goal",
                          "max_incoming", "max_outgoing"},
                         source, pointer);

        NodeDefinition node;
        const std::string idPointer = ChildPointer(pointer, "id");
        node.id = ReadId(nodeJson.at("id"), source, idPointer);
        if (!nodeIndices.emplace(node.id, index).second) {
            ThrowDataError(source.filename, idPointer, "duplicate node ID");
        }

        const std::string typePointer = ChildPointer(pointer, "type_id");
        node.typeId = ReadId(nodeJson.at("type_id"), source, typePointer);
        const auto typeFound = nodeTypeIndices.find(node.typeId);
        if (typeFound == nodeTypeIndices.end()) {
            ThrowDataError(source.filename, typePointer,
                           "node type ID is not declared by node_types.json");
        }
        node.typeIndex = typeFound->second;
        node.stamp = nodeTypes[node.typeIndex].stamp;
        node.displayName = nodeTypes[node.typeIndex].displayName;

        node.origin.column =
            ReadSize(nodeJson.at("column"), kPuzzleGridColumns - 1, source,
                     ChildPointer(pointer, "column"));
        node.origin.row =
            ReadSize(nodeJson.at("row"), kPuzzleGridRows - 1, source,
                     ChildPointer(pointer, "row"));
        node.isRoot = ReadBoolean(nodeJson.at("is_root"), source,
                                  ChildPointer(pointer, "is_root"));
        node.isGoal = ReadBoolean(nodeJson.at("is_goal"), source,
                                  ChildPointer(pointer, "is_goal"));
        node.maxIncoming =
            ReadSize(nodeJson.at("max_incoming"), kMaximumNodeCapacity, source,
                     ChildPointer(pointer, "max_incoming"));
        node.maxOutgoing =
            ReadSize(nodeJson.at("max_outgoing"), kMaximumNodeCapacity, source,
                     ChildPointer(pointer, "max_outgoing"));

        std::size_t minimumColumn = kPuzzleGridColumns;
        std::size_t minimumRow = kPuzzleGridRows;
        std::size_t maximumColumn = 0;
        std::size_t maximumRow = 0;
        for (std::size_t localRow = 0; localRow < node.stamp.rows; ++localRow) {
            for (std::size_t localColumn = 0; localColumn < node.stamp.columns;
                 ++localColumn) {
                if (!node.stamp.IsOccupied(localColumn, localRow)) {
                    continue;
                }
                const std::size_t column = node.origin.column + localColumn;
                const std::size_t row = node.origin.row + localRow;
                if (column >= kPuzzleGridColumns || row >= kPuzzleGridRows) {
                    ThrowDataError(source.filename, ChildPointer(pointer, "column"),
                                   "occupied node stamp cells must fit inside the 80x45 canvas");
                }
                const std::size_t cellIndex = row * kPuzzleGridColumns + column;
                if (puzzle.obstacleTiles.cells[cellIndex] != 0) {
                    ThrowDataError(source.filename, pointer,
                                   "node stamp overlaps a solid obstacle tile");
                }
                if (occupiedOwners[cellIndex] !=
                    (std::numeric_limits<std::size_t>::max)()) {
                    const std::size_t otherIndex = occupiedOwners[cellIndex];
                    ThrowDataError(source.filename, pointer,
                                   "node stamp overlaps node '" +
                                       puzzle.nodes[otherIndex].id + "'");
                }
                occupiedOwners[cellIndex] = index;
                minimumColumn = (std::min)(minimumColumn, column);
                minimumRow = (std::min)(minimumRow, row);
                maximumColumn = (std::max)(maximumColumn, column);
                maximumRow = (std::max)(maximumRow, row);
            }
        }
        node.bounds = {
            minimumColumn,
            minimumRow,
            maximumColumn - minimumColumn + 1,
            maximumRow - minimumRow + 1,
        };
        const std::size_t anchorColumn =
            node.origin.column + node.stamp.anchor.column;
        const std::size_t anchorRow = node.origin.row + node.stamp.anchor.row;
        node.anchorPosition = {
            (static_cast<float>(anchorColumn) + 0.5f) *
                static_cast<float>(kPuzzleTileSize),
            (static_cast<float>(anchorRow) + 0.5f) *
                static_cast<float>(kPuzzleTileSize),
        };
        if (node.isRoot) {
            puzzle.rootNodeIndices.push_back(index);
        }
        if (node.isGoal) {
            puzzle.goalNodeIndices.push_back(index);
        }
        puzzle.nodes.push_back(std::move(node));
        nodeMetadata.push_back({pointer});
    }
    if (puzzle.rootNodeIndices.empty()) {
        ThrowDataError(source.filename, "/nodes",
                       "level must contain at least one root node");
    }
    if (puzzle.goalNodeIndices.empty()) {
        ThrowDataError(source.filename, "/nodes",
                       "level must contain at least one goal node");
    }
    const Json& connections = root.at("connections");
    RequireArray(connections, source, "/connections");
    if (connections.size() > kMaximumConnections) {
        ThrowDataError(source.filename, "/connections",
                       "connections must not contain more than 4096 definitions");
    }
    std::unordered_set<std::string> connectionIds;
    std::unordered_set<std::string> directedPairs;
    std::vector<std::vector<std::size_t>> adjacency(puzzle.nodes.size());
    std::vector<std::size_t> indegrees(puzzle.nodes.size(), 0);
    puzzle.connections.reserve(connections.size());
    for (std::size_t index = 0; index < connections.size(); ++index) {
        const std::string pointer = IndexPointer("/connections", index);
        const Json& connectionJson = connections[index];
        RequireExactKeys(
            connectionJson,
            {"id", "from", "to", "point_count", "thickness_scale",
             "follow_delay_seconds", "initial_direction_degrees"},
            source, pointer);

        ConnectionDefinition connection;
        const std::string idPointer = ChildPointer(pointer, "id");
        connection.id = ReadId(connectionJson.at("id"), source, idPointer);
        if (!connectionIds.insert(connection.id).second) {
            ThrowDataError(source.filename, idPointer,
                           "duplicate connection ID");
        }
        const std::string fromPointer = ChildPointer(pointer, "from");
        connection.fromNodeId =
            ReadId(connectionJson.at("from"), source, fromPointer);
        const auto fromFound = nodeIndices.find(connection.fromNodeId);
        if (fromFound == nodeIndices.end()) {
            ThrowDataError(source.filename, fromPointer,
                           "connection source node does not exist");
        }
        connection.fromNodeIndex = fromFound->second;

        const std::string toPointer = ChildPointer(pointer, "to");
        connection.toNodeId = ReadId(connectionJson.at("to"), source, toPointer);
        const auto toFound = nodeIndices.find(connection.toNodeId);
        if (toFound == nodeIndices.end()) {
            ThrowDataError(source.filename, toPointer,
                           "connection target node does not exist");
        }
        connection.toNodeIndex = toFound->second;
        if (connection.fromNodeIndex == connection.toNodeIndex) {
            ThrowDataError(source.filename, toPointer,
                           "self-connections are not allowed");
        }
        std::string pairKey = connection.fromNodeId;
        pairKey.push_back('\0');
        pairKey += connection.toNodeId;
        if (!directedPairs.insert(std::move(pairKey)).second) {
            ThrowDataError(source.filename, toPointer,
                           "duplicate directed connection");
        }

        connection.pointCount =
            ReadSize(connectionJson.at("point_count"), 12, source,
                     ChildPointer(pointer, "point_count"));
        if (connection.pointCount < 8) {
            ThrowDataError(source.filename, ChildPointer(pointer, "point_count"),
                           "point_count must be in the range [8, 12]");
        }
        connection.thicknessScale = ReadNumberInRange(
            connectionJson.at("thickness_scale"), 0.0f,
            kMaximumThicknessScale, source,
            ChildPointer(pointer, "thickness_scale"), false, true);
        connection.followDelaySeconds = ReadNumberInRange(
            connectionJson.at("follow_delay_seconds"), 0.0f,
            kMaximumFollowDelaySeconds, source,
            ChildPointer(pointer, "follow_delay_seconds"));
        connection.initialDirectionDegrees = ReadNumberInRange(
            connectionJson.at("initial_direction_degrees"),
            -kMaximumDirectionDegrees, kMaximumDirectionDegrees, source,
            ChildPointer(pointer, "initial_direction_degrees"));

        const NodeDefinition& fromNode = puzzle.nodes[connection.fromNodeIndex];
        const NodeDefinition& toNode = puzzle.nodes[connection.toNodeIndex];
        if (fromNode.maxOutgoing == 0) {
            ThrowDataError(source.filename, fromPointer,
                           "connection source has zero outgoing capacity");
        }
        if (toNode.maxIncoming == 0) {
            ThrowDataError(source.filename, toPointer,
                           "connection target has zero incoming capacity");
        }
        if (fromNode.anchorPosition == toNode.anchorPosition) {
            ThrowDataError(source.filename, toPointer,
                           "connection endpoints must have different anchors");
        }
        const float clearance =
            (std::max)(puzzle.baseWidth, puzzle.tipWidth) *
                connection.thicknessScale * 0.5f +
            kObstacleClearancePadding;
        if (IsConnectionBlockedByTiles(fromNode.anchorPosition,
                                       toNode.anchorPosition,
                                       puzzle.obstacleTiles, clearance)) {
            ThrowDataError(source.filename, pointer,
                           "connection is blocked by a solid obstacle tile");
        }

        adjacency[connection.fromNodeIndex].push_back(connection.toNodeIndex);
        ++indegrees[connection.toNodeIndex];
        puzzle.connections.push_back(std::move(connection));
    }

    std::vector<std::size_t> remainingIndegrees = indegrees;
    std::queue<std::size_t> ready;
    for (std::size_t index = 0; index < remainingIndegrees.size(); ++index) {
        if (remainingIndegrees[index] == 0) {
            ready.push(index);
        }
    }
    std::size_t visitedByTopologicalSort = 0;
    while (!ready.empty()) {
        const std::size_t node = ready.front();
        ready.pop();
        ++visitedByTopologicalSort;
        for (const std::size_t target : adjacency[node]) {
            --remainingIndegrees[target];
            if (remainingIndegrees[target] == 0) {
                ready.push(target);
            }
        }
    }
    if (visitedByTopologicalSort != puzzle.nodes.size()) {
        ThrowDataError(source.filename, "/connections",
                       "candidate connection graph must be acyclic");
    }

    std::vector<bool> reachable(puzzle.nodes.size(), false);
    std::queue<std::size_t> pending;
    for (const std::size_t rootIndex : puzzle.rootNodeIndices) {
        if (!reachable[rootIndex]) {
            reachable[rootIndex] = true;
            pending.push(rootIndex);
        }
    }
    while (!pending.empty()) {
        const std::size_t node = pending.front();
        pending.pop();
        for (const std::size_t target : adjacency[node]) {
            if (!reachable[target]) {
                reachable[target] = true;
                pending.push(target);
            }
        }
    }
    for (std::size_t index = 0; index < reachable.size(); ++index) {
        if (!reachable[index]) {
            ThrowDataError(source.filename, nodeMetadata[index].pointer,
                           "node is not reachable from any root");
        }
    }
    for (const std::size_t goalIndex : puzzle.goalNodeIndices) {
        if (!reachable[goalIndex]) {
            ThrowDataError(source.filename, nodeMetadata[goalIndex].pointer,
                           "goal is not reachable from any root");
        }
    }
    return puzzle;
}

[[nodiscard]] const PuzzleJsonSource& RequireSource(
    const PuzzleJsonSource& source, const std::string_view expectedName,
    const PuzzleJsonSource& catalogSource, const std::string_view pointer,
    const std::string_view label) {
    if (NormalizeName(source.filename) != NormalizeName(expectedName)) {
        ThrowDataError(catalogSource.filename, pointer,
                       std::string{"source bundle is missing referenced "} +
                           std::string{label} + " document '" +
                           std::string{expectedName} + "'");
    }
    return source;
}

[[nodiscard]] PuzzleCatalog BuildCatalog(const PuzzleJsonSources& sources) {
    if (sources.catalog.filename.empty()) {
        ThrowDataError("<memory>", "/", "catalog source filename must not be empty");
    }
    const Json catalogRoot = ParseJsonDocument(sources.catalog);
    const CatalogManifest manifest =
        ParseCatalogManifest(catalogRoot, sources.catalog);

    const std::string expectedTilesetName =
        ResolveName(sources.catalog.filename, manifest.tilesetPath);
    const std::string expectedNodeTypesName =
        ResolveName(sources.catalog.filename, manifest.nodeTypesPath);
    const PuzzleJsonSource& tilesetSource =
        RequireSource(sources.tileset, expectedTilesetName, sources.catalog,
                      "/tileset", "tileset");
    const PuzzleJsonSource& nodeTypesSource =
        RequireSource(sources.nodeTypes, expectedNodeTypesName, sources.catalog,
                      "/node_types", "node-types");

    std::unordered_map<std::string, const PuzzleJsonSource*> levelSources;
    levelSources.reserve(sources.levels.size());
    for (const PuzzleJsonSource& source : sources.levels) {
        if (source.filename.empty()) {
            ThrowDataError(sources.catalog.filename, "/levels",
                           "level source filename must not be empty");
        }
        const std::string normalized = NormalizeName(source.filename);
        if (!levelSources.emplace(normalized, &source).second) {
            ThrowDataError(source.filename, "/",
                           "duplicate level source filename in source bundle");
        }
    }
    if (levelSources.size() != manifest.levelPaths.size()) {
        ThrowDataError(sources.catalog.filename, "/levels",
                       "source bundle level count does not match catalog levels");
    }

    std::unordered_set<TileId> knownTileIds;
    TilesetDefinition tileset =
        ParseTileset(ParseJsonDocument(tilesetSource), tilesetSource, knownTileIds);
    std::vector<NodeTypeDefinition> nodeTypes = ParseNodeTypes(
        ParseJsonDocument(nodeTypesSource), nodeTypesSource, knownTileIds);
    std::unordered_map<std::string, std::size_t> nodeTypeIndices;
    nodeTypeIndices.reserve(nodeTypes.size());
    for (std::size_t index = 0; index < nodeTypes.size(); ++index) {
        nodeTypeIndices.emplace(nodeTypes[index].typeId, index);
    }

    std::unordered_set<std::string> puzzleIds;
    std::vector<PuzzleDefinition> puzzles;
    puzzles.reserve(manifest.levelPaths.size());
    for (std::size_t index = 0; index < manifest.levelPaths.size(); ++index) {
        const std::string expectedName =
            ResolveName(sources.catalog.filename, manifest.levelPaths[index]);
        const auto found = levelSources.find(NormalizeName(expectedName));
        if (found == levelSources.end()) {
            ThrowDataError(sources.catalog.filename, IndexPointer("/levels", index),
                           "source bundle is missing referenced level document '" +
                               expectedName + "'");
        }
        const PuzzleJsonSource& levelSource = *found->second;
        PuzzleDefinition puzzle = ParseLevel(
            ParseJsonDocument(levelSource), levelSource, knownTileIds, nodeTypes,
            nodeTypeIndices);
        if (!puzzleIds.insert(puzzle.id).second) {
            ThrowDataError(levelSource.filename, "/id", "duplicate level ID");
        }
        puzzles.push_back(std::move(puzzle));
    }
    return PuzzleCatalog{std::move(tileset), std::move(nodeTypes),
                         std::move(puzzles)};
}

[[nodiscard]] std::string ReadFile(const std::filesystem::path& path,
                                   const std::string_view diagnosticName) {
    std::error_code statusError;
    if (!std::filesystem::is_regular_file(path, statusError)) {
        std::string detail = "referenced JSON document is not a regular file";
        if (statusError) {
            detail += ": " + statusError.message();
        }
        ThrowDataError(diagnosticName, "/", detail);
    }
    std::error_code sizeError;
    const std::uintmax_t size = std::filesystem::file_size(path, sizeError);
    if (sizeError) {
        ThrowDataError(diagnosticName, "/",
                       "failed to determine JSON document size: " +
                           sizeError.message());
    }
    if (size > kMaximumJsonFileBytes) {
        ThrowDataError(diagnosticName, "/",
                       "JSON document exceeds the 16 MiB limit");
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        ThrowDataError(diagnosticName, "/", "failed to open JSON document");
    }
    std::string contents{std::istreambuf_iterator<char>{stream},
                         std::istreambuf_iterator<char>{}};
    if (stream.bad()) {
        ThrowDataError(diagnosticName, "/", "failed to read JSON document");
    }
    return contents;
}

[[nodiscard]] bool IsRegularFile(const std::filesystem::path& path,
                                 std::string& detail) {
    std::error_code error;
    const bool regular = std::filesystem::is_regular_file(path, error);
    if (!regular) {
        detail = "referenced resource is not a regular file";
        if (error) {
            detail += ": " + error.message();
        }
    }
    return regular;
}

[[nodiscard]] bool IsInsideResourceRoot(const std::filesystem::path& resourceRoot,
                                        const std::filesystem::path& asset,
                                        std::string& detail) {
    std::error_code rootError;
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(resourceRoot, rootError);
    if (rootError) {
        detail = "failed to resolve resource root: " + rootError.message();
        return false;
    }
    std::error_code assetError;
    const std::filesystem::path canonicalAsset =
        std::filesystem::canonical(asset, assetError);
    if (assetError) {
        detail = "failed to resolve referenced resource: " + assetError.message();
        return false;
    }
    const std::filesystem::path relative =
        canonicalAsset.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute()) {
        detail = "referenced resource resolves outside the resource root";
        return false;
    }
    for (const std::filesystem::path& component : relative) {
        if (component == "..") {
            detail = "referenced resource resolves outside the resource root";
            return false;
        }
    }
    return true;
}

void RequireRegularFileInsideResourceRoot(
    const std::filesystem::path& resourceRoot,
    const std::filesystem::path& asset,
    const std::string_view diagnosticName,
    const std::string_view pointer) {
    std::string detail;
    if (!IsRegularFile(asset, detail) ||
        !IsInsideResourceRoot(resourceRoot, asset, detail)) {
        ThrowDataError(diagnosticName, pointer, detail);
    }
}

[[nodiscard]] std::string ReadResourceFile(
    const std::filesystem::path& resourceRoot,
    const std::filesystem::path& asset,
    const std::string_view diagnosticName) {
    RequireRegularFileInsideResourceRoot(resourceRoot, asset, diagnosticName, "/");
    return ReadFile(asset, diagnosticName);
}

} // namespace

bool PuzzleCatalogLoader::Load(const std::string& catalogPath,
                               const std::string& resourceRoot,
                               PuzzleCatalog& catalog, std::string& error) {
    try {
        if (resourceRoot.empty()) {
            ThrowDataError("<configuration>", "/resource_root",
                           "resource root must not be empty");
        }
        const PuzzleJsonSource configuration{"<configuration>", {}};
        const Json catalogPathJson = catalogPath;
        const std::string safeCatalogPath = ReadRelativePath(
            catalogPathJson, configuration, "/catalog_path");
        if (!safeCatalogPath.ends_with(".json")) {
            ThrowDataError("<configuration>", "/catalog_path",
                           "catalog path must reference a lowercase .json file");
        }
        std::error_code rootError;
        const std::filesystem::path root{resourceRoot};
        if (!std::filesystem::is_directory(root, rootError)) {
            std::string detail = "resource root is not a directory";
            if (rootError) {
                detail += ": " + rootError.message();
            }
            ThrowDataError("<configuration>", "/resource_root", detail);
        }

        const std::string catalogName = NormalizeName(safeCatalogPath);
        std::string catalogText = ReadResourceFile(
            root, root / std::filesystem::path{catalogName}, catalogName);
        const PuzzleJsonSource catalogSource{catalogName, catalogText};
        const Json catalogJson = ParseJsonDocument(catalogSource);
        const CatalogManifest manifest =
            ParseCatalogManifest(catalogJson, catalogSource);

        const std::string tilesetName =
            ResolveName(catalogName, manifest.tilesetPath);
        const std::string nodeTypesName =
            ResolveName(catalogName, manifest.nodeTypesPath);
        std::string tilesetText = ReadResourceFile(
            root, root / std::filesystem::path{tilesetName}, tilesetName);
        std::string nodeTypesText = ReadResourceFile(
            root, root / std::filesystem::path{nodeTypesName}, nodeTypesName);

        std::vector<std::string> levelNames;
        std::vector<std::string> levelTexts;
        levelNames.reserve(manifest.levelPaths.size());
        levelTexts.reserve(manifest.levelPaths.size());
        for (const std::string& relative : manifest.levelPaths) {
            levelNames.push_back(ResolveName(catalogName, relative));
            levelTexts.push_back(ReadResourceFile(
                root, root / std::filesystem::path{levelNames.back()},
                levelNames.back()));
        }

        PuzzleJsonSources sources{
            {catalogName, catalogText},
            {tilesetName, tilesetText},
            {nodeTypesName, nodeTypesText},
            {},
        };
        sources.levels.reserve(levelNames.size());
        for (std::size_t index = 0; index < levelNames.size(); ++index) {
            sources.levels.push_back({levelNames[index], levelTexts[index]});
        }

        PuzzleCatalog built;
        std::string parseError;
        if (!Parse(sources, built, parseError)) {
            error = std::move(parseError);
            return false;
        }
        const std::filesystem::path atlasPath =
            root / std::filesystem::path{built.GetTileset().atlasPath};
        RequireRegularFileInsideResourceRoot(
            root, atlasPath, tilesetName, "/atlas_path");

        catalog = std::move(built);
        error.clear();
        return true;
    } catch (const PuzzleDataError& exception) {
        error = exception.what();
        return false;
    } catch (const std::exception& exception) {
        error = std::string{"<configuration>#/: unexpected loader failure: "} +
                exception.what();
        return false;
    }
}

bool PuzzleCatalogLoader::Parse(const PuzzleJsonSources& sources,
                                PuzzleCatalog& catalog, std::string& error) {
    try {
        PuzzleCatalog built = BuildCatalog(sources);
        catalog = std::move(built);
        error.clear();
        return true;
    } catch (const PuzzleDataError& exception) {
        error = exception.what();
        return false;
    } catch (const std::exception& exception) {
        const std::string filename = sources.catalog.filename.empty()
                                         ? std::string{"<memory>"}
                                         : std::string{sources.catalog.filename};
        error = filename + "#/: unexpected loader failure: " + exception.what();
        return false;
    }
}

} // namespace object_connect
