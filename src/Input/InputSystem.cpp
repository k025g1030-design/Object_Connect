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
    KamataEngine::Input* const input = KamataEngine::Input::GetInstance();
    state.keyboard.escapePressed = input->TriggerKey(static_cast<BYTE>(DIK_ESCAPE));
    state.mouse.captured = mouseCapture_->IsCaptured();
    if (!state.mouse.captured) {
        return state;
    }

    state.keyboard.w = input->PushKey(static_cast<BYTE>(DIK_W));
    state.keyboard.a = input->PushKey(static_cast<BYTE>(DIK_A));
    state.keyboard.s = input->PushKey(static_cast<BYTE>(DIK_S));
    state.keyboard.d = input->PushKey(static_cast<BYTE>(DIK_D));

    const KamataEngine::Input::MouseMove mouseMove = input->GetMouseMove();
    state.mouse.deltaX = static_cast<float>(mouseMove.lX);
    state.mouse.deltaY = static_cast<float>(mouseMove.lY);
    return state;
}

void InputSystem::Finalize() noexcept {
    mouseCapture_.reset();
}

bool InputSystem::IsInitialized() const noexcept {
    return mouseCapture_ != nullptr;
}

} // namespace fps
