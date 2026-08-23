#pragma once

namespace fps {

struct PlayerDamageResult final {
    bool applied = false;
    bool killed = false;
    float rawDamage = 0.0f;
    float appliedDamage = 0.0f;
    float remainingHealth = 100.0f;
};

// Engine-independent player health state. A new run starts at the fixed MVP
// maximum of 100 HP; level/session ownership decides whether the same instance
// persists across room changes.
class PlayerCombatState final {
public:
    static constexpr float kMaximumHealth = 100.0f;

    PlayerCombatState() noexcept = default;

    void Reset() noexcept;
    [[nodiscard]] PlayerDamageResult ApplyDamage(float damage) noexcept;

    [[nodiscard]] float GetHealth() const noexcept { return health_; }
    [[nodiscard]] float GetMaximumHealth() const noexcept { return kMaximumHealth; }
    [[nodiscard]] bool IsDead() const noexcept { return health_ <= 0.0f; }

private:
    float health_ = kMaximumHealth;
};

} // namespace fps
