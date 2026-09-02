#include "TextureHandleRegistry.hpp"

#include <base/TextureManager.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace object_connect::rendering_detail {
namespace {

// Keep half of KamataEngine's 1024 descriptors available for DebugText, ImGui,
// bootstrap resources, and future runtime systems.
constexpr std::size_t kMaximumManagedTextures = 512;

struct TextureEntry final {
    std::uint32_t handle = 0;
    std::size_t referenceCount = 0;
};

[[nodiscard]] std::unordered_map<std::string, TextureEntry>& Entries() {
    static std::unordered_map<std::string, TextureEntry> entries;
    return entries;
}

[[nodiscard]] bool IsEngineOwnedTexture(const std::string_view path) noexcept {
    return path == "white1x1.png" || path == "debugfont.png";
}

} // namespace

std::uint32_t TextureHandleRegistry::Acquire(const std::string& path) {
    if (path.empty()) {
        throw std::runtime_error("Cannot acquire an empty texture path.");
    }

    auto& entries = Entries();
    const auto existing = entries.find(path);
    if (existing != entries.end()) {
        if (existing->second.referenceCount ==
            (std::numeric_limits<std::size_t>::max)()) {
            throw std::runtime_error("Texture reference count overflow for '" +
                                     path + "'.");
        }
        ++existing->second.referenceCount;
        return existing->second.handle;
    }
    if (entries.size() >= kMaximumManagedTextures) {
        throw std::runtime_error(
            "The runtime texture limit of 512 unique active paths was exceeded.");
    }

    const std::uint32_t handle = KamataEngine::TextureManager::Load(path);
    try {
        entries.emplace(path, TextureEntry{handle, 1});
    } catch (...) {
        if (!IsEngineOwnedTexture(path)) {
            static_cast<void>(KamataEngine::TextureManager::Unload(handle));
        }
        throw;
    }
    return handle;
}

void TextureHandleRegistry::Release(const std::string& path) noexcept {
    auto& entries = Entries();
    const auto found = entries.find(path);
    if (found == entries.end()) {
        return;
    }
    if (found->second.referenceCount == 0) {
        return;
    }
    if (found->second.referenceCount > 1) {
        --found->second.referenceCount;
        return;
    }

    const std::uint32_t handle = found->second.handle;
    const bool engineOwned = IsEngineOwnedTexture(found->first);
    if (engineOwned) {
        // KamataEngine bootstrap systems may keep sprites that use these
        // descriptors beyond our renderers' lifetime. Retain one process-wide
        // handle and let a later Acquire revive the registry reference count.
        found->second.referenceCount = 0;
        return;
    }
    entries.erase(found);
    static_cast<void>(KamataEngine::TextureManager::Unload(handle));
}

} // namespace object_connect::rendering_detail
