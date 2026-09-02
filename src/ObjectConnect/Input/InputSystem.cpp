#include "ObjectConnect/Input/InputSystem.hpp"

#include <base/WinApp.h>
#include <input/Input.h>

#include <Windows.h>
#include <dinput.h>

namespace object_connect {

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

InputState InputSystem::Sample() noexcept {
    InputState state{};
    if (!initialized_) {
        return state;
    }

    const HWND window = KamataEngine::WinApp::GetInstance()->GetHwnd();
    state.windowFocused = window != nullptr && ::GetForegroundWindow() == window &&
                          !::IsIconic(window);
    state.focusLost = previousWindowFocused_ && !state.windowFocused;
    previousWindowFocused_ = state.windowFocused;
    if (!state.windowFocused) {
        previousLeftHeld_ = false;
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
    state.keyboard.retryPressed = input->TriggerKey(static_cast<BYTE>(DIK_R));

    const KamataEngine::Vector2& mouse = input->GetMousePosition();
    state.mouse.positionX = mouse.x;
    state.mouse.positionY = mouse.y;
    state.mouse.leftHeld = input->IsPressMouse(0);
    state.mouse.leftPressed = input->IsTriggerMouse(0) ||
                              (state.mouse.leftHeld && !previousLeftHeld_);
    state.mouse.leftReleased = !state.mouse.leftHeld && previousLeftHeld_;
    previousLeftHeld_ = state.mouse.leftHeld;
    return state;
}

void InputSystem::Finalize() noexcept {
    initialized_ = false;
    previousLeftHeld_ = false;
    previousWindowFocused_ = false;
}

} // namespace object_connect
