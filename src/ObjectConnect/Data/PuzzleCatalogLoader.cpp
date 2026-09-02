#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"

#include "ObjectConnect/Data/Csv.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
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
constexpr std::string_view kNodePresetsCatalogName{"nodes.csv"};
constexpr std::size_t kMaximumIdentifierLength = 64;
constexpr std::size_t kMaximumDisplayTextLength = 128;

constexpr std::array<std::string_view, 11> kLevelsHeader{
    "level_id",
    "level_name",
    "map_path",
    "next_level_id",
    "total_length",
    "minimum_slack_ratio",
    "background_color",
    "vessel_color",
    "base_width",
    "tip_width",
    "width_variation",
};

constexpr std::array<std::string_view, 9> kNodePresetsHeader{
    "preset_id",
    "node_type",
    "texture_path",
    "width_tiles",
    "height_tiles",
    "display_name",
    "max_incoming",
    "max_outgoing",
    "max_outgoing_length",
};

constexpr std::array<std::string_view, 12> kMapHeader{
    "instance_id",
    "source_preset_id",
    "node_type",
    "texture_path",
    "width_tiles",
    "height_tiles",
    "display_name",
    "tile_x",
    "tile_y",
    "max_incoming",
    "max_outgoing",
    "max_outgoing_length",
};

constexpr Color kDefaultBackgroundColor{
    18.0f / 255.0f, 11.0f / 255.0f, 16.0f / 255.0f, 1.0f};
constexpr Color kDefaultVesselColor{
    134.0f / 255.0f, 27.0f / 255.0f, 43.0f / 255.0f, 1.0f};

class PuzzleDataError final : public std::runtime_error {
public:
    explicit PuzzleDataError(const std::string& message)
        : std::runtime_error(message) {}
};

template <std::size_t Size>
[[nodiscard]] std::string JoinHeader(
    const std::array<std::string_view, Size>& header) {
    std::string joined;
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (index != 0) {
            joined.push_back(',');
        }
        joined += header[index];
    }
    return joined;
}

template <std::size_t Size>
void ValidateHeader(const data::CsvDocument& document,
                    const std::array<std::string_view, Size>& expected,
                    const std::string_view catalogName) {
    std::size_t mismatch = 0;
    const std::size_t sharedSize =
        (std::min)(document.header.size(), expected.size());
    while (mismatch < sharedSize && document.header[mismatch] == expected[mismatch]) {
        ++mismatch;
    }
    if (mismatch == sharedSize && document.header.size() == expected.size()) {
        return;
    }
    throw PuzzleDataError(
        std::string{catalogName} + " line 1, column " +
        std::to_string(mismatch + 1) + ": header must be exactly: " +
        JoinHeader(expected));
}

template <std::size_t Size>
[[noreturn]] void ThrowFieldError(
    const std::string_view catalogName,
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
    throw PuzzleDataError(
        std::string{catalogName} + " line " + std::to_string(lineNumber) +
        ", column " + std::to_string(column) + ", field '" +
        std::string{fieldName} + "': " + detail);
}

[[nodiscard]] std::string ParseDefinitionId(
    const std::string& text,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty() || text.size() > kMaximumIdentifierLength ||
        text.front() < 'a' || text.front() > 'z' || text.back() == '_') {
        ThrowFieldError(
            catalogName, lineNumber, fieldName, header,
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
            ThrowFieldError(
                catalogName, lineNumber, fieldName, header,
                "ID must use lower_snake_case and contain no repeated underscores");
        }
        previousUnderscore = underscore;
    }
    return text;
}

[[nodiscard]] std::string ParseOptionalDefinitionId(
    const std::string& text,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty()) {
        return {};
    }
    return ParseDefinitionId(text, catalogName, lineNumber, fieldName, header);
}

[[nodiscard]] std::string ParseDisplayText(
    const std::string& text,
    const bool mayBeEmpty,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty() && mayBeEmpty) {
        return {};
    }
    if (text.empty() || text.size() > kMaximumDisplayTextLength) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        mayBeEmpty
                            ? "text must be empty or contain at most 128 bytes"
                            : "text must contain between 1 and 128 bytes");
    }

    bool hasVisibleByte = false;
    for (const unsigned char byte : text) {
        if (byte < 0x20u || byte == 0x7Fu) {
            ThrowFieldError(catalogName, lineNumber, fieldName, header,
                            "text must not contain control characters");
        }
        hasVisibleByte = hasVisibleByte || byte != static_cast<unsigned char>(' ');
    }
    if (!hasVisibleByte) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "text must contain a visible character");
    }
    return text;
}

[[nodiscard]] std::uint32_t ParseUnsigned(
    const std::string& text,
    const bool mayBeEmpty,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty() && mayBeEmpty) {
        return 0;
    }

    std::uint32_t value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value, 10);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "expected a non-negative 32-bit integer");
    }
    return value;
}

[[nodiscard]] float ParseNonNegativeFloat(
    const std::string& text,
    const std::optional<float> defaultValue,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty() && defaultValue.has_value()) {
        return *defaultValue;
    }

    float value = 0.0f;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const std::from_chars_result result =
        std::from_chars(begin, end, value, std::chars_format::general);
    if (text.empty() || result.ec != std::errc{} || result.ptr != end ||
        !std::isfinite(value) || value < 0.0f) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "expected a finite non-negative decimal number");
    }
    return value;
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

[[nodiscard]] Color ParseColor(
    const std::string& text,
    const Color defaultValue,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty()) {
        return defaultValue;
    }
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
                            "expected hexadecimal color digits");
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

[[nodiscard]] NodeType ParseNodeType(
    const std::string& text,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const auto& header) {
    if (text == "root") {
        return NodeType::Root;
    }
    if (text == "follow") {
        return NodeType::Follow;
    }
    if (text == "end") {
        return NodeType::End;
    }
    if (text == "dead") {
        return NodeType::Dead;
    }
    ThrowFieldError(catalogName, lineNumber, "node_type", header,
                    "expected exactly 'root', 'follow', 'end', or 'dead'");
}

[[nodiscard]] std::string ValidateRelativePath(
    const std::string& text,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find('\r') != std::string::npos ||
        text.find('\n') != std::string::npos ||
        text.find(':') != std::string::npos) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "path must be non-empty, single-line, and relative");
    }

    const std::filesystem::path relative{text};
    if (relative.has_root_path() || relative.has_root_name() ||
        relative.filename().empty()) {
        ThrowFieldError(catalogName, lineNumber, fieldName, header,
                        "path must be relative to the resource root");
    }
    for (const std::filesystem::path& component : relative) {
        if (component.empty() || component == "." || component == "..") {
            ThrowFieldError(catalogName, lineNumber, fieldName, header,
                            "path must remain within the resource root");
        }
    }
    return relative.generic_string();
}

[[nodiscard]] std::string ValidateOptionalRelativePath(
    const std::string& text,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const auto& header) {
    if (text.empty()) {
        return {};
    }
    return ValidateRelativePath(text, catalogName, lineNumber, fieldName, header);
}

template <std::size_t Size>
void RequireResourceFile(
    const std::filesystem::path& resourceRoot,
    const std::string& relativePath,
    const std::string_view catalogName,
    const std::size_t lineNumber,
    const std::string_view fieldName,
    const std::array<std::string_view, Size>& header) {
    const std::filesystem::path fullPath = resourceRoot / relativePath;
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(fullPath, fileError)) {
        std::string detail =
            "referenced resource does not exist: " + fullPath.generic_string();
        if (fileError) {
            detail += " (" + fileError.message() + ")";
        }
        ThrowFieldError(catalogName, lineNumber, fieldName, header, detail);
    }
}

void ValidateResourceRoot(const std::filesystem::path& resourceRoot) {
    std::error_code directoryError;
    if (resourceRoot.empty() ||
        !std::filesystem::is_directory(resourceRoot, directoryError)) {
        std::string detail = "puzzle-data resource root is not a directory: " +
                             resourceRoot.generic_string();
        if (directoryError) {
            detail += " (" + directoryError.message() + ")";
        }
        throw PuzzleDataError(detail);
    }
}

[[nodiscard]] std::filesystem::path ResolveCatalogPath(
    const std::string& text,
    const std::filesystem::path& resourceRoot,
    const std::string_view label) {
    if (text.empty() || text.find('\0') != std::string::npos ||
        text.find('\r') != std::string::npos ||
        text.find('\n') != std::string::npos ||
        text.find(':') != std::string::npos) {
        throw PuzzleDataError(std::string{label} +
                              " path must be non-empty, single-line, and relative");
    }
    const std::filesystem::path relative{text};
    if (relative.has_root_path() || relative.has_root_name() ||
        relative.filename().empty()) {
        throw PuzzleDataError(std::string{label} +
                              " path must be relative to the resource root");
    }
    for (const std::filesystem::path& component : relative) {
        if (component.empty() || component == "." || component == "..") {
            throw PuzzleDataError(std::string{label} +
                                  " path must remain within the resource root");
        }
    }
    return resourceRoot / relative;
}

[[nodiscard]] const data::CsvDocument& RequireDocument(
    const data::CsvParseResult& result,
    const std::string_view label) {
    if (!result.document.has_value()) {
        throw PuzzleDataError(std::string{label} + ": " + result.error);
    }
    return *result.document;
}

struct ParsedLevel final {
    PuzzleDefinition definition;
    std::size_t lineNumber = 0;
};

[[nodiscard]] std::vector<ParsedLevel> ParseLevels(
    const data::CsvDocument& document) {
    ValidateHeader(document, kLevelsHeader, kLevelsCatalogName);
    if (document.records.empty()) {
        throw PuzzleDataError("levels.csv must contain at least one level");
    }

    std::vector<ParsedLevel> parsed;
    parsed.reserve(document.records.size());
    std::unordered_set<std::string> ids;
    for (const data::CsvRecord& record : document.records) {
        ParsedLevel level;
        level.lineNumber = record.lineNumber;
        PuzzleDefinition& definition = level.definition;
        definition.id = ParseDefinitionId(
            record.fields[0], kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[0], kLevelsHeader);
        if (!ids.insert(definition.id).second) {
            ThrowFieldError(kLevelsCatalogName, record.lineNumber,
                            kLevelsHeader[0], kLevelsHeader,
                            "duplicate level ID");
        }
        definition.title = ParseDisplayText(
            record.fields[1], false, kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[1], kLevelsHeader);
        definition.mapPath = ValidateRelativePath(
            record.fields[2], kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[2], kLevelsHeader);
        if (!record.fields[3].empty()) {
            definition.nextLevelId = ParseDefinitionId(
                record.fields[3], kLevelsCatalogName, record.lineNumber,
                kLevelsHeader[3], kLevelsHeader);
        }
        definition.totalLength = ParseNonNegativeFloat(
            record.fields[4], std::nullopt, kLevelsCatalogName,
            record.lineNumber, kLevelsHeader[4], kLevelsHeader);
        definition.minimumSlackRatio = ParseNonNegativeFloat(
            record.fields[5], 1.05f, kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[5], kLevelsHeader);
        definition.backgroundColor = ParseColor(
            record.fields[6], kDefaultBackgroundColor, kLevelsCatalogName,
            record.lineNumber, kLevelsHeader[6], kLevelsHeader);
        definition.vesselColor = ParseColor(
            record.fields[7], kDefaultVesselColor, kLevelsCatalogName,
            record.lineNumber, kLevelsHeader[7], kLevelsHeader);
        definition.baseWidth = ParseNonNegativeFloat(
            record.fields[8], 16.0f, kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[8], kLevelsHeader);
        definition.tipWidth = ParseNonNegativeFloat(
            record.fields[9], 16.0f, kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[9], kLevelsHeader);
        definition.widthVariation = ParseNonNegativeFloat(
            record.fields[10], 0.16f, kLevelsCatalogName, record.lineNumber,
            kLevelsHeader[10], kLevelsHeader);
        parsed.push_back(std::move(level));
    }
    return parsed;
}

[[nodiscard]] std::vector<NodeDefinition> ParseMap(
    const data::CsvDocument& document,
    const std::string_view mapName,
    const std::filesystem::path* resourceRoot,
    const std::span<const NodePresetDefinition> presets) {
    ValidateHeader(document, kMapHeader, mapName);

    std::vector<NodeDefinition> nodes;
    nodes.reserve(document.records.size());
    std::unordered_set<std::string> ids;
    for (const data::CsvRecord& record : document.records) {
        NodeDefinition node;
        node.id = ParseDefinitionId(record.fields[0], mapName, record.lineNumber,
                                    kMapHeader[0], kMapHeader);
        if (!ids.insert(node.id).second) {
            ThrowFieldError(mapName, record.lineNumber, kMapHeader[0], kMapHeader,
                            "duplicate node instance ID");
        }
        node.sourcePresetId = ParseOptionalDefinitionId(
            record.fields[1], mapName, record.lineNumber, kMapHeader[1], kMapHeader);
        const NodePresetDefinition* preset = nullptr;
        if (!node.sourcePresetId.empty()) {
            const auto found = std::find_if(
                presets.begin(), presets.end(), [&node](const NodePresetDefinition& item) {
                    return item.id == node.sourcePresetId;
                });
            if (found == presets.end()) {
                ThrowFieldError(mapName, record.lineNumber, kMapHeader[1], kMapHeader,
                                "unknown node preset ID '" + node.sourcePresetId + "'");
            }
            preset = &*found;
            node.type = preset->type;
            node.texturePath = preset->texturePath;
            node.widthTiles = preset->widthTiles;
            node.heightTiles = preset->heightTiles;
            node.displayName = preset->displayName;
            node.maxIncoming = preset->maxIncoming;
            node.maxOutgoing = preset->maxOutgoing;
            node.maxOutgoingLength = preset->maxOutgoingLength;
        }

        if (!record.fields[2].empty()) {
            node.type = ParseNodeType(record.fields[2], mapName, record.lineNumber,
                                      kMapHeader);
        } else if (preset == nullptr) {
            ThrowFieldError(mapName, record.lineNumber, kMapHeader[2], kMapHeader,
                            "node_type is required when source_preset_id is empty");
        }
        if (!record.fields[3].empty()) {
            node.texturePath = ValidateRelativePath(
                record.fields[3], mapName, record.lineNumber, kMapHeader[3], kMapHeader);
        }
        if (resourceRoot != nullptr && !node.texturePath.empty()) {
            RequireResourceFile(*resourceRoot, node.texturePath, mapName,
                                record.lineNumber, kMapHeader[3], kMapHeader);
        }
        if (!record.fields[4].empty()) {
            node.widthTiles = ParseUnsigned(
                record.fields[4], false, mapName, record.lineNumber,
                kMapHeader[4], kMapHeader);
        }
        if (!record.fields[5].empty()) {
            node.heightTiles = ParseUnsigned(
                record.fields[5], false, mapName, record.lineNumber,
                kMapHeader[5], kMapHeader);
        }
        if (!record.fields[6].empty()) {
            node.displayName = ParseDisplayText(
                record.fields[6], false, mapName, record.lineNumber,
                kMapHeader[6], kMapHeader);
        }

        const bool hasTileX = !record.fields[7].empty();
        const bool hasTileY = !record.fields[8].empty();
        if (hasTileX != hasTileY) {
            ThrowFieldError(mapName, record.lineNumber,
                            hasTileX ? kMapHeader[8] : kMapHeader[7], kMapHeader,
                            "tile_x and tile_y must either both be set or both be empty");
        }
        if (hasTileX) {
            node.tilePosition = TilePosition{
                ParseUnsigned(record.fields[7], false, mapName, record.lineNumber,
                              kMapHeader[7], kMapHeader),
                ParseUnsigned(record.fields[8], false, mapName, record.lineNumber,
                              kMapHeader[8], kMapHeader),
            };
        }

        if (!record.fields[9].empty()) {
            node.maxIncoming = ParseUnsigned(
                record.fields[9], false, mapName, record.lineNumber,
                kMapHeader[9], kMapHeader);
        }
        if (!record.fields[10].empty()) {
            node.maxOutgoing = ParseUnsigned(
                record.fields[10], false, mapName, record.lineNumber,
                kMapHeader[10], kMapHeader);
        }
        if (!record.fields[11].empty()) {
            node.maxOutgoingLength = ParseNonNegativeFloat(
                record.fields[11], std::nullopt, mapName, record.lineNumber,
                kMapHeader[11], kMapHeader);
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

[[nodiscard]] std::vector<NodePresetDefinition> ParseNodePresets(
    const data::CsvDocument& document,
    const std::filesystem::path* resourceRoot) {
    ValidateHeader(document, kNodePresetsHeader, kNodePresetsCatalogName);

    std::vector<NodePresetDefinition> presets;
    presets.reserve(document.records.size());
    std::unordered_set<std::string> ids;
    for (const data::CsvRecord& record : document.records) {
        NodePresetDefinition preset;
        preset.id = ParseDefinitionId(
            record.fields[0], kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[0], kNodePresetsHeader);
        if (!ids.insert(preset.id).second) {
            ThrowFieldError(kNodePresetsCatalogName, record.lineNumber,
                            kNodePresetsHeader[0], kNodePresetsHeader,
                            "duplicate preset ID");
        }
        preset.type = ParseNodeType(
            record.fields[1], kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader);
        preset.texturePath = ValidateOptionalRelativePath(
            record.fields[2], kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[2], kNodePresetsHeader);
        if (resourceRoot != nullptr && !preset.texturePath.empty()) {
            RequireResourceFile(*resourceRoot, preset.texturePath,
                                kNodePresetsCatalogName, record.lineNumber,
                                kNodePresetsHeader[2], kNodePresetsHeader);
        }
        preset.widthTiles = ParseUnsigned(
            record.fields[3], false, kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[3], kNodePresetsHeader);
        preset.heightTiles = ParseUnsigned(
            record.fields[4], false, kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[4], kNodePresetsHeader);
        preset.displayName = ParseDisplayText(
            record.fields[5], true, kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[5], kNodePresetsHeader);
        preset.maxIncoming = ParseUnsigned(
            record.fields[6], true, kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[6], kNodePresetsHeader);
        preset.maxOutgoing = ParseUnsigned(
            record.fields[7], true, kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[7], kNodePresetsHeader);
        preset.maxOutgoingLength = ParseNonNegativeFloat(
            record.fields[8], 0.0f, kNodePresetsCatalogName, record.lineNumber,
            kNodePresetsHeader[8], kNodePresetsHeader);
        presets.push_back(std::move(preset));
    }
    return presets;
}

[[nodiscard]] PuzzleCatalog BuildParsedCatalog(
    const data::CsvDocument& levelsDocument,
    const data::CsvDocument& nodePresetsDocument,
    const std::span<const PuzzleMapCsvSource> mapSources) {
    std::vector<ParsedLevel> levels = ParseLevels(levelsDocument);
    const std::vector<NodePresetDefinition> presets =
        ParseNodePresets(nodePresetsDocument, nullptr);

    std::unordered_map<std::string, std::vector<NodeDefinition>> maps;
    maps.reserve(mapSources.size());
    for (const PuzzleMapCsvSource& source : mapSources) {
        const std::string path = ValidateRelativePath(
            std::string{source.path}, kLevelsCatalogName, 1,
            kLevelsHeader[2], kLevelsHeader);
        const data::CsvParseResult result = data::Csv::Parse(source.contents);
        const data::CsvDocument& document = RequireDocument(result, path);
        std::vector<NodeDefinition> parsedMap =
            ParseMap(document, path, nullptr, presets);
        if (!maps.emplace(path, std::move(parsedMap)).second) {
            throw PuzzleDataError("duplicate map CSV source path: " + path);
        }
    }

    std::vector<PuzzleDefinition> definitions;
    definitions.reserve(levels.size());
    for (ParsedLevel& level : levels) {
        const auto found = maps.find(level.definition.mapPath);
        if (found == maps.end()) {
            ThrowFieldError(kLevelsCatalogName, level.lineNumber,
                            kLevelsHeader[2], kLevelsHeader,
                            "no map CSV source was provided for '" +
                                level.definition.mapPath + "'");
        }
        level.definition.nodes = found->second;
        definitions.push_back(std::move(level.definition));
    }
    return PuzzleCatalog{std::move(definitions)};
}

[[nodiscard]] PuzzleCatalog LoadPuzzleCatalog(
    const PuzzleDataPaths& paths,
    const std::filesystem::path& resourceRoot) {
    const std::filesystem::path levelsPath =
        ResolveCatalogPath(paths.levels, resourceRoot, "levels catalog");
    const data::CsvParseResult levelsResult = data::Csv::Load(levelsPath);
    const data::CsvDocument& levelsDocument =
        RequireDocument(levelsResult, kLevelsCatalogName);
    std::vector<ParsedLevel> levels = ParseLevels(levelsDocument);

    const std::filesystem::path nodesPath =
        ResolveCatalogPath(paths.nodes, resourceRoot, "node preset catalog");
    const data::CsvParseResult nodesResult = data::Csv::Load(nodesPath);
    const std::vector<NodePresetDefinition> presets = ParseNodePresets(
        RequireDocument(nodesResult, kNodePresetsCatalogName), &resourceRoot);

    std::unordered_map<std::string, std::vector<NodeDefinition>> loadedMaps;
    loadedMaps.reserve(levels.size());
    for (const ParsedLevel& level : levels) {
        if (loadedMaps.contains(level.definition.mapPath)) {
            continue;
        }
        const std::filesystem::path mapPath =
            resourceRoot / std::filesystem::path{level.definition.mapPath};
        const data::CsvParseResult mapResult = data::Csv::Load(mapPath);
        const data::CsvDocument& mapDocument =
            RequireDocument(mapResult, level.definition.mapPath);
        loadedMaps.emplace(
            level.definition.mapPath,
            ParseMap(mapDocument, level.definition.mapPath, &resourceRoot, presets));
    }

    std::vector<PuzzleDefinition> definitions;
    definitions.reserve(levels.size());
    for (ParsedLevel& level : levels) {
        level.definition.nodes = loadedMaps.at(level.definition.mapPath);
        definitions.push_back(std::move(level.definition));
    }
    return PuzzleCatalog{std::move(definitions)};
}

} // namespace

bool PuzzleCatalogLoader::Load(const PuzzleDataPaths& paths,
                               const std::string& resourceRoot,
                               PuzzleCatalog& catalog,
                               std::string& error) {
    error.clear();
    try {
        const std::filesystem::path root{resourceRoot};
        ValidateResourceRoot(root);
        PuzzleCatalog parsed = LoadPuzzleCatalog(paths, root);
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
        const data::CsvParseResult levelsResult = data::Csv::Parse(sources.levels);
        const data::CsvParseResult nodesResult = data::Csv::Parse(sources.nodes);
        PuzzleCatalog parsed = BuildParsedCatalog(
            RequireDocument(levelsResult, kLevelsCatalogName),
            RequireDocument(nodesResult, kNodePresetsCatalogName), sources.maps);
        catalog = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool NodePresetCatalogLoader::Load(const NodePresetDataPaths& paths,
                                   const std::string& resourceRoot,
                                   NodePresetCatalog& catalog,
                                   std::string& error) {
    error.clear();
    try {
        const std::filesystem::path root{resourceRoot};
        ValidateResourceRoot(root);
        const std::filesystem::path nodesPath =
            ResolveCatalogPath(paths.nodes, root, "node preset catalog");
        const data::CsvParseResult result = data::Csv::Load(nodesPath);
        NodePresetCatalog parsed{ParseNodePresets(
            RequireDocument(result, kNodePresetsCatalogName), &root)};
        catalog = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

bool NodePresetCatalogLoader::Parse(const std::string_view nodesCsv,
                                    NodePresetCatalog& catalog,
                                    std::string& error) {
    error.clear();
    try {
        const data::CsvParseResult result = data::Csv::Parse(nodesCsv);
        NodePresetCatalog parsed{ParseNodePresets(
            RequireDocument(result, kNodePresetsCatalogName), nullptr)};
        catalog = std::move(parsed);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

} // namespace object_connect
