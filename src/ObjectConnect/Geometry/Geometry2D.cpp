#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace object_connect {
namespace {

[[nodiscard]] bool IsFiniteScalar(const float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool HasValidCircle(const Vec2 center, const float radius) noexcept {
    return IsFinite(center) && IsFiniteScalar(radius) && radius >= 0.0f;
}

[[nodiscard]] bool HasValidRectangle(const Vec2 center, const float width,
                                     const float height) noexcept {
    return IsFinite(center) && IsFiniteScalar(width) && IsFiniteScalar(height) &&
           width >= 0.0f && height >= 0.0f;
}

} // namespace

bool PointInCircle(const Vec2 point, const Vec2 center, const float radius) noexcept {
    if (!IsFinite(point) || !HasValidCircle(center, radius)) {
        return false;
    }
    const Vec2 offset = point - center;
    return static_cast<double>(offset.x) * offset.x +
               static_cast<double>(offset.y) * offset.y <=
           static_cast<double>(radius) * radius;
}

bool CircleOverlapsRectangle(const Vec2 circleCenter, const float circleRadius,
                             const Vec2 rectangleCenter, const float width,
                             const float height) noexcept {
    if (!HasValidCircle(circleCenter, circleRadius) ||
        !HasValidRectangle(rectangleCenter, width, height)) {
        return false;
    }

    const double halfWidth = static_cast<double>(width) * 0.5;
    const double halfHeight = static_cast<double>(height) * 0.5;
    const double closestX = std::clamp(static_cast<double>(circleCenter.x),
                                       static_cast<double>(rectangleCenter.x) - halfWidth,
                                       static_cast<double>(rectangleCenter.x) + halfWidth);
    const double closestY = std::clamp(static_cast<double>(circleCenter.y),
                                       static_cast<double>(rectangleCenter.y) - halfHeight,
                                       static_cast<double>(rectangleCenter.y) + halfHeight);
    const double deltaX = static_cast<double>(circleCenter.x) - closestX;
    const double deltaY = static_cast<double>(circleCenter.y) - closestY;
    return deltaX * deltaX + deltaY * deltaY <=
           static_cast<double>(circleRadius) * circleRadius;
}

bool SegmentIntersectsCircle(const Vec2 start, const Vec2 end, const Vec2 center,
                             const float radius) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !HasValidCircle(center, radius)) {
        return false;
    }

    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaY = static_cast<double>(end.y) - start.y;
    const double lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (lengthSquared <= static_cast<double>((std::numeric_limits<float>::epsilon)())) {
        return PointInCircle(start, center, radius);
    }

    const double centerX = static_cast<double>(center.x) - start.x;
    const double centerY = static_cast<double>(center.y) - start.y;
    const double time = std::clamp((centerX * deltaX + centerY * deltaY) / lengthSquared,
                                   0.0, 1.0);
    const double closestX = static_cast<double>(start.x) + deltaX * time;
    const double closestY = static_cast<double>(start.y) + deltaY * time;
    const double offsetX = static_cast<double>(center.x) - closestX;
    const double offsetY = static_cast<double>(center.y) - closestY;
    return offsetX * offsetX + offsetY * offsetY <=
           static_cast<double>(radius) * radius;
}

bool SegmentIntersectsRectangle(const Vec2 start, const Vec2 end, const Vec2 center,
                                const float width, const float height) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !HasValidRectangle(center, width, height)) {
        return false;
    }

    const double minimumX = static_cast<double>(center.x) - width * 0.5;
    const double maximumX = static_cast<double>(center.x) + width * 0.5;
    const double minimumY = static_cast<double>(center.y) - height * 0.5;
    const double maximumY = static_cast<double>(center.y) + height * 0.5;
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaY = static_cast<double>(end.y) - start.y;
    double minimumTime = 0.0;
    double maximumTime = 1.0;

    const auto clipAxis = [&minimumTime, &maximumTime](const double origin,
                                                       const double delta,
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
        minimumTime = (std::max)(minimumTime, entry);
        maximumTime = (std::min)(maximumTime, exit);
        return minimumTime <= maximumTime;
    };

    return clipAxis(start.x, deltaX, minimumX, maximumX) &&
           clipAxis(start.y, deltaY, minimumY, maximumY);
}

bool IsConnectionBlocked(const Vec2 start, const Vec2 end,
                         const std::span<const ObstacleDefinition> obstacles,
                         const float clearance) noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !IsFiniteScalar(clearance) || clearance < 0.0f) {
        return true;
    }

    for (const ObstacleDefinition& obstacle : obstacles) {
        switch (obstacle.shape) {
        case ObstacleShape::Rectangle: {
            const float expandedWidth = obstacle.width + clearance * 2.0f;
            const float expandedHeight = obstacle.height + clearance * 2.0f;
            if (!HasValidRectangle(obstacle.center, expandedWidth, expandedHeight) ||
                SegmentIntersectsRectangle(start, end, obstacle.center, expandedWidth,
                                           expandedHeight)) {
                return true;
            }
            break;
        }
        case ObstacleShape::Circle: {
            const float expandedRadius = obstacle.radius + clearance;
            if (!HasValidCircle(obstacle.center, expandedRadius) ||
                SegmentIntersectsCircle(start, end, obstacle.center, expandedRadius)) {
                return true;
            }
            break;
        }
        default:
            return true;
        }
    }
    return false;
}

} // namespace object_connect
