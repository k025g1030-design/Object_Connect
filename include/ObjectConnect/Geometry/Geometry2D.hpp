#pragma once

#include "ObjectConnect/Math/Vec2.hpp"

#include <optional>
#include <span>

namespace object_connect {

struct AxisAlignedBox final {
    Vec2 minimum{};
    Vec2 maximum{};
};

[[nodiscard]] bool IsValidAxisAlignedBox(const AxisAlignedBox& box) noexcept;
[[nodiscard]] bool PointInAxisAlignedBox(Vec2 point,
                                         const AxisAlignedBox& box) noexcept;
[[nodiscard]] AxisAlignedBox ExpandAxisAlignedBox(const AxisAlignedBox& box,
                                                  float amount) noexcept;
[[nodiscard]] bool SegmentIntersectsAxisAlignedBox(
    Vec2 start, Vec2 end, const AxisAlignedBox& box) noexcept;
[[nodiscard]] std::optional<float> SegmentAxisAlignedBoxEntryTime(
    Vec2 start, Vec2 end, const AxisAlignedBox& box) noexcept;
[[nodiscard]] bool SegmentIntersectsAnyAxisAlignedBox(
    Vec2 start, Vec2 end, std::span<const AxisAlignedBox> boxes,
    float clearance = 0.0f) noexcept;

} // namespace object_connect
