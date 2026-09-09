#pragma once

#include "ObjectConnect/Math/Color.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Windows.h defines DrawText as a Win32 encoding-selection macro. The engine
// API deliberately owns this name, so prevent include order from rewriting the
// public declaration or call sites to DrawTextA/DrawTextW.
#ifdef DrawText
#undef DrawText
#endif

namespace object_connect {

struct FontHandle final {
    std::uint32_t value = 0;

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    [[nodiscard]] bool operator==(const FontHandle&) const noexcept = default;
};

struct TextMetrics final {
    float width = 0.0f;
    float height = 0.0f;
    float firstBaseline = 0.0f;
    float lineHeight = 0.0f;
    std::size_t lineCount = 0;
};

enum class TextHorizontalAlignment {
    Left,
    Center,
    Right,
};

enum class TextVerticalAlignment {
    Top,
    Middle,
    Bottom,
    FirstBaseline,
};

struct TextDrawOptions final {
    Vec2 position{};
    std::uint32_t pixelSize = 18;
    Color color{};
    TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left;
    TextVerticalAlignment verticalAlignment = TextVerticalAlignment::Top;
};

struct FontSystemStats final {
    std::uint64_t utf8DecodeCount = 0;
    std::uint64_t layoutCacheHits = 0;
    std::uint64_t layoutCacheMisses = 0;
    std::uint64_t glyphCacheHits = 0;
    std::uint64_t glyphCacheMisses = 0;
    std::uint64_t glyphRasterizations = 0;
    std::uint64_t atlasUploads = 0;
    std::uint64_t missingGlyphFallbacks = 0;
};

class FontSystem final {
public:
    FontSystem() noexcept;
    ~FontSystem();

    FontSystem(const FontSystem&) = delete;
    FontSystem& operator=(const FontSystem&) = delete;
    FontSystem(FontSystem&&) = delete;
    FontSystem& operator=(FontSystem&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    // The owner must finish the last submitted frame before destroying
    // GPU-backed font data.
    void Finalize() noexcept;

    [[nodiscard]] FontHandle LoadFont(const std::string& utf8Path,
                                      std::string& error);
    // Call only outside BeginFrame/Flush and after the last frame that used the
    // font has completed on the GPU (Game does this after DirectXCommon::PostDraw).
    void UnloadFont(FontHandle font) noexcept;

    [[nodiscard]] TextMetrics MeasureText(FontHandle font,
                                          std::uint32_t pixelSize,
                                          std::string_view utf8Text);

    void BeginFrame();
    [[nodiscard]] bool DrawText(FontHandle font, std::string_view utf8Text,
                                const TextDrawOptions& options);
    // Records copy/barrier/draw work into KamataEngine's current command list;
    // command submission and GPU synchronization remain the frame owner's job.
    [[nodiscard]] bool Flush();

    [[nodiscard]] FontSystemStats GetStatistics() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace object_connect
