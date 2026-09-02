#include "ObjectConnect/Core/FrameTimer.hpp"

#include <algorithm>

namespace object_connect {

FrameTimer::FrameTimer() noexcept
    : previousTime_(Clock::now()) {}

void FrameTimer::Reset() noexcept { previousTime_ = Clock::now(); }

float FrameTimer::Tick() noexcept {
    const Clock::time_point now = Clock::now();
    const std::chrono::duration<float> elapsed = now - previousTime_;
    previousTime_ = now;
    return std::clamp(elapsed.count(), 0.0f, kMaxDeltaSeconds);
}

} // namespace object_connect
