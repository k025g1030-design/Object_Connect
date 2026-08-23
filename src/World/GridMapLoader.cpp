#include "RetroFPS/World/GridMapLoader.hpp"

#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fps {
namespace {

[[nodiscard]] std::string MakeParseMessage(
    const std::size_t line, const std::size_t column, const std::string& detail) {
    std::ostringstream stream;
    stream << "line " << line << ", column " << column << ": " << detail;
    return stream.str();
}

class GridMapParseError final : public std::runtime_error {
public:
    GridMapParseError(
        const std::size_t line,
        const std::size_t column,
        const std::string& detail)
        : std::runtime_error(MakeParseMessage(line, column, detail)) {}
};

[[nodiscard]] TileType DecodeTile(
    const char cell, const std::size_t line, const std::size_t column) {
    switch (cell) {
    case '.':
        return TileType::Floor;
    case '#':
        return TileType::Wall;
    case 'P':
        return TileType::PlayerSpawn;
    case 'M':
        return TileType::MeleeEnemySpawn;
    case 'R':
        return TileType::RangedEnemySpawn;
    case 'D':
        return TileType::NextMapExit;
    default:
        throw GridMapParseError(
            line,
            column,
            "invalid cell; expected '#', '.', 'P', 'M', 'R', or 'D'");
    }
}

} // namespace

MapLoadResult GridMapLoader::Parse(const std::string_view text) {
    try {
        if (text.empty()) {
            throw GridMapParseError(1, 1, "map is empty");
        }

        std::vector<TileType> tiles;
        std::optional<GridCoordinate> playerSpawnCell;
        std::vector<EnemySpawnPoint> enemySpawnPoints;
        std::optional<GridCoordinate> nextMapExitCell;
        std::size_t expectedWidth = 0;
        std::size_t height = 0;

        const auto parseLine = [&](const std::string_view line, const std::size_t lineNumber) {
            if (line.empty()) {
                throw GridMapParseError(lineNumber, 1, "map rows must not be empty");
            }

            if (height == 0) {
                expectedWidth = line.size();
            } else if (line.size() != expectedWidth) {
                const std::size_t mismatchColumn =
                    line.size() < expectedWidth ? line.size() + 1 : expectedWidth + 1;
                throw GridMapParseError(
                    lineNumber, mismatchColumn, "map rows must form a rectangle");
            }

            for (std::size_t column = 0; column < line.size(); ++column) {
                const TileType tile = DecodeTile(line[column], lineNumber, column + 1);
                const GridCoordinate coordinate{height, column};
                switch (tile) {
                case TileType::PlayerSpawn:
                    if (playerSpawnCell.has_value()) {
                        throw GridMapParseError(
                            lineNumber,
                            column + 1,
                            "map must contain exactly one player spawn 'P'");
                    }
                    playerSpawnCell = coordinate;
                    break;
                case TileType::MeleeEnemySpawn:
                    enemySpawnPoints.push_back({EnemyKind::Melee, coordinate});
                    break;
                case TileType::RangedEnemySpawn:
                    enemySpawnPoints.push_back({EnemyKind::Ranged, coordinate});
                    break;
                case TileType::NextMapExit:
                    if (nextMapExitCell.has_value()) {
                        throw GridMapParseError(
                            lineNumber,
                            column + 1,
                            "map must contain exactly one next-map exit 'D'");
                    }
                    nextMapExitCell = coordinate;
                    break;
                case TileType::Floor:
                case TileType::Wall:
                    break;
                }
                tiles.push_back(tile);
            }

            ++height;
        };

        std::size_t lineStart = 0;
        std::size_t lineNumber = 1;
        for (std::size_t index = 0; index < text.size(); ++index) {
            if (text[index] != '\n') {
                continue;
            }

            std::size_t lineEnd = index;
            if (lineEnd > lineStart && text[lineEnd - 1] == '\r') {
                --lineEnd;
            }
            parseLine(text.substr(lineStart, lineEnd - lineStart), lineNumber);
            lineStart = index + 1;
            ++lineNumber;
        }

        if (lineStart < text.size()) {
            parseLine(text.substr(lineStart), lineNumber);
        }

        if (height == 0) {
            throw GridMapParseError(1, 1, "map is empty");
        }
        if (!playerSpawnCell.has_value()) {
            throw GridMapParseError(1, 1, "map must contain exactly one player spawn 'P'");
        }
        if (!nextMapExitCell.has_value()) {
            throw GridMapParseError(1, 1, "map must contain exactly one next-map exit 'D'");
        }

        return {
            GridMap(
                std::move(tiles),
                expectedWidth,
                height,
                *playerSpawnCell,
                std::move(enemySpawnPoints),
                *nextMapExitCell),
            {}};
    } catch (const std::exception& error) {
        return {std::nullopt, error.what()};
    }
}

MapLoadResult GridMapLoader::Load(const std::filesystem::path& path) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open map file: " + path.string());
        }

        const std::string contents{
            std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad()) {
            throw std::runtime_error("failed to read map file: " + path.string());
        }

        return Parse(contents);
    } catch (const std::exception& error) {
        return {std::nullopt, error.what()};
    }
}

} // namespace fps
