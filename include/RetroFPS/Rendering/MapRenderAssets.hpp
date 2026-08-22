#pragma once

#include <string>

namespace fps {

struct MapRenderAssets {
    std::string floorModelName{"map_floor"};
    std::string wallModelName{"map_wall"};
    std::string doorModelName{"cube"};
    std::string doorTexturePath{"white1x1.png"};
};

} // namespace fps
