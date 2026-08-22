#pragma once

namespace fps {

struct KeyboardState final {
    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
    bool escapePressed = false;
};

struct MouseState final {
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    bool captured = false;
};

struct InputState final {
    KeyboardState keyboard{};
    MouseState mouse{};
};

} // namespace fps
