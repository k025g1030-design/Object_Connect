#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace object_connect {
namespace {

[[nodiscard]] bool IsFiniteScalar(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool HasPositiveArea(const AxisAlignedBox& box) noexcept {
    return IsValidAxisAlignedBox(box) && box.minimum.x < box.maximum.x &&
           box.minimum.y < box.maximum.y;
}

[[nodiscard]] std::optional<std::int64_t> FloorToInt64(
    const double value) noexcept {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    const double floored = std::floor(value);
    constexpr double minimum =
        static_cast<double>((std::numeric_limits<std::int64_t>::min)());
    constexpr double maximum =
        static_cast<double>((std::numeric_limits<std::int64_t>::max)());
    if (floored < minimum || floored >= maximum) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(floored);
}

[[nodiscard]] bool AppendDrawTranslation(
    std::vector<Vec2>& translations, const WrapWinding winding,
    const double width, const double height) {
    if (translations.size() > kMaximumWrappedPathCrossings) {
        return false;
    }
    const Vec2 translation{
        static_cast<float>(-static_cast<double>(winding.x) * width),
        static_cast<float>(-static_cast<double>(winding.y) * height),
    };
    if (!IsFinite(translation)) {
        return false;
    }
    translations.push_back(translation);
    return true;
}

[[nodiscard]] bool AppendWrappedSegment(
    WrappedPath& path, const Vec2 start, const Vec2 end,
    const Vec2 unwrappedStart, const Vec2 unwrappedEnd) {
    if (path.visibleSegments.size() > kMaximumWrappedPathCrossings) {
        return false;
    }
    path.visibleSegments.push_back({start, end, unwrappedStart, unwrappedEnd});
    const double deltaX =
        static_cast<double>(unwrappedEnd.x) - unwrappedStart.x;
    const double deltaY =
        static_cast<double>(unwrappedEnd.y) - unwrappedStart.y;
    const double segmentLength = std::hypot(deltaX, deltaY);
    const double nextLength = static_cast<double>(path.length) + segmentLength;
    if (!std::isfinite(nextLength) ||
        nextLength > static_cast<double>((std::numeric_limits<float>::max)())) {
        return false;
    }
    path.length = static_cast<float>(nextLength);
    return true;
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

CanonicalWrappedPoint CanonicalizeWrappedPoint(
    const Vec2 point, const AxisAlignedBox& bounds) noexcept {
    CanonicalWrappedPoint result{};
    if (!IsFinite(point) || !HasPositiveArea(bounds)) {
        return result;
    }

    const double width = static_cast<double>(bounds.maximum.x) - bounds.minimum.x;
    const double height = static_cast<double>(bounds.maximum.y) - bounds.minimum.y;
    const double relativeX =
        (static_cast<double>(point.x) - bounds.minimum.x) / width;
    const double relativeY =
        (static_cast<double>(point.y) - bounds.minimum.y) / height;
    const std::optional<std::int64_t> windingX = FloorToInt64(relativeX);
    const std::optional<std::int64_t> windingY = FloorToInt64(relativeY);
    if (!windingX.has_value() || !windingY.has_value()) {
        return result;
    }

    double canonicalX = static_cast<double>(point.x) -
                        static_cast<double>(*windingX) * width;
    double canonicalY = static_cast<double>(point.y) -
                        static_cast<double>(*windingY) * height;
    canonicalX = std::clamp(canonicalX, static_cast<double>(bounds.minimum.x),
                            static_cast<double>(bounds.maximum.x));
    canonicalY = std::clamp(canonicalY, static_cast<double>(bounds.minimum.y),
                            static_cast<double>(bounds.maximum.y));
    if (canonicalX >= bounds.maximum.x) {
        canonicalX = bounds.minimum.x;
    }
    if (canonicalY >= bounds.maximum.y) {
        canonicalY = bounds.minimum.y;
    }

    result.position = {static_cast<float>(canonicalX),
                       static_cast<float>(canonicalY)};
    result.winding = {*windingX, *windingY};
    result.valid = IsFinite(result.position);
    return result;
}

std::optional<WrappedPath> BuildWrappedPath(
    const Vec2 start, const Vec2 end, const AxisAlignedBox& bounds,
    const bool wrapEdges) noexcept {
    if (!IsFinite(start) || !IsFinite(end) ||
        (wrapEdges && !HasPositiveArea(bounds))) {
        return std::nullopt;
    }

    try {
        WrappedPath path{};
        path.unwrappedEnd = end;
        path.visibleSegments.reserve(4);
        path.drawTranslations.reserve(4);

        if (!wrapEdges) {
            path.canonicalEnd = end;
            if (!AppendDrawTranslation(path.drawTranslations, {}, 0.0, 0.0) ||
                !AppendWrappedSegment(path, start, end, start, end)) {
                return std::nullopt;
            }
            return path;
        }

        const CanonicalWrappedPoint canonicalStart =
            CanonicalizeWrappedPoint(start, bounds);
        const CanonicalWrappedPoint canonicalEnd =
            CanonicalizeWrappedPoint(end, bounds);
        if (!canonicalStart.valid || !canonicalEnd.valid) {
            return std::nullopt;
        }

        const double width =
            static_cast<double>(bounds.maximum.x) - bounds.minimum.x;
        const double height =
            static_cast<double>(bounds.maximum.y) - bounds.minimum.y;
        WrapWinding currentWinding = canonicalStart.winding;
        const WrapWinding finalWinding = canonicalEnd.winding;
        const long double horizontalCrossings = std::fabs(
            static_cast<long double>(finalWinding.x) -
            static_cast<long double>(currentWinding.x));
        const long double verticalCrossings = std::fabs(
            static_cast<long double>(finalWinding.y) -
            static_cast<long double>(currentWinding.y));
        if (horizontalCrossings > kMaximumWrappedPathCrossings ||
            verticalCrossings > kMaximumWrappedPathCrossings) {
            return std::nullopt;
        }

        if (!AppendDrawTranslation(path.drawTranslations, currentWinding,
                                   width, height)) {
            return std::nullopt;
        }

        const double deltaX = static_cast<double>(end.x) - start.x;
        const double deltaY = static_cast<double>(end.y) - start.y;
        const int stepX = deltaX > 0.0 ? 1 : (deltaX < 0.0 ? -1 : 0);
        const int stepY = deltaY > 0.0 ? 1 : (deltaY < 0.0 ? -1 : 0);
        constexpr double crossingEpsilon = 1.0e-10;
        std::size_t crossingCount = 0;
        double previousTime = 0.0;
        Vec2 canonicalSegmentStart = canonicalStart.position;
        Vec2 unwrappedSegmentStart = start;

        while (currentWinding != finalWinding) {
            if (crossingCount >= kMaximumWrappedPathCrossings) {
                return std::nullopt;
            }
            ++crossingCount;

            double crossingX = (std::numeric_limits<double>::infinity)();
            double crossingY = (std::numeric_limits<double>::infinity)();
            if (currentWinding.x != finalWinding.x && stepX != 0) {
                const double boundaryX = bounds.minimum.x +
                    static_cast<double>(currentWinding.x + (stepX > 0 ? 1 : 0)) *
                        width;
                crossingX = (boundaryX - start.x) / deltaX;
            }
            if (currentWinding.y != finalWinding.y && stepY != 0) {
                const double boundaryY = bounds.minimum.y +
                    static_cast<double>(currentWinding.y + (stepY > 0 ? 1 : 0)) *
                        height;
                crossingY = (boundaryY - start.y) / deltaY;
            }

            const double crossingTime = (std::min)(crossingX, crossingY);
            if (!std::isfinite(crossingTime) ||
                crossingTime + crossingEpsilon < previousTime ||
                crossingTime > 1.0 + crossingEpsilon) {
                return std::nullopt;
            }
            const double clampedTime = std::clamp(crossingTime, 0.0, 1.0);
            const Vec2 unwrappedCrossing{
                static_cast<float>(static_cast<double>(start.x) +
                                   deltaX * clampedTime),
                static_cast<float>(static_cast<double>(start.y) +
                                   deltaY * clampedTime),
            };
            Vec2 outgoingCrossing{
                static_cast<float>(static_cast<double>(unwrappedCrossing.x) -
                                   static_cast<double>(currentWinding.x) * width),
                static_cast<float>(static_cast<double>(unwrappedCrossing.y) -
                                   static_cast<double>(currentWinding.y) * height),
            };
            outgoingCrossing.x = std::clamp(
                outgoingCrossing.x, bounds.minimum.x, bounds.maximum.x);
            outgoingCrossing.y = std::clamp(
                outgoingCrossing.y, bounds.minimum.y, bounds.maximum.y);
            const bool crossesX =
                std::fabs(crossingX - crossingTime) <= crossingEpsilon;
            const bool crossesY =
                std::fabs(crossingY - crossingTime) <= crossingEpsilon;
            if (crossesX) {
                outgoingCrossing.x =
                    stepX > 0 ? bounds.maximum.x : bounds.minimum.x;
            }
            if (crossesY) {
                outgoingCrossing.y =
                    stepY > 0 ? bounds.maximum.y : bounds.minimum.y;
            }
            if (!IsFinite(unwrappedCrossing) || !IsFinite(outgoingCrossing)) {
                return std::nullopt;
            }
            // A point exactly on a half-open minimum can cross in the negative
            // direction at t=0. Advance the image without manufacturing a
            // zero-length visible segment at the portal.
            if (clampedTime > previousTime + crossingEpsilon &&
                !AppendWrappedSegment(path, canonicalSegmentStart,
                                      outgoingCrossing, unwrappedSegmentStart,
                                      unwrappedCrossing)) {
                return std::nullopt;
            }

            if (crossesX) {
                currentWinding.x += stepX;
            }
            if (crossesY) {
                currentWinding.y += stepY;
            }
            if (!AppendDrawTranslation(path.drawTranslations, currentWinding,
                                       width, height)) {
                return std::nullopt;
            }

            canonicalSegmentStart = outgoingCrossing;
            if (crossesX) {
                canonicalSegmentStart.x =
                    stepX > 0 ? bounds.minimum.x : bounds.maximum.x;
            }
            if (crossesY) {
                canonicalSegmentStart.y =
                    stepY > 0 ? bounds.minimum.y : bounds.maximum.y;
            }
            unwrappedSegmentStart = unwrappedCrossing;
            previousTime = clampedTime;
        }

        const bool hasRemainingSegment =
            previousTime < 1.0 - crossingEpsilon ||
            path.visibleSegments.empty();
        if (hasRemainingSegment &&
            !AppendWrappedSegment(path, canonicalSegmentStart,
                                  canonicalEnd.position, unwrappedSegmentStart,
                                  end)) {
            return std::nullopt;
        }
        path.canonicalEnd = canonicalEnd.position;
        path.endWinding = canonicalEnd.winding;
        return path;
    } catch (...) {
        return std::nullopt;
    }
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
