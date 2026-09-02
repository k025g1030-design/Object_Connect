#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"

#include <string>

namespace object_connect {

struct GameConfig final {
    PuzzleDataPaths data{};
    std::string resourceRoot{"Resources"};
};

} // namespace object_connect
