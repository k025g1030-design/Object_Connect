#include "RetroFPS/World/World.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace fps {
namespace {

void ValidateSettings(const WorldSettings& settings) {
    if (!std::isfinite(settings.cellSize) || settings.cellSize <= 0.0f) {
        throw std::invalid_argument("cell size must be finite and greater than zero");
    }
    if (!std::isfinite(settings.wallHeight) || settings.wallHeight <= 0.0f) {
        throw std::invalid_argument("wall height must be finite and greater than zero");
    }
}

} // namespace

World::World(GridMap map, const WorldSettings settings) {
    Initialize(std::move(map), settings);
}

void World::Initialize(GridMap map, const WorldSettings settings) {
    ValidateSettings(settings);
    map_.emplace(std::move(map));
    settings_.emplace(settings);
}

void World::Reset() noexcept {
    map_.reset();
    settings_.reset();
}

bool World::IsInitialized() const noexcept {
    return map_.has_value() && settings_.has_value();
}

const GridMap& World::GetMap() const {
    if (!IsInitialized()) {
        throw std::logic_error("world is not initialized");
    }
    return *map_;
}

const WorldSettings& World::GetSettings() const {
    if (!IsInitialized()) {
        throw std::logic_error("world is not initialized");
    }
    return *settings_;
}

} // namespace fps
