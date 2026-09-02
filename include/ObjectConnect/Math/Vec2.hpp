#pragma once

#include <cmath>

namespace object_connect {

struct Vec2 final {
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] bool operator==(const Vec2&) const noexcept = default;
};

[[nodiscard]] constexpr Vec2 operator+(const Vec2 left, const Vec2 right) noexcept {
    return {left.x + right.x, left.y + right.y};
}

[[nodiscard]] constexpr Vec2 operator-(const Vec2 left, const Vec2 right) noexcept {
    return {left.x - right.x, left.y - right.y};
}

[[nodiscard]] constexpr Vec2 operator-(const Vec2 value) noexcept {
    return {-value.x, -value.y};
}

[[nodiscard]] constexpr Vec2 operator*(const Vec2 value, const float scalar) noexcept {
    return {value.x * scalar, value.y * scalar};
}

[[nodiscard]] constexpr Vec2 operator*(const float scalar, const Vec2 value) noexcept {
    return value * scalar;
}

[[nodiscard]] constexpr Vec2 operator/(const Vec2 value, const float scalar) noexcept {
    return {value.x / scalar, value.y / scalar};
}

constexpr Vec2& operator+=(Vec2& left, const Vec2 right) noexcept {
    left.x += right.x;
    left.y += right.y;
    return left;
}

constexpr Vec2& operator-=(Vec2& left, const Vec2 right) noexcept {
    left.x -= right.x;
    left.y -= right.y;
    return left;
}

constexpr Vec2& operator*=(Vec2& value, const float scalar) noexcept {
    value.x *= scalar;
    value.y *= scalar;
    return value;
}

[[nodiscard]] constexpr float Dot(const Vec2 left, const Vec2 right) noexcept {
    return left.x * right.x + left.y * right.y;
}

[[nodiscard]] constexpr float LengthSquared(const Vec2 value) noexcept {
    return Dot(value, value);
}

[[nodiscard]] inline float Length(const Vec2 value) noexcept {
    return std::sqrt(LengthSquared(value));
}

[[nodiscard]] inline Vec2 NormalizeOr(const Vec2 value, const Vec2 fallback) noexcept {
    const float length = Length(value);
    return std::isfinite(length) && length > 0.00001f ? value / length : fallback;
}

[[nodiscard]] constexpr Vec2 Perpendicular(const Vec2 value) noexcept {
    return {-value.y, value.x};
}

[[nodiscard]] inline bool IsFinite(const Vec2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

} // namespace object_connect
