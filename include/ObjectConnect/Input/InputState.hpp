#pragma once

#include "ObjectConnect/Math/Vec2.hpp"

#include <cstdint>
#include <optional>

namespace object_connect {

struct KeyboardState final {
    bool previousPressed = false;
    bool nextPressed = false;
    bool enterPressed = false;
    bool escapePressed = false;
    bool retryPressed = false;
};

struct MouseState final {
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::optional<Vec2> unwrappedPosition;
    std::int32_t wheelDelta = 0;
    bool leftHeld = false;
    bool leftPressed = false;
    bool leftReleased = false;
};

struct InputState final {
    KeyboardState keyboard{};
    MouseState mouse{};
    bool windowFocused = false;
    bool focusLost = false;
};

} // namespace object_connect
