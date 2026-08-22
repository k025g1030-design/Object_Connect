#include "RetroFPS/Core/FrameTimer.hpp"

#include <algorithm>

namespace fps {

FrameTimer::FrameTimer() noexcept
    : previousTime_(Clock::now()) {}

void FrameTimer::Reset() noexcept {
    previousTime_ = Clock::now();
}

float FrameTimer::Tick() noexcept {
    const Clock::time_point currentTime = Clock::now();
    const std::chrono::duration<float> elapsed = currentTime - previousTime_;
    previousTime_ = currentTime;

    return std::clamp(elapsed.count(), 0.0f, kMaxDeltaSeconds);
}

} // namespace fps
