#include "RetroFPS/Data/Csv.hpp"

#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fps::data {
namespace {

[[nodiscard]] std::string MakeCsvError(
    const std::size_t line,
    const std::size_t column,
    const std::string& detail) {
    std::ostringstream stream;
    stream << "line " << line << ", column " << column << ": " << detail;
    return stream.str();
}

class CsvSyntaxError final : public std::runtime_error {
public:
    CsvSyntaxError(
        const std::size_t line,
        const std::size_t column,
        const std::string& detail)
        : std::runtime_error(MakeCsvError(line, column, detail)) {}
};

enum class FieldState {
    Start,
    Unquoted,
    Quoted,
    AfterQuoted,
};

[[nodiscard]] bool StartsWithUtf8Bom(const std::string_view text) noexcept {
    return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEFu &&
           static_cast<unsigned char>(text[1]) == 0xBBu &&
           static_cast<unsigned char>(text[2]) == 0xBFu;
}

[[nodiscard]] std::vector<CsvRecord> ParseRecords(std::string_view text) {
    if (StartsWithUtf8Bom(text)) {
        text.remove_prefix(3);
    }
    if (text.empty()) {
        throw CsvSyntaxError(1, 1, "CSV is empty");
    }

    std::vector<CsvRecord> records;
    std::vector<std::string> fields;
    std::string field;
    std::size_t index = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t recordLine = 1;
    std::size_t quotedStartLine = 0;
    std::size_t quotedStartColumn = 0;
    FieldState state = FieldState::Start;
    bool recordOpen = false;

    const auto consumeCharacter = [&]() noexcept {
        ++index;
        ++column;
    };
    const auto consumeNewline = [&]() {
        if (text[index] == '\r') {
            if (index + 1 >= text.size() || text[index + 1] != '\n') {
                throw CsvSyntaxError(line, column, "carriage return must be followed by line feed");
            }
            index += 2;
        } else {
            ++index;
        }
        ++line;
        column = 1;
    };
    const auto finishField = [&]() {
        fields.push_back(std::move(field));
        field.clear();
    };
    const auto finishRecord = [&]() {
        finishField();
        records.push_back({recordLine, std::move(fields)});
        fields.clear();
        state = FieldState::Start;
        recordOpen = false;
    };

    while (index < text.size()) {
        const char character = text[index];
        const bool isNewline = character == '\n' || character == '\r';

        if (state == FieldState::Quoted) {
            if (character == '"') {
                if (index + 1 < text.size() && text[index + 1] == '"') {
                    field.push_back('"');
                    consumeCharacter();
                    consumeCharacter();
                } else {
                    state = FieldState::AfterQuoted;
                    consumeCharacter();
                }
            } else if (isNewline) {
                field.push_back('\n');
                consumeNewline();
            } else {
                field.push_back(character);
                consumeCharacter();
            }
            continue;
        }

        if (isNewline) {
            finishRecord();
            consumeNewline();
            recordLine = line;
            continue;
        }

        if (state == FieldState::Start) {
            recordOpen = true;
            if (character == '"') {
                quotedStartLine = line;
                quotedStartColumn = column;
                state = FieldState::Quoted;
                consumeCharacter();
            } else if (character == ',') {
                finishField();
                consumeCharacter();
            } else {
                field.push_back(character);
                state = FieldState::Unquoted;
                consumeCharacter();
            }
            continue;
        }

        if (state == FieldState::Unquoted) {
            if (character == '"') {
                throw CsvSyntaxError(line, column, "quote must begin a quoted field");
            }
            if (character == ',') {
                finishField();
                state = FieldState::Start;
                consumeCharacter();
            } else {
                field.push_back(character);
                consumeCharacter();
            }
            continue;
        }

        if (character == ',') {
            finishField();
            state = FieldState::Start;
            consumeCharacter();
            continue;
        }
        throw CsvSyntaxError(line, column, "unexpected character after closing quote");
    }

    if (state == FieldState::Quoted) {
        throw CsvSyntaxError(quotedStartLine, quotedStartColumn, "unterminated quoted field");
    }
    if (recordOpen) {
        finishRecord();
    }
    return records;
}

} // namespace

CsvParseResult Csv::Parse(const std::string_view text) {
    try {
        std::vector<CsvRecord> parsed = ParseRecords(text);
        if (parsed.empty()) {
            throw CsvSyntaxError(1, 1, "CSV does not contain a header record");
        }

        CsvDocument document;
        document.header = std::move(parsed.front().fields);
        document.records.reserve(parsed.size() - 1);
        for (std::size_t index = 1; index < parsed.size(); ++index) {
            CsvRecord& record = parsed[index];
            if (record.fields.size() != document.header.size()) {
                throw CsvSyntaxError(
                    record.lineNumber,
                    1,
                    "record has " + std::to_string(record.fields.size()) +
                        " fields; expected " + std::to_string(document.header.size()));
            }
            document.records.push_back(std::move(record));
        }
        return {std::move(document), {}};
    } catch (const std::exception& exception) {
        return {std::nullopt, exception.what()};
    }
}

CsvParseResult Csv::Load(const std::filesystem::path& path) {
    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open CSV file: " + path.generic_string());
        }
        const std::string contents{
            std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad()) {
            throw std::runtime_error("failed to read CSV file: " + path.generic_string());
        }

        CsvParseResult result = Parse(contents);
        if (!result && !result.error.empty()) {
            result.error = path.generic_string() + ": " + result.error;
        }
        return result;
    } catch (const std::exception& exception) {
        return {std::nullopt, exception.what()};
    }
}

} // namespace fps::data
