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
    void SetCaptureEnabled(bool enabled) noexcept;

    [[nodiscard]] bool IsCaptureEnabled() const noexcept { return captureEnabled_; }
    [[nodiscard]] bool IsCaptured() const noexcept { return isCaptured_; }
    [[nodiscard]] bool IsWindowFocused() const noexcept { return windowFocused_; }
    [[nodiscard]] bool WasCapturedThisFrame() const noexcept { return capturedThisFrame_; }

private:
    [[nodiscard]] static int QueryCursorDisplayCount() noexcept;
    void SetCursorVisible(bool visible) noexcept;
    void RestoreCursorDisplayCount() noexcept;
    void ReleaseCapture() noexcept;
    [[nodiscard]] bool TryGetClientScreenRect(RECT& screenRect) const noexcept;

    HWND window_ = nullptr;
    RECT originalClipRect_{};
    int cursorCountDelta_ = 0;
    bool hasOriginalClipRect_ = false;
    bool captureEnabled_ = false;
    bool isCaptured_ = false;
    bool windowFocused_ = false;
    bool capturedThisFrame_ = false;
};

} // namespace fps
