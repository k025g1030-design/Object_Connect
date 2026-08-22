#pragma once

#include "RetroFPS/Input/InputState.hpp"

#include <memory>
#include <string>

namespace fps {

class MouseCapture;

// 物理キーボードとマウスの状態を取得するプラットフォームアダプター。
// 各キーにゲーム上の意味は割り当てない。
class InputSystem final {
public:
    InputSystem() noexcept;
    ~InputSystem();

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;
    InputSystem(InputSystem&&) = delete;
    InputSystem& operator=(InputSystem&&) = delete;

    [[nodiscard]] bool Initialize(std::string& error);
    [[nodiscard]] InputState Sample() noexcept;
    void Finalize() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    std::unique_ptr<MouseCapture> mouseCapture_;
};

} // namespace fps
