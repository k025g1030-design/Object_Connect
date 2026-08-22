#pragma once

#include <chrono>

namespace fps {

class FrameTimer final {
public:
    FrameTimer() noexcept;

    void Reset() noexcept;
    [[nodiscard]] float Tick() noexcept;

    static constexpr float kMaxDeltaSeconds = 0.05f;

private:
    using Clock = std::chrono::steady_clock;

    Clock::time_point previousTime_;
};

} // namespace fps
