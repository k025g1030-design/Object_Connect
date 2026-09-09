#pragma once

#include <atomic>
#include <cstdint>

namespace object_connect::text_detail {

// Process-wide use prevents a stale handle from aliasing a later font even if
// a FontSystem is finalized/reinitialized or more than one instance exists.
class MonotonicFontIdSource final {
public:
    explicit MonotonicFontIdSource(
        const std::uint32_t firstId = 1) noexcept
        : next_(firstId) {}

    [[nodiscard]] std::uint32_t Allocate() noexcept {
        std::uint32_t current = next_.load(std::memory_order_relaxed);
        while (current != 0) {
            const std::uint32_t next = current + 1u;
            if (next_.compare_exchange_weak(
                    current, next, std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                return current;
            }
        }
        return 0;
    }

private:
    std::atomic<std::uint32_t> next_;
};

} // namespace object_connect::text_detail
