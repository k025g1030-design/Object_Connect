#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace object_connect {

struct PuzzleJsonSource final {
    std::string_view filename;
    std::string_view contents;
};

// In-memory source bundle used by headless tests and tools. Document filenames
// are resolved exactly like disk paths: catalog references are relative to the
// catalog document's directory.
struct PuzzleJsonSources final {
    PuzzleJsonSource catalog;
    PuzzleJsonSource tileset;
    PuzzleJsonSource nodeTypes;
    std::vector<PuzzleJsonSource> levels;
};

class PuzzleCatalogLoader final {
public:
    [[nodiscard]] static bool Load(const std::string& catalogPath,
                                   const std::string& resourceRoot,
                                   PuzzleCatalog& catalog,
                                   std::string& error);
    [[nodiscard]] static bool Parse(const PuzzleJsonSources& sources,
                                    PuzzleCatalog& catalog,
                                    std::string& error);
};

} // namespace object_connect
