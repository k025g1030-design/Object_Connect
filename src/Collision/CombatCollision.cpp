#include "RetroFPS/Collision/CombatCollision.hpp"

#include "RetroFPS/World/GridMap.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace fps {
namespace {

constexpr float kEpsilon = 0.000001f;

[[nodiscard]] bool IsFinite(const Float3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float Length(const Float3 value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

[[nodiscard]] Float3 AddScaled(
    const Float3 origin, const Float3 direction, const float distance) noexcept {
    return {
        origin.x + direction.x * distance,
        origin.y + direction.y * distance,
        origin.z + direction.z * distance,
    };
}

void ValidateQuery(
    const Float3 origin,
    const Float3 direction,
    const float maximumDistance,
    const float sweepRadius) {
    if (!IsFinite(origin) || !IsFinite(direction)) {
        throw std::invalid_argument("combat query vectors must be finite");
    }
    if (!std::isfinite(maximumDistance) || maximumDistance < 0.0f) {
        throw std::invalid_argument("combat query distance must be finite and non-negative");
    }
    if (!std::isfinite(sweepRadius) || sweepRadius < 0.0f) {
        throw std::invalid_argument("combat query sweep radius must be finite and non-negative");
    }
    const float directionLength = Length(direction);
    if (!std::isfinite(directionLength) || directionLength <= kEpsilon) {
        throw std::invalid_argument("combat query direction must be non-zero");
    }
}

void ValidateCapsule(const VerticalCapsule& capsule) {
    if (!std::isfinite(capsule.centerXZ.x) || !std::isfinite(capsule.centerXZ.z) ||
        !std::isfinite(capsule.height) || !std::isfinite(capsule.radius) ||
        capsule.radius <= 0.0f || capsule.height < capsule.radius * 2.0f) {
        throw std::invalid_argument(
            "combat capsule must be finite, positive, and at least two radii high");
    }
}

[[nodiscard]] Float3 Normalize(const Float3 value) noexcept {
    const float length = Length(value);
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] std::optional<float> RaySphere(
    const Float3 origin,
    const Float3 direction,
    const float maximumDistance,
    const Float3 center,
    const float radius) noexcept {
    const Float3 offset{origin.x - center.x, origin.y - center.y, origin.z - center.z};
    const float halfB = offset.x * direction.x + offset.y * direction.y +
                        offset.z * direction.z;
    const float c = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z -
                    radius * radius;
    const float discriminant = halfB * halfB - c;
    if (discriminant < 0.0f) {
        return std::nullopt;
    }

    const float root = std::sqrt((std::max)(0.0f, discriminant));
    float distance = -halfB - root;
    if (distance < 0.0f) {
        distance = -halfB + root;
    }
    if (distance < 0.0f || distance > maximumDistance) {
        return std::nullopt;
    }
    return distance;
}

[[nodiscard]] std::optional<float> RayAabb(
    const Float3 origin,
    const Float3 direction,
    const float maximumDistance,
    const Float3 minimum,
    const Float3 maximum) noexcept {
    float entry = 0.0f;
    float exit = maximumDistance;
    const std::array<float, 3> origins{origin.x, origin.y, origin.z};
    const std::array<float, 3> directions{direction.x, direction.y, direction.z};
    const std::array<float, 3> minima{minimum.x, minimum.y, minimum.z};
    const std::array<float, 3> maxima{maximum.x, maximum.y, maximum.z};

    for (std::size_t axis = 0; axis < origins.size(); ++axis) {
        if (std::fabs(directions[axis]) <= kEpsilon) {
            if (origins[axis] < minima[axis] || origins[axis] > maxima[axis]) {
                return std::nullopt;
            }
            continue;
        }

        float first = (minima[axis] - origins[axis]) / directions[axis];
        float second = (maxima[axis] - origins[axis]) / directions[axis];
        if (first > second) {
            std::swap(first, second);
        }
        entry = (std::max)(entry, first);
        exit = (std::min)(exit, second);
        if (entry > exit) {
            return std::nullopt;
        }
    }

    if (exit < 0.0f || entry > maximumDistance) {
        return std::nullopt;
    }
    return (std::max)(0.0f, entry);
}

[[nodiscard]] std::optional<float> RayCapsuleUnchecked(
    const Float3 origin,
    const Float3 direction,
    const float maximumDistance,
    const VerticalCapsule& capsule,
    const float sweepRadius) noexcept {
    const float radius = capsule.radius + sweepRadius;
    const float segmentBottom = capsule.radius;
    const float segmentTop = capsule.height - capsule.radius;
    const float closestHeight = std::clamp(origin.y, segmentBottom, segmentTop);
    const float overlapX = origin.x - capsule.centerXZ.x;
    const float overlapY = origin.y - closestHeight;
    const float overlapZ = origin.z - capsule.centerXZ.z;
    if (overlapX * overlapX + overlapY * overlapY + overlapZ * overlapZ <=
        radius * radius) {
        return 0.0f;
    }
    float closest = (std::numeric_limits<float>::max)();

    const float offsetX = origin.x - capsule.centerXZ.x;
    const float offsetZ = origin.z - capsule.centerXZ.z;
    const float a = direction.x * direction.x + direction.z * direction.z;
    if (a > kEpsilon) {
        const float halfB = offsetX * direction.x + offsetZ * direction.z;
        const float c = offsetX * offsetX + offsetZ * offsetZ - radius * radius;
        const float discriminant = halfB * halfB - a * c;
        if (discriminant >= 0.0f) {
            const float root = std::sqrt((std::max)(0.0f, discriminant));
            const std::array<float, 2> roots{
                (-halfB - root) / a,
                (-halfB + root) / a,
            };
            for (const float distance : roots) {
                if (distance < 0.0f || distance > maximumDistance) {
                    continue;
                }
                const float height = origin.y + direction.y * distance;
                if (height >= segmentBottom && height <= segmentTop) {
                    closest = (std::min)(closest, distance);
                }
            }
        }
    }

    const std::array<Float3, 2> ends{{
        {capsule.centerXZ.x, segmentBottom, capsule.centerXZ.z},
        {capsule.centerXZ.x, segmentTop, capsule.centerXZ.z},
    }};
    for (const Float3 end : ends) {
        const std::optional<float> hit =
            RaySphere(origin, direction, maximumDistance, end, radius);
        if (hit.has_value()) {
            closest = (std::min)(closest, *hit);
        }
    }

    if (closest == (std::numeric_limits<float>::max)()) {
        return std::nullopt;
    }
    return closest;
}

} // namespace

std::optional<CombatHit> CombatCollision::Raycast(
    const GridMap& map,
    const WorldSettings& worldSettings,
    const Float3 origin,
    const Float3 direction,
    const float maximumDistance,
    const std::span<const CombatTarget> targets,
    const float sweepRadius) {
    ValidateQuery(origin, direction, maximumDistance, sweepRadius);
    if (!std::isfinite(worldSettings.cellSize) || worldSettings.cellSize <= 0.0f ||
        !std::isfinite(worldSettings.wallHeight) || worldSettings.wallHeight <= 0.0f) {
        throw std::invalid_argument("combat query world settings must be finite and positive");
    }
    const Float3 normalized = Normalize(direction);

    std::optional<CombatHit> closest;
    const auto consider = [&closest, origin, normalized](
                              const CombatHitKind kind,
                              const float distance,
                              const CombatTargetId targetId = 0) {
        if (!closest.has_value() || distance < closest->distance) {
            closest = CombatHit{kind, AddScaled(origin, normalized, distance), distance, targetId};
        }
    };

    for (std::size_t row = 0; row < map.GetHeight(); ++row) {
        for (std::size_t column = 0; column < map.GetWidth(); ++column) {
            if (!map.IsSolid(static_cast<std::ptrdiff_t>(row),
                             static_cast<std::ptrdiff_t>(column))) {
                continue;
            }
            const float minimumX = static_cast<float>(column) * worldSettings.cellSize - sweepRadius;
            const float minimumZ = static_cast<float>(row) * worldSettings.cellSize - sweepRadius;
            const Float3 minimum{minimumX, -sweepRadius, minimumZ};
            const Float3 maximum{
                static_cast<float>(column + 1) * worldSettings.cellSize + sweepRadius,
                worldSettings.wallHeight + sweepRadius,
                static_cast<float>(row + 1) * worldSettings.cellSize + sweepRadius,
            };
            const std::optional<float> distance =
                RayAabb(origin, normalized, maximumDistance, minimum, maximum);
            if (distance.has_value()) {
                consider(CombatHitKind::Wall, *distance);
            }
        }
    }

    if (normalized.y < -kEpsilon && origin.y >= sweepRadius) {
        const float floorDistance = (sweepRadius - origin.y) / normalized.y;
        if (floorDistance >= 0.0f && floorDistance <= maximumDistance) {
            consider(CombatHitKind::Floor, floorDistance);
        }
    }

    for (const CombatTarget& target : targets) {
        ValidateCapsule(target.capsule);
        const std::optional<float> distance = RayCapsuleUnchecked(
            origin, normalized, maximumDistance, target.capsule, sweepRadius);
        if (distance.has_value()) {
            consider(CombatHitKind::Target, *distance, target.id);
        }
    }
    return closest;
}

std::optional<float> CombatCollision::RaycastCapsule(
    const Float3 origin,
    const Float3 direction,
    const float maximumDistance,
    const VerticalCapsule& capsule,
    const float sweepRadius) {
    ValidateQuery(origin, direction, maximumDistance, sweepRadius);
    ValidateCapsule(capsule);
    return RayCapsuleUnchecked(
        origin, Normalize(direction), maximumDistance, capsule, sweepRadius);
}

std::optional<float> CombatCollision::SweepSegmentAgainstCapsule(
    const Float3 start,
    const Float3 end,
    const float sweepRadius,
    const VerticalCapsule& capsule) {
    if (!IsFinite(start) || !IsFinite(end)) {
        throw std::invalid_argument("combat sweep endpoints must be finite");
    }
    if (!std::isfinite(sweepRadius) || sweepRadius < 0.0f) {
        throw std::invalid_argument(
            "combat sweep radius must be finite and non-negative");
    }
    ValidateCapsule(capsule);
    const Float3 delta{end.x - start.x, end.y - start.y, end.z - start.z};
    const float length = Length(delta);
    if (length <= kEpsilon) {
        const Float3 capsuleCenter{
            capsule.centerXZ.x,
            (std::clamp)(start.y, capsule.radius, capsule.height - capsule.radius),
            capsule.centerXZ.z,
        };
        const float dx = start.x - capsuleCenter.x;
        const float dy = start.y - capsuleCenter.y;
        const float dz = start.z - capsuleCenter.z;
        const float radius = capsule.radius + sweepRadius;
        return dx * dx + dy * dy + dz * dz <= radius * radius
                   ? std::optional<float>{0.0f}
                   : std::nullopt;
    }
    const std::optional<float> distance =
        RaycastCapsule(start, delta, length, capsule, sweepRadius);
    return distance.has_value() ? std::optional<float>{*distance / length} : std::nullopt;
}

} // namespace fps
