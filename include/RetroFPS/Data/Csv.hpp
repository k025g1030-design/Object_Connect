#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fps::data {

struct CsvRecord final {
    std::size_t lineNumber = 0;
    std::vector<std::string> fields;
};

struct CsvDocument final {
    std::vector<std::string> header;
    std::vector<CsvRecord> records;
};

struct CsvParseResult final {
    std::optional<CsvDocument> document;
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept { return document.has_value(); }
    [[nodiscard]] explicit operator bool() const noexcept { return Succeeded(); }
};

// Strict RFC-4180-style CSV reader used by the game-data catalogs. UTF-8 BOM,
// LF and CRLF are accepted. Quoted fields may contain commas/newlines and use
// doubled quotes as escapes; malformed quoting and ragged records are rejected.
class Csv final {
public:
    [[nodiscard]] static CsvParseResult Parse(std::string_view text);
    [[nodiscard]] static CsvParseResult Load(const std::filesystem::path& path);
};

} // namespace fps::data
