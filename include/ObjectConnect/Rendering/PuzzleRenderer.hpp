#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace object_connect {

class PuzzleRenderer final {
public:
    PuzzleRenderer() noexcept;
    ~PuzzleRenderer();

    PuzzleRenderer(const PuzzleRenderer&) = delete;
    PuzzleRenderer& operator=(const PuzzleRenderer&) = delete;

    [[nodiscard]] bool Initialize(std::size_t maxVertices, std::string& error);
    [[nodiscard]] bool PreparePuzzle(const PuzzleDefinition& definition,
                                     std::string& error);
    void Draw(const PuzzleDefinition& definition,
              const PuzzleBoardSnapshot& snapshot,
              float elapsedSeconds);
    void Finalize() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace object_connect
