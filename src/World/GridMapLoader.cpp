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

} // namespace

MapLoadResult GridMapLoader::Parse(const std::string_view text) {
    try {
        if (text.empty()) {
            throw GridMapParseError(1, 1, "map is empty");
        }

        std::vector<std::string> rows;
        std::optional<GridCoordinate> spawnCell;
        std::size_t expectedWidth = 0;

        const auto parseLine = [&](const std::string_view line, const std::size_t lineNumber) {
            if (line.empty()) {
                throw GridMapParseError(lineNumber, 1, "map rows must not be empty");
            }

            for (std::size_t column = 0; column < line.size(); ++column) {
                const char cell = line[column];
                if (cell != '#' && cell != '.' && cell != 'P') {
                    throw GridMapParseError(
                        lineNumber, column + 1, "invalid cell; expected '#', '.', or 'P'");
                }

                if (cell == 'P') {
                    if (spawnCell.has_value()) {
                        throw GridMapParseError(
                            lineNumber,
                            column + 1,
                            "map must contain exactly one player spawn 'P'");
                    }
                    spawnCell = GridCoordinate{rows.size(), column};
                }
            }

            if (rows.empty()) {
                expectedWidth = line.size();
            } else if (line.size() != expectedWidth) {
                const std::size_t mismatchColumn =
                    line.size() < expectedWidth ? line.size() + 1 : expectedWidth + 1;
                throw GridMapParseError(
                    lineNumber, mismatchColumn, "map rows must form a rectangle");
            }

            rows.emplace_back(line);
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

        if (rows.empty()) {
            throw GridMapParseError(1, 1, "map is empty");
        }
        if (!spawnCell.has_value()) {
            throw GridMapParseError(1, 1, "map must contain exactly one player spawn 'P'");
        }

        return {GridMap(std::move(rows), *spawnCell), {}};
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
