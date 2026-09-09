#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"

#include <string>

namespace object_connect {

struct GameConfig final {
    PuzzleDataPaths data{};
    std::string resourceRoot{"Resources"};
    std::string uiFontPath{"fonts/BIZUDPGothic-Regular.ttf"};
};

} // namespace object_connect
