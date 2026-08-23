#include "RetroFPS/Gameplay/Player/PlayerCombatState.hpp"

#include <algorithm>
#include <cmath>

namespace fps {

void PlayerCombatState::Reset() noexcept { health_ = kMaximumHealth; }

PlayerDamageResult PlayerCombatState::ApplyDamage(const float damage) noexcept {
    PlayerDamageResult result{};
    result.rawDamage = damage;
    result.remainingHealth = health_;

    if (!std::isfinite(damage) || damage <= 0.0f || IsDead()) {
        return result;
    }

    result.applied = true;
    result.appliedDamage = (std::min)(damage, health_);
    health_ = (std::max)(0.0f, health_ - result.appliedDamage);
    result.killed = health_ <= 0.0f;
    result.remainingHealth = health_;
    return result;
}

} // namespace fps
