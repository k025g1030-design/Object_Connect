#pragma once

#include "ObjectConnect/Math/Color.hpp"
#include "ObjectConnect/Math/Vec2.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace object_connect {

enum class ObstacleShape {
    Rectangle,
    Circle,
};

struct NodeDefinition final {
    std::string id;
    std::string label;
    Vec2 position{};
    float radius = 24.0f;
    Color color{};
};

struct ConnectionDefinition final {
    std::string fromNodeId;
    std::string toNodeId;
    std::size_t fromNodeIndex = 0;
    std::size_t toNodeIndex = 0;
    std::size_t pointCount = 10;
    float thicknessScale = 1.0f;
    float followDelaySeconds = 0.0f;
    float initialDirectionDegrees = 0.0f;
};

struct ObstacleDefinition final {
    std::string id;
    ObstacleShape shape = ObstacleShape::Rectangle;
    Vec2 center{};
    float width = 0.0f;
    float height = 0.0f;
    float radius = 0.0f;
    Color color{};
};

struct PuzzleDefinition final {
    std::string id;
    std::string title;
    std::string startNodeId;
    std::size_t startNodeIndex = 0;
    float totalLength = 0.0f;
    float minimumSlackRatio = 1.05f;
    Color backgroundColor{};
    bool showTargetConnections = true;
    Color vesselColor{134.0f / 255.0f, 27.0f / 255.0f,
                      43.0f / 255.0f, 1.0f};
    float baseWidth = 16.0f;
    float tipWidth = 16.0f;
    float widthVariation = 0.16f;
    std::vector<NodeDefinition> nodes;
    std::vector<ConnectionDefinition> connections;
    std::vector<ObstacleDefinition> obstacles;
};

class PuzzleCatalog final {
public:
    PuzzleCatalog() = default;
    explicit PuzzleCatalog(std::vector<PuzzleDefinition> puzzles)
        : puzzles_(std::move(puzzles)) {}

    [[nodiscard]] const std::vector<PuzzleDefinition>& GetPuzzles() const noexcept {
        return puzzles_;
    }
    [[nodiscard]] const PuzzleDefinition* Find(const std::string_view id) const noexcept {
        for (const PuzzleDefinition& puzzle : puzzles_) {
            if (puzzle.id == id) {
                return &puzzle;
            }
        }
        return nullptr;
    }
    [[nodiscard]] bool Empty() const noexcept { return puzzles_.empty(); }

private:
    std::vector<PuzzleDefinition> puzzles_;
};

struct PuzzleDataPaths final {
    std::string levels{"data/levels.csv"};
    std::string nodes{"data/nodes.csv"};
    std::string connections{"data/connections.csv"};
    std::string obstacles{"data/obstacles.csv"};
};

} // namespace object_connect
