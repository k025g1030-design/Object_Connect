#pragma once

namespace fps {

struct KeyboardState final {
    bool w = false;
    bool a = false;
    bool s = false;
    bool d = false;
    bool wPressed = false;
    bool sPressed = false;
    bool upPressed = false;
    bool downPressed = false;
    bool rPressed = false;
    bool enterPressed = false;
    bool escapePressed = false;
};

struct MouseState final {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    bool leftHeld = false;
    bool leftPressed = false;
    bool captured = false;
};

struct InputState final {
    KeyboardState keyboard{};
    MouseState mouse{};
    bool windowFocused = false;
};

} // namespace fps
