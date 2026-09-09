#include "ObjectConnect/Text/TextLayout.hpp"

#include <algorithm>

namespace object_connect::text_layout {

Layout Build(const std::span<const GlyphToken> tokens,
             const float firstBaseline, const float lineHeight) {
    Layout layout;
    layout.metrics.firstBaseline = firstBaseline;
    layout.metrics.lineHeight = lineHeight;
    if (tokens.empty()) {
        return layout;
    }

    layout.lineWidths.push_back(0.0f);
    float penX = 0.0f;
    std::size_t lineIndex = 0;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        const GlyphToken& token = tokens[index];
        if (token.codePoint == U'\r' || token.codePoint == U'\n') {
            if (token.codePoint == U'\r' && index + 1 < tokens.size() &&
                tokens[index + 1].codePoint == U'\n') {
                ++index;
            }
            layout.lineWidths[lineIndex] = penX;
            layout.lineWidths.push_back(0.0f);
            ++lineIndex;
            penX = 0.0f;
            continue;
        }

        penX += token.kerningBefore;
        layout.glyphs.push_back({
            index,
            lineIndex,
            penX,
            token.bearingX,
            token.bearingY,
            token.width,
            token.height,
        });
        penX += token.advance;
        layout.lineWidths[lineIndex] = penX;
    }

    layout.lineWidths[lineIndex] = penX;
    layout.metrics.lineCount = layout.lineWidths.size();
    layout.metrics.width = *std::max_element(
        layout.lineWidths.begin(), layout.lineWidths.end());
    layout.metrics.height =
        static_cast<float>(layout.metrics.lineCount) * lineHeight;
    return layout;
}

GlyphQuad ResolveGlyphQuad(
    const Layout& layout, const PositionedGlyph& glyph, const Vec2 anchor,
    const TextHorizontalAlignment horizontalAlignment,
    const TextVerticalAlignment verticalAlignment) noexcept {
    if (glyph.lineIndex >= layout.lineWidths.size()) {
        return {};
    }

    float lineLeft = anchor.x;
    const float lineWidth = layout.lineWidths[glyph.lineIndex];
    switch (horizontalAlignment) {
    case TextHorizontalAlignment::Left:
        break;
    case TextHorizontalAlignment::Center:
        lineLeft -= lineWidth * 0.5f;
        break;
    case TextHorizontalAlignment::Right:
        lineLeft -= lineWidth;
        break;
    }

    float blockTop = anchor.y;
    switch (verticalAlignment) {
    case TextVerticalAlignment::Top:
        break;
    case TextVerticalAlignment::Middle:
        blockTop -= layout.metrics.height * 0.5f;
        break;
    case TextVerticalAlignment::Bottom:
        blockTop -= layout.metrics.height;
        break;
    case TextVerticalAlignment::FirstBaseline:
        blockTop -= layout.metrics.firstBaseline;
        break;
    }

    const float baselineY =
        blockTop + layout.metrics.firstBaseline +
        static_cast<float>(glyph.lineIndex) * layout.metrics.lineHeight;
    return {
        lineLeft + glyph.baselineX + glyph.bearingX,
        baselineY + glyph.bearingY,
        glyph.width,
        glyph.height,
    };
}

} // namespace object_connect::text_layout
