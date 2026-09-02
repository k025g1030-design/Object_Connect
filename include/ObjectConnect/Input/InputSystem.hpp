#pragma once

#include "ObjectConnect/Input/InputState.hpp"

#include <string>

namespace object_connect {

class InputSystem final {
public:
    [[nodiscard]] bool Initialize(std::string& error) noexcept;
    [[nodiscard]] InputState Sample() noexcept;
    void Finalize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
    bool previousLeftHeld_ = false;
    bool previousWindowFocused_ = false;
    bool hasPreviousMousePosition_ = false;
    float previousMouseX_ = 0.0f;
    float previousMouseY_ = 0.0f;
};

} // namespace object_connect
