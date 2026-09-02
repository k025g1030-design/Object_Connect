#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace object_connect::rendering_detail {

// KamataEngine exposes process-wide texture handles without reference counts.
// Runtime renderers share this small registry so one owner cannot unload a
// texture while another owner still has a Sprite that uses it.
class TextureHandleRegistry final {
public:
    [[nodiscard]] static std::uint32_t Acquire(const std::string& path);
    static void Release(const std::string& path) noexcept;
};

} // namespace object_connect::rendering_detail
