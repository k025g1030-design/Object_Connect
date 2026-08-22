#pragma once

#include "RetroFPS/World/GridMap.hpp"
#include "RetroFPS/World/WorldSettings.hpp"

#include <optional>

namespace fps {

class World final {
public:
    World() = default;
    explicit World(GridMap map, WorldSettings settings = {});

    void Initialize(GridMap map, WorldSettings settings = {});
    void Reset() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GridMap& GetMap() const;
    [[nodiscard]] const WorldSettings& GetSettings() const;

private:
    std::optional<GridMap> map_;
    std::optional<WorldSettings> settings_;
};

} // namespace fps
