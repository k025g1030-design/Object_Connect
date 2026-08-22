#pragma once

#include <Windows.h>

namespace fps {

class MouseCapture final {
public:
    MouseCapture() noexcept;
    explicit MouseCapture(HWND window) noexcept;
    ~MouseCapture();

    MouseCapture(const MouseCapture&) = delete;
    MouseCapture& operator=(const MouseCapture&) = delete;
    MouseCapture(MouseCapture&&) = delete;
    MouseCapture& operator=(MouseCapture&&) = delete;

    void Update() noexcept;
    [[nodiscard]] bool IsCaptured() const noexcept { return isCaptured_; }

private:
    [[nodiscard]] static int QueryCursorDisplayCount() noexcept;
    void SetCursorVisible(bool visible) noexcept;
    void RestoreCursorDisplayCount() noexcept;
    void ReleaseCaptureForFocusLoss() noexcept;
    [[nodiscard]] bool TryGetClientScreenRect(RECT& screenRect) const noexcept;

    HWND window_ = nullptr;
    RECT originalClipRect_{};
    int cursorCountDelta_ = 0;
    bool hasOriginalClipRect_ = false;
    bool isCaptured_ = false;
};

} // namespace fps
