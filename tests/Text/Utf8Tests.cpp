#include "../TestSupport.hpp"

#include "ObjectConnect/Text/Utf8.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace object_connect::tests {
namespace {

void ExpectDecoded(TestContext& context,
                   const std::string_view encoded,
                   const std::initializer_list<char32_t> expected,
                   const std::size_t expectedReplacementCount,
                   const std::string_view description) {
    const Utf8DecodeResult decoded = DecodeUtf8(encoded);
    context.Expect(decoded.codePoints == std::vector<char32_t>{expected}, description);
    context.Expect(decoded.replacementCount == expectedReplacementCount,
                   std::string{description} + " reports its replacement count");
}

void TestValidUtf8(TestContext& context) {
    ExpectDecoded(context, {}, {}, 0, "an empty UTF-8 string decodes to no code points");
    ExpectDecoded(context, std::string_view{"\0A", 2}, {U'\0', U'A'}, 0,
                  "embedded NUL and ASCII bytes are preserved");
    ExpectDecoded(context, "START\r\nNEXT\rEND\n",
                  {U'S', U'T', U'A', U'R', U'T', U'\r', U'\n', U'N', U'E', U'X',
                   U'T', U'\r', U'E', U'N', U'D', U'\n'},
                  0, "CR, LF, and CRLF remain unnormalized for text layout");
    ExpectDecoded(context, "START", {U'S', U'T', U'A', U'R', U'T'}, 0,
                  "the acceptance START label decodes exactly");
    ExpectDecoded(context, "LEVEL 01",
                  {U'L', U'E', U'V', U'E', U'L', U' ', U'0', U'1'}, 0,
                  "the acceptance level label decodes exactly");
    ExpectDecoded(context, "SCORE: 1000",
                  {U'S', U'C', U'O', U'R', U'E', U':', U' ', U'1', U'0', U'0', U'0'},
                  0, "the acceptance score label decodes exactly");

    ExpectDecoded(context,
                  "\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF"
                  "\xEE\x80\x80\xEF\xBF\xBF\xF0\x90\x80\x80"
                  "\xF4\x8F\xBF\xBF",
                  {static_cast<char32_t>(0x0080), static_cast<char32_t>(0x07FF),
                   static_cast<char32_t>(0x0800), static_cast<char32_t>(0xD7FF),
                   static_cast<char32_t>(0xE000), static_cast<char32_t>(0xFFFF),
                   static_cast<char32_t>(0x10000), static_cast<char32_t>(0x10FFFF)},
                  0, "all two-, three-, and four-byte scalar boundaries decode");

    ExpectDecoded(context, "つなぐ・ゲーム・血管接続🙂",
                  {U'つ', U'な', U'ぐ', U'・', U'ゲ', U'ー', U'ム', U'・', U'血',
                   U'管', U'接', U'続', static_cast<char32_t>(0x1F642)},
                  0, "Hiragana, Katakana, Kanji, and a four-byte scalar decode together");

    ExpectDecoded(context, "ゲームスタート",
                  {U'ゲ', U'ー', U'ム', U'ス', U'タ', U'ー', U'ト'}, 0,
                  "the acceptance Katakana phrase decodes exactly");
    ExpectDecoded(context, "ステージ選択",
                  {U'ス', U'テ', U'ー', U'ジ', U'選', U'択'}, 0,
                  "the shipped level-select heading decodes exactly");
    ExpectDecoded(context, "LEVEL 01 - 血管接続",
                  {U'L', U'E', U'V', U'E', U'L', U' ', U'0', U'1', U' ', U'-',
                   U' ', U'血', U'管', U'接', U'続'},
                  0, "the acceptance English, numeric, and Kanji text decodes exactly");
    ExpectDecoded(context, "ステージクリア\nSCORE: 1200",
                  {U'ス', U'テ', U'ー', U'ジ', U'ク', U'リ', U'ア', U'\n',
                   U'S', U'C', U'O', U'R', U'E', U':', U' ', U'1', U'2', U'0', U'0'},
                  0, "the acceptance multiline Japanese and ASCII text preserves LF");
}

void TestInvalidLeadBytes(TestContext& context) {
    ExpectDecoded(context, "\x80\xBF", {U'\uFFFD', U'\uFFFD'}, 2,
                  "stray continuation bytes are replaced individually");
    ExpectDecoded(context, "\xC0\xAF\xC1\xBF", {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'},
                  4, "illegal two-byte leads cannot encode overlong ASCII");
    ExpectDecoded(context, "\xF5\x80\x80\x80\xFF",
                  {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'}, 5,
                  "out-of-range lead and continuation bytes always make progress");
}

void TestRestrictedScalarRanges(TestContext& context) {
    ExpectDecoded(context, "\xE0\x80\xAF", {U'\uFFFD', U'\uFFFD', U'\uFFFD'}, 3,
                  "an overlong three-byte sequence is rejected by maximal subpart");
    ExpectDecoded(context, "\xF0\x80\x80\xAF",
                  {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'}, 4,
                  "an overlong four-byte sequence is rejected by maximal subpart");
    ExpectDecoded(context, "\xED\xA0\x80", {U'\uFFFD', U'\uFFFD', U'\uFFFD'}, 3,
                  "UTF-16 surrogate code points are rejected");
    ExpectDecoded(context, "\xF4\x90\x80\x80",
                  {U'\uFFFD', U'\uFFFD', U'\uFFFD', U'\uFFFD'}, 4,
                  "code points beyond U+10FFFF are rejected");
}

void TestTruncationAndRecovery(TestContext& context) {
    ExpectDecoded(context, "\xC2", {U'\uFFFD'}, 1,
                  "a truncated two-byte sequence becomes one replacement");
    ExpectDecoded(context, "\xE2\x82", {U'\uFFFD'}, 1,
                  "a truncated three-byte maximal subpart becomes one replacement");
    ExpectDecoded(context, "\xF0\x9F\x99", {U'\uFFFD'}, 1,
                  "a truncated four-byte maximal subpart becomes one replacement");
    ExpectDecoded(context, "\xE2\x82" "A", {U'\uFFFD', U'A'}, 1,
                  "a non-continuation terminates and follows a maximal subpart");
    ExpectDecoded(context, "\xE2" "A\x80", {U'\uFFFD', U'A', U'\uFFFD'}, 2,
                  "an invalid second byte is reprocessed before later stray continuation");
    ExpectDecoded(context, "A\xF0\x9F" "B\xC2\xA2" "C",
                  {U'A', U'\uFFFD', U'B', U'\u00A2', U'C'}, 1,
                  "decoding recovers after malformed input and resumes valid UTF-8");
}

} // namespace

void RunUtf8Tests(TestContext& context) {
    TestValidUtf8(context);
    TestInvalidLeadBytes(context);
    TestRestrictedScalarRanges(context);
    TestTruncationAndRecovery(context);
}

} // namespace object_connect::tests
