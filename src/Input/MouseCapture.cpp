#include "Input/MouseCapture.hpp"

#include <base/WinApp.h>

namespace fps {

MouseCapture::MouseCapture() noexcept
    : MouseCapture(KamataEngine::WinApp::GetInstance()->GetHwnd()) {}

MouseCapture::MouseCapture(HWND window) noexcept
    : window_(window) {
    hasOriginalClipRect_ = ::GetClipCursor(&originalClipRect_) != FALSE;
    Update();
}

MouseCapture::~MouseCapture() {
    if (hasOriginalClipRect_) {
        ::ClipCursor(&originalClipRect_);
    } else {
        ::ClipCursor(nullptr);
    }
    RestoreCursorDisplayCount();
}

void MouseCapture::Update() noexcept {
    capturedThisFrame_ = false;
    windowFocused_ = window_ != nullptr && ::IsWindow(window_) != FALSE &&
                     ::GetForegroundWindow() == window_ && ::IsIconic(window_) == FALSE;
    if (!captureEnabled_ || !windowFocused_) {
        ReleaseCapture();
        return;
    }

    RECT clientScreenRect{};
    if (!TryGetClientScreenRect(clientScreenRect)) {
        ReleaseCapture();
        return;
    }

    if (::ClipCursor(&clientScreenRect) == FALSE) {
        ReleaseCapture();
        return;
    }

    capturedThisFrame_ = !isCaptured_;
    SetCursorVisible(false);
    isCaptured_ = true;
}

void MouseCapture::SetCaptureEnabled(const bool enabled) noexcept {
    captureEnabled_ = enabled;
    capturedThisFrame_ = false;
    if (!captureEnabled_) {
        ReleaseCapture();
    }
}

int MouseCapture::QueryCursorDisplayCount() noexcept {
    static_cast<void>(::ShowCursor(TRUE));
    return ::ShowCursor(FALSE);
}

void MouseCapture::SetCursorVisible(bool visible) noexcept {
    int displayCount = QueryCursorDisplayCount();

    if (visible) {
        while (displayCount < 0) {
            displayCount = ::ShowCursor(TRUE);
            ++cursorCountDelta_;
        }
        return;
    }

    while (displayCount >= 0) {
        displayCount = ::ShowCursor(FALSE);
        --cursorCountDelta_;
    }
}

void MouseCapture::RestoreCursorDisplayCount() noexcept {
    while (cursorCountDelta_ > 0) {
        static_cast<void>(::ShowCursor(FALSE));
        --cursorCountDelta_;
    }
    while (cursorCountDelta_ < 0) {
        static_cast<void>(::ShowCursor(TRUE));
        ++cursorCountDelta_;
    }
}

void MouseCapture::ReleaseCapture() noexcept {
    if (isCaptured_) {
        ::ClipCursor(nullptr);
        isCaptured_ = false;
    }
    SetCursorVisible(true);
}

bool MouseCapture::TryGetClientScreenRect(RECT& screenRect) const noexcept {
    RECT clientRect{};
    if (::GetClientRect(window_, &clientRect) == FALSE) {
        return false;
    }

    POINT topLeft{clientRect.left, clientRect.top};
    POINT bottomRight{clientRect.right, clientRect.bottom};
    if (::ClientToScreen(window_, &topLeft) == FALSE ||
        ::ClientToScreen(window_, &bottomRight) == FALSE) {
        return false;
    }

    if (bottomRight.x <= topLeft.x || bottomRight.y <= topLeft.y) {
        return false;
    }

    screenRect = {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    return true;
}

} // namespace fps
