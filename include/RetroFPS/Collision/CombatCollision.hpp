#pragma once

#include "RetroFPS/Math/Vector.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace fps {

class GridMap;
struct WorldSettings;

using CombatTargetId = std::uint64_t;

struct VerticalCapsule final {
    Float2 centerXZ{};
    float height = 0.0f;
    float radius = 0.0f;
};

struct CombatTarget final {
    CombatTargetId id = 0;
    VerticalCapsule capsule{};
};

enum class CombatHitKind {
    Wall,
    Floor,
    Target,
};

struct CombatHit final {
    CombatHitKind kind = CombatHitKind::Wall;
    Float3 position{};
    float distance = 0.0f;
    CombatTargetId targetId = 0;
};

// Engine-independent 3D queries used by weapon hitscan and swept projectiles.
// Grid movement remains in GridCollision; this class treats every solid tile as
// a wall-height AABB and characters as upright capsules.
class CombatCollision final {
public:
    [[nodiscard]] static std::optional<CombatHit> Raycast(
        const GridMap& map,
        const WorldSettings& worldSettings,
        Float3 origin,
        Float3 direction,
        float maximumDistance,
        std::span<const CombatTarget> targets = {},
        float sweepRadius = 0.0f);

    [[nodiscard]] static std::optional<float> RaycastCapsule(
        Float3 origin,
        Float3 direction,
        float maximumDistance,
        const VerticalCapsule& capsule,
        float sweepRadius = 0.0f);

    // Returns the normalized segment fraction [0, 1] of the first hit.
    [[nodiscard]] static std::optional<float> SweepSegmentAgainstCapsule(
        Float3 start,
        Float3 end,
        float sweepRadius,
        const VerticalCapsule& capsule);
};

} // namespace fps
