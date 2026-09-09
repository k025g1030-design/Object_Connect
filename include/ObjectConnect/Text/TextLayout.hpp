#pragma once

#include "ObjectConnect/Text/FontSystem.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace object_connect::text_layout {

// One decoded scalar together with the font-derived values needed by the
// simple, left-to-right layout supported by FontSystem. Line-break tokens only
// need codePoint; their remaining fields are ignored.
struct GlyphToken final {
    char32_t codePoint = 0;
    float advance = 0.0f;
    float kerningBefore = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct PositionedGlyph final {
    std::size_t sourceIndex = 0;
    std::size_t lineIndex = 0;
    float baselineX = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct Layout final {
    TextMetrics metrics{};
    std::vector<float> lineWidths;
    std::vector<PositionedGlyph> glyphs;
};

struct GlyphQuad final {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Performs deliberately simple left-to-right layout. It recognizes LF, CRLF,
// and bare CR, but intentionally performs no shaping or bidirectional layout.
[[nodiscard]] Layout Build(std::span<const GlyphToken> tokens,
                           float firstBaseline, float lineHeight);

// Resolves both line-specific horizontal alignment and block-wide vertical
// alignment, then applies the glyph's font bearing.
[[nodiscard]] GlyphQuad ResolveGlyphQuad(
    const Layout& layout, const PositionedGlyph& glyph, Vec2 anchor,
    TextHorizontalAlignment horizontalAlignment,
    TextVerticalAlignment verticalAlignment) noexcept;

// The production layout cache is kept here (rather than inside the D3D
// implementation) so keying and LRU behavior remain headless-testable. Value
// may include renderer-private bindings; only the cache policy is generic.
template <typename Value>
class LruCache final {
public:
    struct Lookup final {
        Value* value = nullptr;
        bool wasHit = false;
    };

    explicit LruCache(const std::size_t capacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    template <typename Builder>
    [[nodiscard]] Lookup GetOrBuild(const std::uint32_t fontId,
                                    const std::uint32_t pixelSize,
                                    const std::string_view text,
                                    Builder&& builder) {
        Key key{fontId, pixelSize, std::string{text}};
        const auto existing = entries_.find(key);
        if (existing != entries_.end()) {
            existing->second.lastUse = ++useCounter_;
            return {&existing->second.value, true};
        }

        Value built = std::invoke(std::forward<Builder>(builder));
        if (entries_.size() >= capacity_) {
            auto oldest = entries_.begin();
            for (auto iterator = entries_.begin(); iterator != entries_.end();
                 ++iterator) {
                if (iterator->second.lastUse < oldest->second.lastUse) {
                    oldest = iterator;
                }
            }
            entries_.erase(oldest);
        }

        Entry entry{std::move(built), ++useCounter_};
        const auto inserted =
            entries_.emplace(std::move(key), std::move(entry)).first;
        return {&inserted->second.value, false};
    }

    void EraseFont(const std::uint32_t fontId) noexcept {
        for (auto iterator = entries_.begin(); iterator != entries_.end();) {
            if (iterator->first.fontId == fontId) {
                iterator = entries_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void Clear() noexcept { entries_.clear(); }

    [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
    [[nodiscard]] std::size_t Capacity() const noexcept { return capacity_; }

private:
    struct Key final {
        std::uint32_t fontId = 0;
        std::uint32_t pixelSize = 0;
        std::string text;

        [[nodiscard]] bool operator==(const Key&) const noexcept = default;
    };

    struct KeyHash final {
        [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
            std::size_t value = std::hash<std::string>{}(key.text);
            value ^= static_cast<std::size_t>(key.fontId) + 0x9E3779B9u +
                     (value << 6u) + (value >> 2u);
            value ^= static_cast<std::size_t>(key.pixelSize) + 0x9E3779B9u +
                     (value << 6u) + (value >> 2u);
            return value;
        }
    };

    struct Entry final {
        Value value;
        std::uint64_t lastUse = 0;
    };

    std::size_t capacity_ = 1;
    std::uint64_t useCounter_ = 0;
    std::unordered_map<Key, Entry, KeyHash> entries_;
};

} // namespace object_connect::text_layout
