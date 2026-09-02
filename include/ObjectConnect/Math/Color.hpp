#pragma once

#include <algorithm>

namespace object_connect {

struct Color final {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    [[nodiscard]] bool operator==(const Color&) const noexcept = default;
};

[[nodiscard]] constexpr Color WithAlpha(const Color color, const float alpha) noexcept {
    return {color.r, color.g, color.b, alpha};
}

[[nodiscard]] constexpr Color ScaleRgb(const Color color, const float scale) noexcept {
    return {
        std::clamp(color.r * scale, 0.0f, 1.0f),
        std::clamp(color.g * scale, 0.0f, 1.0f),
        std::clamp(color.b * scale, 0.0f, 1.0f),
        color.a,
    };
}

} // namespace object_connect
