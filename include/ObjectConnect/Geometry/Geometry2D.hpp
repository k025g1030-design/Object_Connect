#pragma once

#include "ObjectConnect/Math/Vec2.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace object_connect {

struct AxisAlignedBox final {
    Vec2 minimum{};
    Vec2 maximum{};
};

inline constexpr std::size_t kMaximumWrappedPathCrossings = 256;

struct WrapWinding final {
    std::int64_t x = 0;
    std::int64_t y = 0;

    [[nodiscard]] bool operator==(const WrapWinding&) const noexcept = default;
};

struct CanonicalWrappedPoint final {
    Vec2 position{};
    WrapWinding winding{};
    bool valid = false;
};

struct WrappedPathSegment final {
    Vec2 start{};
    Vec2 end{};
    Vec2 unwrappedStart{};
    Vec2 unwrappedEnd{};
};

struct WrappedPath final {
    std::vector<WrappedPathSegment> visibleSegments;
    std::vector<Vec2> drawTranslations;
    Vec2 unwrappedEnd{};
    Vec2 canonicalEnd{};
    WrapWinding endWinding{};
    float length = 0.0f;
};

[[nodiscard]] bool IsValidAxisAlignedBox(const AxisAlignedBox& box) noexcept;
[[nodiscard]] CanonicalWrappedPoint CanonicalizeWrappedPoint(
    Vec2 point, const AxisAlignedBox& bounds) noexcept;
[[nodiscard]] std::optional<WrappedPath> BuildWrappedPath(
    Vec2 start, Vec2 end, const AxisAlignedBox& bounds,
    bool wrapEdges = true) noexcept;
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
