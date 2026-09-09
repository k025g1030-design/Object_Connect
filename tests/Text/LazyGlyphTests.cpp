#include "../TestSupport.hpp"

#include "ObjectConnect/Text/Detail/LazyGlyph.hpp"
#include "ObjectConnect/Text/Detail/MonotonicFontId.hpp"

#include <cstdint>
#include <limits>
#include <map>
#include <tuple>

namespace object_connect::tests {
namespace {

using GlyphKey = std::tuple<std::uint32_t, std::uint32_t, char32_t>;

void TestLazyRasterAndUpload(TestContext& context) {
    std::map<GlyphKey, text_detail::LazyGlyphResidency> cache;
    std::map<GlyphKey, int> rasterizations;
    std::map<GlyphKey, int> uploads;
    const auto draw = [&](const GlyphKey key) {
        return cache[key].Ensure([&]() {
            ++rasterizations[key];
            ++uploads[key];
            return true;
        });
    };

    const GlyphKey base{1, 32, U'\u8840'};
    context.Expect(draw(base) && draw(base) && draw(base),
                   "a resident glyph remains drawable across frames");
    context.Expect(rasterizations[base] == 1 && uploads[base] == 1,
                   "the same font-size-codepoint key rasterizes and uploads once");

    const GlyphKey otherFont{2, 32, U'\u8840'};
    const GlyphKey otherSize{1, 41, U'\u8840'};
    const GlyphKey otherCodePoint{1, 32, U'\u7BA1'};
    context.Expect(draw(otherFont) && draw(otherSize) && draw(otherCodePoint),
                   "independent glyph-cache keys can become resident");
    context.Expect(rasterizations.size() == 4 && uploads.size() == 4,
                   "font, pixel size, and Unicode scalar all separate glyph cache entries");
}

void TestFailedAtlasWorkIsSafe(TestContext& context) {
    text_detail::LazyGlyphResidency residency;
    int attempts = 0;
    const auto failAtlasCreation = [&]() {
        return residency.Ensure([&]() {
            ++attempts;
            return false;
        });
    };
    context.Expect(!failAtlasCreation() && !failAtlasCreation() && attempts == 1,
                   "a failed atlas operation is reported and not retried every frame");
    context.Expect(residency.HasFailed() && !residency.IsReady(),
                   "failed glyph residency remains in a safe terminal state");
}

void TestMissingGlyphResolution(TestContext& context) {
    const text_detail::GlyphResolution present =
        text_detail::ResolveGlyphIndex(17, 9);
    context.Expect(present.glyphIndex == 17 && !present.usedFallback,
                   "a cmap hit keeps its requested glyph");

    const text_detail::GlyphResolution replacement =
        text_detail::ResolveGlyphIndex(0, 9);
    context.Expect(replacement.glyphIndex == 9 && replacement.usedFallback,
                   "a cmap miss first selects the font's U+FFFD glyph");

    const text_detail::GlyphResolution missingGlyph =
        text_detail::ResolveGlyphIndex(0, 0);
    context.Expect(missingGlyph.glyphIndex == 0 && missingGlyph.usedFallback,
                   "glyph zero is the safe fallback when U+FFFD is also absent");
}

void TestMonotonicFontHandles(TestContext& context) {
    text_detail::MonotonicFontIdSource ids;
    const std::uint32_t first = ids.Allocate();
    const std::uint32_t second = ids.Allocate();
    // Clearing FontSystem records does not reset the process-wide source.
    const std::uint32_t afterSimulatedFinalize = ids.Allocate();
    context.Expect(first == 1 && second == 2 && afterSimulatedFinalize == 3,
                   "font IDs remain monotonic across registry lifetime boundaries");

    text_detail::MonotonicFontIdSource exhaustion{
        (std::numeric_limits<std::uint32_t>::max)() - 1u};
    const std::uint32_t penultimate = exhaustion.Allocate();
    const std::uint32_t last = exhaustion.Allocate();
    const std::uint32_t exhausted = exhaustion.Allocate();
    context.Expect(
        penultimate == (std::numeric_limits<std::uint32_t>::max)() - 1u &&
            last == (std::numeric_limits<std::uint32_t>::max)() &&
            exhausted == 0 && exhaustion.Allocate() == 0,
        "font ID exhaustion fails safely without wrapping to a reusable handle");
}

} // namespace

void RunLazyGlyphTests(TestContext& context) {
    TestLazyRasterAndUpload(context);
    TestFailedAtlasWorkIsSafe(context);
    TestMissingGlyphResolution(context);
    TestMonotonicFontHandles(context);
}

} // namespace object_connect::tests
