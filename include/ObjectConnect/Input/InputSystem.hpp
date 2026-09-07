#pragma once

#include "ObjectConnect/Geometry/Geometry2D.hpp"
#include "ObjectConnect/Input/InputState.hpp"

#include <string>

namespace object_connect {

class InputSystem final {
public:
    [[nodiscard]] bool Initialize(std::string& error) noexcept;
    [[nodiscard]] InputState Sample() noexcept;
    [[nodiscard]] InputState Sample(const AxisAlignedBox& playfieldBounds,
                                    bool wrapPointer) noexcept;
    void ResetPointerWrap() noexcept;
    void Finalize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
    bool previousLeftHeld_ = false;
    bool previousWindowFocused_ = false;
    bool pointerWrapTracking_ = false;
    Vec2 unwrappedPointerPosition_{};
    Vec2 previousClientPointerPosition_{};
    bool suppressRelativeMouseX_ = false;
    bool suppressRelativeMouseY_ = false;
    WrapWinding pointerWinding_{};
};

} // namespace object_connect
