#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"

#include <string>
#include <string_view>

namespace object_connect {

struct PuzzleCsvSources final {
    std::string_view levels;
    std::string_view nodes;
    std::string_view connections;
    std::string_view obstacles;
};

class PuzzleCatalogLoader final {
public:
    [[nodiscard]] static bool Load(const PuzzleDataPaths& paths,
                                   const std::string& resourceRoot,
                                   PuzzleCatalog& catalog,
                                   std::string& error);
    [[nodiscard]] static bool Parse(const PuzzleCsvSources& sources,
                                    PuzzleCatalog& catalog,
                                    std::string& error);
};

} // namespace object_connect
