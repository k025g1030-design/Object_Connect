#include "../TestSupport.hpp"

#include "ObjectConnect/Text/TextLayout.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace object_connect::tests {
namespace {

using text_layout::GlyphToken;

void ExpectFloat(TestContext& context, const float actual,
                 const float expected, const std::string_view description) {
    context.Expect(NearlyEqual(actual, expected), description);
}

void TestMetricsAndFontPlacement(TestContext& context) {
    const std::vector<GlyphToken> tokens = {
        {U'A', 10.0f, 0.0f, -2.0f, -7.0f, 8.0f, 9.0f},
        {U'V', 12.0f, -2.0f, 1.0f, -6.0f, 11.0f, 8.0f},
    };
    const text_layout::Layout layout = text_layout::Build(tokens, 8.0f, 12.0f);

    context.Expect(layout.metrics.lineCount == 1,
                   "a non-empty single-line layout reports one line");
    ExpectFloat(context, layout.metrics.width, 20.0f,
                "advance and kerning determine the measured width");
    ExpectFloat(context, layout.metrics.height, 12.0f,
                "one line occupies one line height");
    ExpectFloat(context, layout.metrics.firstBaseline, 8.0f,
                "font ascent determines the first baseline");
    ExpectFloat(context, layout.metrics.lineHeight, 12.0f,
                "font line height is retained in metrics");
    context.Expect(layout.glyphs.size() == 2,
                   "both printable glyphs receive positions");
    ExpectFloat(context, layout.glyphs[0].baselineX, 0.0f,
                "the first glyph begins at the initial pen position");
    ExpectFloat(context, layout.glyphs[1].baselineX, 8.0f,
                "kerning is applied before the following glyph baseline");

    const text_layout::GlyphQuad first = text_layout::ResolveGlyphQuad(
        layout, layout.glyphs[0], {100.0f, 50.0f},
        TextHorizontalAlignment::Left, TextVerticalAlignment::Top);
    ExpectFloat(context, first.left, 98.0f,
                "horizontal bearing offsets the raster from its baseline");
    ExpectFloat(context, first.top, 51.0f,
                "vertical bearing offsets the raster from the font baseline");
    ExpectFloat(context, first.width, 8.0f,
                "the positioned quad preserves raster width");
    ExpectFloat(context, first.height, 9.0f,
                "the positioned quad preserves raster height");
}

void TestLineBreaksAndEmptyText(TestContext& context) {
    const text_layout::Layout empty = text_layout::Build({}, 8.0f, 12.0f);
    context.Expect(empty.metrics.lineCount == 0,
                   "an empty string has no layout lines");
    ExpectFloat(context, empty.metrics.width, 0.0f,
                "an empty string has zero width");
    ExpectFloat(context, empty.metrics.height, 0.0f,
                "an empty string has zero height");
    ExpectFloat(context, empty.metrics.firstBaseline, 8.0f,
                "empty metrics retain the font baseline");
    ExpectFloat(context, empty.metrics.lineHeight, 12.0f,
                "empty metrics retain the font line height");

    const std::vector<GlyphToken> tokens = {
        {U'A', 10.0f},
        {U'\r'},
        {U'\n'},
        {U'B', 6.0f},
        {U'\r'},
        {U'C', 7.0f},
        {U'\n'},
    };
    const text_layout::Layout layout = text_layout::Build(tokens, 8.0f, 12.0f);

    context.Expect(layout.metrics.lineCount == 4,
                   "CRLF, bare CR, and LF each create exactly one new line");
    context.Expect(layout.lineWidths == std::vector<float>{10.0f, 6.0f, 7.0f, 0.0f},
                   "line widths include the empty line after a trailing newline");
    ExpectFloat(context, layout.metrics.width, 10.0f,
                "multiline width is the widest line");
    ExpectFloat(context, layout.metrics.height, 48.0f,
                "trailing newlines contribute to block height");
    context.Expect(layout.glyphs.size() == 3,
                   "line-break tokens do not produce glyph quads");
    context.Expect(layout.glyphs[0].lineIndex == 0 &&
                       layout.glyphs[1].lineIndex == 1 &&
                       layout.glyphs[2].lineIndex == 2,
                   "printable glyphs are assigned to their normalized lines");
    context.Expect(layout.glyphs[1].sourceIndex == 3,
                   "CRLF consumes both source tokens without losing source indices");
}

void TestAlignment(TestContext& context) {
    const std::vector<GlyphToken> tokens = {
        {U'A', 20.0f, 0.0f, -2.0f, -7.0f, 8.0f, 9.0f},
        {U'\n'},
        {U'B', 6.0f, 0.0f, 1.0f, -5.0f, 5.0f, 7.0f},
    };
    const text_layout::Layout layout = text_layout::Build(tokens, 8.0f, 12.0f);
    const Vec2 anchor{100.0f, 50.0f};

    const text_layout::GlyphQuad centered = text_layout::ResolveGlyphQuad(
        layout, layout.glyphs[1], anchor, TextHorizontalAlignment::Center,
        TextVerticalAlignment::Top);
    ExpectFloat(context, centered.left, 98.0f,
                "center alignment uses the current line's width");
    ExpectFloat(context, centered.top, 65.0f,
                "later baselines advance by line height");

    const text_layout::GlyphQuad right = text_layout::ResolveGlyphQuad(
        layout, layout.glyphs[1], anchor, TextHorizontalAlignment::Right,
        TextVerticalAlignment::Top);
    ExpectFloat(context, right.left, 95.0f,
                "right alignment subtracts the complete line width");

    const text_layout::GlyphQuad middle = text_layout::ResolveGlyphQuad(
        layout, layout.glyphs[0], anchor, TextHorizontalAlignment::Left,
        TextVerticalAlignment::Middle);
    ExpectFloat(context, middle.top, 39.0f,
                "middle alignment centers the complete line block");

    const text_layout::GlyphQuad bottom = text_layout::ResolveGlyphQuad(
        layout, layout.glyphs[0], anchor, TextHorizontalAlignment::Left,
        TextVerticalAlignment::Bottom);
    ExpectFloat(context, bottom.top, 27.0f,
                "bottom alignment places the block above the anchor");

    const text_layout::GlyphQuad firstBaseline = text_layout::ResolveGlyphQuad(
        layout, layout.glyphs[0], anchor, TextHorizontalAlignment::Left,
        TextVerticalAlignment::FirstBaseline);
    ExpectFloat(context, firstBaseline.top, 43.0f,
                "first-baseline alignment places the first baseline at the anchor");
}

void TestProductionLruCache(TestContext& context) {
    text_layout::LruCache<int> separatedCache{256};
    std::size_t buildCount = 0;
    const auto build = [&](const int value) {
        return [&, value]() {
            ++buildCount;
            return value;
        };
    };

    const auto first = separatedCache.GetOrBuild(1, 32, "same", build(10));
    const auto repeated = separatedCache.GetOrBuild(1, 32, "same", build(11));
    context.Expect(!first.wasHit && repeated.wasHit && buildCount == 1,
                   "a layout-cache hit does not invoke decode/build again");
    context.Expect(repeated.value != nullptr && *repeated.value == 10,
                   "a cache hit returns the originally built layout");

    const auto otherFont = separatedCache.GetOrBuild(2, 32, "same", build(20));
    const auto otherSize = separatedCache.GetOrBuild(1, 33, "same", build(30));
    context.Expect(!otherFont.wasHit && !otherSize.wasHit && buildCount == 3,
                   "font id and pixel size are independent layout-cache key fields");

    text_layout::LruCache<std::uint32_t> evictionCache{256};
    std::size_t evictionBuildCount = 0;
    for (std::uint32_t index = 0; index < 256; ++index) {
        const std::string text = std::to_string(index);
        const auto inserted = evictionCache.GetOrBuild(
            1, 20, text, [&, index]() {
                ++evictionBuildCount;
                return index;
            });
        context.Expect(!inserted.wasHit,
                       "each unique layout initially misses the cache");
    }
    context.Expect(evictionCache.Size() == 256 &&
                       evictionCache.Capacity() == 256,
                   "the production layout cache is bounded to 256 entries");

    const auto refreshed = evictionCache.GetOrBuild(
        1, 20, "0", [&]() {
            ++evictionBuildCount;
            return 1000u;
        });
    const auto overflow = evictionCache.GetOrBuild(
        1, 20, "256", [&]() {
            ++evictionBuildCount;
            return 256u;
        });
    const auto retained = evictionCache.GetOrBuild(
        1, 20, "0", [&]() {
            ++evictionBuildCount;
            return 1001u;
        });
    const auto evicted = evictionCache.GetOrBuild(
        1, 20, "1", [&]() {
            ++evictionBuildCount;
            return 1u;
        });
    context.Expect(refreshed.wasHit && !overflow.wasHit && retained.wasHit &&
                       !evicted.wasHit,
                   "the 257th entry evicts the least recently used layout");
    context.Expect(evictionBuildCount == 258,
                   "only misses invoke the layout builder during LRU eviction");
    context.Expect(evictionCache.Size() == 256,
                   "LRU replacement never grows beyond 256 entries");
}

} // namespace

void RunTextLayoutTests(TestContext& context) {
    TestMetricsAndFontPlacement(context);
    TestLineBreaksAndEmptyText(context);
    TestAlignment(context);
    TestProductionLruCache(context);
}

} // namespace object_connect::tests
