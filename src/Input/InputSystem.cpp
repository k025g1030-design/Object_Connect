#include "RetroFPS/Input/InputSystem.hpp"

#include "Input/MouseCapture.hpp"

#include <input/Input.h>

#include <exception>

namespace fps {

InputSystem::InputSystem() noexcept = default;

InputSystem::~InputSystem() {
    Finalize();
}

bool InputSystem::Initialize(std::string& error) {
    Finalize();
    error.clear();

    try {
        mouseCapture_ = std::make_unique<MouseCapture>();
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize input: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize input because of an unknown error.";
    }

    return false;
}

InputState InputSystem::Sample() noexcept {
    InputState state{};
    if (!mouseCapture_) {
        return state;
    }

    mouseCapture_->Update();
    state.windowFocused = mouseCapture_->IsWindowFocused();
    state.mouse.captured = mouseCapture_->IsCaptured();
    if (!state.windowFocused) {
        return state;
    }

    KamataEngine::Input* const input = KamataEngine::Input::GetInstance();
    state.keyboard.w = input->PushKey(static_cast<BYTE>(DIK_W));
    state.keyboard.a = input->PushKey(static_cast<BYTE>(DIK_A));
    state.keyboard.s = input->PushKey(static_cast<BYTE>(DIK_S));
    state.keyboard.d = input->PushKey(static_cast<BYTE>(DIK_D));
    state.keyboard.wPressed = input->TriggerKey(static_cast<BYTE>(DIK_W));
    state.keyboard.sPressed = input->TriggerKey(static_cast<BYTE>(DIK_S));
    state.keyboard.upPressed = input->TriggerKey(static_cast<BYTE>(DIK_UP));
    state.keyboard.downPressed = input->TriggerKey(static_cast<BYTE>(DIK_DOWN));
    state.keyboard.enterPressed =
        input->TriggerKey(static_cast<BYTE>(DIK_RETURN)) ||
        input->TriggerKey(static_cast<BYTE>(DIK_NUMPADENTER));
    state.keyboard.escapePressed = input->TriggerKey(static_cast<BYTE>(DIK_ESCAPE));

    const KamataEngine::Vector2& mousePosition = input->GetMousePosition();
    state.mouse.positionX = mousePosition.x;
    state.mouse.positionY = mousePosition.y;
    state.mouse.leftPressed = input->IsTriggerMouse(0);

    if (!state.mouse.captured || mouseCapture_->WasCapturedThisFrame()) {
        return state;
    }

    const KamataEngine::Input::MouseMove mouseMove = input->GetMouseMove();
    state.mouse.deltaX = static_cast<float>(mouseMove.lX);
    state.mouse.deltaY = static_cast<float>(mouseMove.lY);
    return state;
}

void InputSystem::SetMouseCaptureEnabled(const bool enabled) noexcept {
    if (mouseCapture_) {
        mouseCapture_->SetCaptureEnabled(enabled);
    }
}

void InputSystem::Finalize() noexcept {
    mouseCapture_.reset();
}

bool InputSystem::IsInitialized() const noexcept {
    return mouseCapture_ != nullptr;
}

bool InputSystem::IsMouseCaptureEnabled() const noexcept {
    return mouseCapture_ && mouseCapture_->IsCaptureEnabled();
}

} // namespace fps
