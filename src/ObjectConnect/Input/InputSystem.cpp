#include "ObjectConnect/Input/InputSystem.hpp"

#include <base/WinApp.h>
#include <input/Input.h>

#include <Windows.h>
#include <dinput.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace object_connect {
namespace {

[[nodiscard]] bool GetClientSize(const HWND window, float& width,
                                 float& height) noexcept {
    RECT client{};
    if (window == nullptr || !::GetClientRect(window, &client)) {
        return false;
    }
    const LONG clientWidth = client.right - client.left;
    const LONG clientHeight = client.bottom - client.top;
    if (clientWidth <= 0 || clientHeight <= 0) {
        return false;
    }
    width = static_cast<float>(clientWidth);
    height = static_cast<float>(clientHeight);
    return true;
}

[[nodiscard]] bool IsCursorInsideClient(const HWND window) noexcept {
    if (window == nullptr || !::IsWindow(window)) {
        return false;
    }

    POINT cursorPosition{};
    RECT clientBounds{};
    if (!::GetCursorPos(&cursorPosition) ||
        !::ScreenToClient(window, &cursorPosition) ||
        !::GetClientRect(window, &clientBounds)) {
        return false;
    }

    // PtInRect deliberately treats the right and bottom edges as exclusive,
    // matching Win32 client-coordinate bounds and preventing a custom cursor
    // from being drawn over the resize border or another non-client region.
    return ::PtInRect(&clientBounds, cursorPosition) != FALSE;
}

[[nodiscard]] Vec2 ToLogicalPosition(const Vec2 clientPosition,
                                     const AxisAlignedBox& bounds,
                                     const float clientWidth,
                                     const float clientHeight) noexcept {
    const Vec2 extent{bounds.maximum.x - bounds.minimum.x,
                      bounds.maximum.y - bounds.minimum.y};
    return {bounds.minimum.x + clientPosition.x * extent.x / clientWidth,
            bounds.minimum.y + clientPosition.y * extent.y / clientHeight};
}

[[nodiscard]] Vec2 ToLogicalDelta(const Vec2 clientDelta,
                                  const AxisAlignedBox& bounds,
                                  const float clientWidth,
                                  const float clientHeight) noexcept {
    const Vec2 extent{bounds.maximum.x - bounds.minimum.x,
                      bounds.maximum.y - bounds.minimum.y};
    return {clientDelta.x * extent.x / clientWidth,
            clientDelta.y * extent.y / clientHeight};
}

[[nodiscard]] std::optional<float> MakeUnwrappedCoordinate(
    const double imageCoordinate, const std::int64_t winding,
    const double extent) noexcept {
    const double value = imageCoordinate +
        static_cast<double>(winding) * extent;
    if (!std::isfinite(value) ||
        std::abs(value) >
            static_cast<double>((std::numeric_limits<float>::max)())) {
        return std::nullopt;
    }
    const float converted = static_cast<float>(value);
    return std::isfinite(converted) ? std::optional<float>{converted}
                                    : std::nullopt;
}

[[nodiscard]] bool MoveCursorToLogicalPosition(
    const HWND window, const Vec2 logicalPosition,
    const AxisAlignedBox& bounds, const float clientWidth,
    const float clientHeight, Vec2& clientTarget) noexcept {
    const Vec2 extent{bounds.maximum.x - bounds.minimum.x,
                      bounds.maximum.y - bounds.minimum.y};
    if (!(extent.x > 0.0f) || !(extent.y > 0.0f)) {
        return false;
    }

    const LONG maximumClientX = static_cast<LONG>(clientWidth) - 1;
    const LONG maximumClientY = static_cast<LONG>(clientHeight) - 1;
    POINT target{
        std::clamp(static_cast<LONG>(std::lround(
                       (logicalPosition.x - bounds.minimum.x) * clientWidth /
                       extent.x)),
                   0L, maximumClientX),
        std::clamp(static_cast<LONG>(std::lround(
                       (logicalPosition.y - bounds.minimum.y) * clientHeight /
                       extent.y)),
                   0L, maximumClientY),
    };
    clientTarget = {static_cast<float>(target.x),
                    static_cast<float>(target.y)};
    if (!::ClientToScreen(window, &target)) {
        return false;
    }
    return ::SetCursorPos(target.x, target.y) != FALSE;
}

} // namespace

bool InputSystem::Initialize(std::string& error) noexcept {
    Finalize();
    error.clear();
    if (KamataEngine::Input::GetInstance() == nullptr ||
        KamataEngine::WinApp::GetInstance() == nullptr) {
        error = "KamataEngine input and window services must be initialized first.";
        return false;
    }
    initialized_ = true;
    const HWND window = KamataEngine::WinApp::GetInstance()->GetHwnd();
    previousWindowFocused_ = window != nullptr && ::GetForegroundWindow() == window &&
                             !::IsIconic(window);
    return true;
}

InputState InputSystem::Sample() noexcept { return Sample({}, false); }

InputState InputSystem::Sample(const AxisAlignedBox& playfieldBounds,
                               const bool wrapPointer) noexcept {
    InputState state{};
    if (!initialized_) {
        return state;
    }

    const HWND window = KamataEngine::WinApp::GetInstance()->GetHwnd();
    state.windowFocused = window != nullptr && ::GetForegroundWindow() == window &&
                          !::IsIconic(window);
    state.mouse.insideClient = IsCursorInsideClient(window);
    state.focusLost = previousWindowFocused_ && !state.windowFocused;
    previousWindowFocused_ = state.windowFocused;
    if (!state.windowFocused) {
        previousLeftHeld_ = false;
        ResetPointerWrap();
        return state;
    }

    KamataEngine::Input* const input = KamataEngine::Input::GetInstance();
    state.keyboard.previousPressed =
        input->TriggerKey(static_cast<BYTE>(DIK_W)) ||
        input->TriggerKey(static_cast<BYTE>(DIK_UP));
    state.keyboard.nextPressed =
        input->TriggerKey(static_cast<BYTE>(DIK_S)) ||
        input->TriggerKey(static_cast<BYTE>(DIK_DOWN));
    state.keyboard.enterPressed =
        input->TriggerKey(static_cast<BYTE>(DIK_RETURN)) ||
        input->TriggerKey(static_cast<BYTE>(DIK_NUMPADENTER));
    state.keyboard.escapePressed = input->TriggerKey(static_cast<BYTE>(DIK_ESCAPE));

    const KamataEngine::Vector2& mouse = input->GetMousePosition();
    const KamataEngine::Input::MouseMove relativeMouse = input->GetMouseMove();
    const Vec2 clientPointerPosition{mouse.x, mouse.y};
    Vec2 pointerPosition = clientPointerPosition;
    float clientWidth = 0.0f;
    float clientHeight = 0.0f;
    const bool hasCoordinateMapping =
        IsValidAxisAlignedBox(playfieldBounds) &&
        playfieldBounds.minimum.x < playfieldBounds.maximum.x &&
        playfieldBounds.minimum.y < playfieldBounds.maximum.y &&
        GetClientSize(window, clientWidth, clientHeight);
    if (hasCoordinateMapping) {
        pointerPosition = ToLogicalPosition(pointerPosition, playfieldBounds,
                                            clientWidth, clientHeight);
    }

    state.mouse.positionX = pointerPosition.x;
    state.mouse.positionY = pointerPosition.y;
    state.mouse.wheelDelta = input->GetWheel();
    state.mouse.leftHeld = input->IsPressMouse(0);
    state.mouse.leftPressed = input->IsTriggerMouse(0) ||
                              (state.mouse.leftHeld && !previousLeftHeld_);
    state.mouse.leftReleased = !state.mouse.leftHeld && previousLeftHeld_;
    previousLeftHeld_ = state.mouse.leftHeld;

    if (!wrapPointer || !hasCoordinateMapping) {
        ResetPointerWrap();
        return state;
    }

    Vec2 clientPointerDelta{};
    if (!pointerWrapTracking_) {
        pointerWrapTracking_ = true;
        unwrappedPointerPosition_ = pointerPosition;
        previousClientPointerPosition_ = clientPointerPosition;
        pointerWinding_ = {};
    } else {
        clientPointerDelta =
            clientPointerPosition - previousClientPointerPosition_;
        const Vec2 logicalDelta = ToLogicalDelta(
            clientPointerDelta, playfieldBounds, clientWidth, clientHeight);
        unwrappedPointerPosition_.x += logicalDelta.x;
        unwrappedPointerPosition_.y += logicalDelta.y;
        previousClientPointerPosition_ = clientPointerPosition;
    }

    // SetCursorPos can surface as a synthetic relative DirectInput sample even
    // though the absolute baseline below already starts at the destination.
    // Suppress that fallback for exactly one sample, then re-arm it so a later
    // legitimate crossing of the same edge is never latched out.
    const bool suppressRelativeMouseX = suppressRelativeMouseX_;
    const bool suppressRelativeMouseY = suppressRelativeMouseY_;
    suppressRelativeMouseX_ = false;
    suppressRelativeMouseY_ = false;

    CanonicalWrappedPoint canonical =
        CanonicalizeWrappedPoint(unwrappedPointerPosition_, playfieldBounds);
    if (!canonical.valid) {
        ResetPointerWrap();
        return state;
    }

    const Vec2 extent{playfieldBounds.maximum.x - playfieldBounds.minimum.x,
                      playfieldBounds.maximum.y - playfieldBounds.minimum.y};
    const float maximumClientX = clientWidth - 1.0f;
    const float maximumClientY = clientHeight - 1.0f;
    const float logicalPixelX = extent.x / clientWidth;
    const float logicalPixelY = extent.y / clientHeight;
    const bool movingLeft = clientPointerDelta.x < 0.0f ||
        (clientPointerDelta.x == 0.0f && !suppressRelativeMouseX &&
         relativeMouse.lX < 0);
    const bool movingRight = clientPointerDelta.x > 0.0f ||
        (clientPointerDelta.x == 0.0f && !suppressRelativeMouseX &&
         relativeMouse.lX > 0);
    const bool movingUp = clientPointerDelta.y < 0.0f ||
        (clientPointerDelta.y == 0.0f && !suppressRelativeMouseY &&
         relativeMouse.lY < 0);
    const bool movingDown = clientPointerDelta.y > 0.0f ||
        (clientPointerDelta.y == 0.0f && !suppressRelativeMouseY &&
         relativeMouse.lY > 0);
    bool boundaryCoordinateValid = true;

    // Client coordinates are integer pixel positions in [0, size - 1].
    // Cross at the last inside pixel only while motion on that axis points out
    // of the client. If Windows clamps the absolute position at the edge, the
    // DirectInput delta supplies direction without contributing displacement.
    if (clientWidth > 1.0f && clientPointerPosition.x <= 0.0f && movingLeft &&
        canonical.winding.x == pointerWinding_.x) {
        const std::optional<float> crossed = MakeUnwrappedCoordinate(
            static_cast<double>(playfieldBounds.minimum.x) - logicalPixelX,
            pointerWinding_.x, extent.x);
        boundaryCoordinateValid = crossed.has_value();
        if (crossed.has_value()) {
            unwrappedPointerPosition_.x = *crossed;
        }
    } else if (clientWidth > 1.0f &&
               clientPointerPosition.x >= maximumClientX && movingRight &&
               canonical.winding.x == pointerWinding_.x) {
        const std::optional<float> crossed = MakeUnwrappedCoordinate(
            playfieldBounds.maximum.x, pointerWinding_.x, extent.x);
        boundaryCoordinateValid = crossed.has_value();
        if (crossed.has_value()) {
            unwrappedPointerPosition_.x = *crossed;
        }
    }
    if (clientHeight > 1.0f && clientPointerPosition.y <= 0.0f && movingUp &&
        canonical.winding.y == pointerWinding_.y) {
        const std::optional<float> crossed = MakeUnwrappedCoordinate(
            static_cast<double>(playfieldBounds.minimum.y) - logicalPixelY,
            pointerWinding_.y, extent.y);
        boundaryCoordinateValid = boundaryCoordinateValid &&
                                  crossed.has_value();
        if (crossed.has_value()) {
            unwrappedPointerPosition_.y = *crossed;
        }
    } else if (clientHeight > 1.0f &&
               clientPointerPosition.y >= maximumClientY && movingDown &&
               canonical.winding.y == pointerWinding_.y) {
        const std::optional<float> crossed = MakeUnwrappedCoordinate(
            playfieldBounds.maximum.y, pointerWinding_.y, extent.y);
        boundaryCoordinateValid = boundaryCoordinateValid &&
                                  crossed.has_value();
        if (crossed.has_value()) {
            unwrappedPointerPosition_.y = *crossed;
        }
    }
    if (!boundaryCoordinateValid) {
        ResetPointerWrap();
        return state;
    }

    canonical = CanonicalizeWrappedPoint(unwrappedPointerPosition_,
                                         playfieldBounds);
    if (!canonical.valid) {
        ResetPointerWrap();
        return state;
    }

    state.mouse.positionX = canonical.position.x;
    state.mouse.positionY = canonical.position.y;
    state.mouse.unwrappedPosition = unwrappedPointerPosition_;
    if (canonical.winding != pointerWinding_) {
        Vec2 clientTarget{};
        if (MoveCursorToLogicalPosition(window, canonical.position,
                                        playfieldBounds, clientWidth,
                                        clientHeight, clientTarget)) {
            // Reset the absolute-position baseline to the warp destination so
            // SetCursorPos never becomes gameplay movement on the next frame.
            previousClientPointerPosition_ = clientTarget;
            if (canonical.winding.x != pointerWinding_.x) {
                suppressRelativeMouseX_ = true;
            }
            if (canonical.winding.y != pointerWinding_.y) {
                suppressRelativeMouseY_ = true;
            }
            pointerWinding_ = canonical.winding;
        }
    }
    return state;
}

void InputSystem::ResetPointerWrap() noexcept {
    pointerWrapTracking_ = false;
    unwrappedPointerPosition_ = {};
    previousClientPointerPosition_ = {};
    suppressRelativeMouseX_ = false;
    suppressRelativeMouseY_ = false;
    pointerWinding_ = {};
}

void InputSystem::Finalize() noexcept {
    initialized_ = false;
    previousLeftHeld_ = false;
    previousWindowFocused_ = false;
    ResetPointerWrap();
}

} // namespace object_connect
