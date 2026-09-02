#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace object_connect {
namespace {

[[nodiscard]] bool IsFiniteScalar(const float value) noexcept {
    return std::isfinite(value);
}

struct SegmentClipTimes final {
    double entry = 0.0;
    double exit = 1.0;
};

[[nodiscard]] std::optional<SegmentClipTimes> ClipSegmentToAxisAlignedBox(
    const Vec2 start, const Vec2 end, const AxisAlignedBox& box) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !IsValidAxisAlignedBox(box)) {
        return std::nullopt;
    }

    SegmentClipTimes times{};
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaY = static_cast<double>(end.y) - start.y;
    const auto clipAxis = [&times](const double origin, const double delta,
                                   const double minimum,
                                   const double maximum) noexcept {
        if (std::abs(delta) <= (std::numeric_limits<double>::epsilon)()) {
            return origin >= minimum && origin <= maximum;
        }

        double entry = (minimum - origin) / delta;
        double exit = (maximum - origin) / delta;
        if (entry > exit) {
            std::swap(entry, exit);
        }
        times.entry = (std::max)(times.entry, entry);
        times.exit = (std::min)(times.exit, exit);
        return times.entry <= times.exit;
    };

    if (!clipAxis(start.x, deltaX, box.minimum.x, box.maximum.x) ||
        !clipAxis(start.y, deltaY, box.minimum.y, box.maximum.y)) {
        return std::nullopt;
    }
    return times;
}

} // namespace

bool IsValidAxisAlignedBox(const AxisAlignedBox& box) noexcept {
    return IsFinite(box.minimum) && IsFinite(box.maximum) &&
           box.minimum.x <= box.maximum.x && box.minimum.y <= box.maximum.y;
}

bool PointInAxisAlignedBox(const Vec2 point,
                           const AxisAlignedBox& box) noexcept {
    return IsFinite(point) && IsValidAxisAlignedBox(box) &&
           point.x >= box.minimum.x && point.x <= box.maximum.x &&
           point.y >= box.minimum.y && point.y <= box.maximum.y;
}

AxisAlignedBox ExpandAxisAlignedBox(const AxisAlignedBox& box,
                                    const float amount) noexcept {
    if (!IsValidAxisAlignedBox(box) || !IsFiniteScalar(amount) || amount < 0.0f) {
        const float nan = (std::numeric_limits<float>::quiet_NaN)();
        return {{nan, nan}, {nan, nan}};
    }
    return {
        {box.minimum.x - amount, box.minimum.y - amount},
        {box.maximum.x + amount, box.maximum.y + amount},
    };
}

bool SegmentIntersectsAxisAlignedBox(const Vec2 start, const Vec2 end,
                                     const AxisAlignedBox& box) noexcept {
    return ClipSegmentToAxisAlignedBox(start, end, box).has_value();
}

std::optional<float> SegmentAxisAlignedBoxEntryTime(
    const Vec2 start, const Vec2 end, const AxisAlignedBox& box) noexcept {
    const std::optional<SegmentClipTimes> times =
        ClipSegmentToAxisAlignedBox(start, end, box);
    if (!times.has_value()) {
        return std::nullopt;
    }
    return static_cast<float>(std::clamp(times->entry, 0.0, 1.0));
}

bool SegmentIntersectsAnyAxisAlignedBox(
    const Vec2 start, const Vec2 end, const std::span<const AxisAlignedBox> boxes,
    const float clearance) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !IsFiniteScalar(clearance) ||
        clearance < 0.0f) {
        return true;
    }

    for (const AxisAlignedBox& box : boxes) {
        const AxisAlignedBox expanded = ExpandAxisAlignedBox(box, clearance);
        if (!IsValidAxisAlignedBox(expanded) ||
            SegmentIntersectsAxisAlignedBox(start, end, expanded)) {
            return true;
        }
    }
    return false;
}

} // namespace object_connect
