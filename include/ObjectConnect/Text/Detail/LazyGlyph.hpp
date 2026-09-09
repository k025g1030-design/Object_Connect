#pragma once

#include <functional>
#include <utility>

namespace object_connect::text_detail {

struct GlyphResolution final {
    int glyphIndex = 0;
    bool usedFallback = false;
};

// A cmap lookup returns glyph zero when a scalar is unsupported. Prefer the
// font's U+FFFD glyph in that case; replacementGlyphIndex is itself allowed to
// be zero, which is the font's .notdef glyph and remains a safe final fallback.
[[nodiscard]] constexpr GlyphResolution ResolveGlyphIndex(
    const int requestedGlyphIndex,
    const int replacementGlyphIndex) noexcept {
    return requestedGlyphIndex == 0
               ? GlyphResolution{replacementGlyphIndex, true}
               : GlyphResolution{requestedGlyphIndex, false};
}

// Tracks the irreversible atlas residency contract. A glyph rect is never
// moved or overwritten, and a failed allocation/upload is not retried every
// frame. The work callback may combine rasterization and dirty-region staging.
class LazyGlyphResidency final {
public:
    template <typename Work>
    [[nodiscard]] bool Ensure(Work&& work) {
        if (state_ == State::Ready) {
            return true;
        }
        if (state_ == State::Failed) {
            return false;
        }

        // Mark failure before invoking external work so an exception also
        // prevents a per-frame retry storm.
        state_ = State::Failed;
        if (std::invoke(std::forward<Work>(work))) {
            state_ = State::Ready;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool IsReady() const noexcept {
        return state_ == State::Ready;
    }

    [[nodiscard]] bool HasFailed() const noexcept {
        return state_ == State::Failed;
    }

private:
    enum class State {
        Pending,
        Ready,
        Failed,
    };

    State state_ = State::Pending;
};

} // namespace object_connect::text_detail
