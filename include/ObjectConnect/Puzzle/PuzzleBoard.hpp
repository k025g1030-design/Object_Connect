#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Tentacle/BloodTentacle.hpp"
#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace object_connect {

struct BoardPointerInput final {
    Vec2 position{};
    bool leftPressed = false;
    bool leftHeld = false;
    bool leftReleased = false;
};

struct TentacleRenderSnapshot final {
    std::vector<Vec2> points;
    TentacleStyle style{};
    bool preview = false;
};

struct PuzzleBoardSnapshot final {
    std::vector<TentacleRenderSnapshot> tentacles;
    float totalLength = 0.0f;
    float remainingLength = 0.0f;
    float reservedLength = 0.0f;
    std::optional<std::size_t> activeNodeIndex;
    bool dragging = false;
    bool retracting = false;
    bool solved = false;
    bool lengthExhausted = false;
};

class PuzzleBoard final {
public:
    [[nodiscard]] bool Initialize(const PuzzleDefinition& definition, std::string& error);
    void Update(const BoardPointerInput& input, float deltaSeconds) noexcept;
    void CancelDrag(bool immediate) noexcept;

    [[nodiscard]] PuzzleBoardSnapshot MakeSnapshot() const;
    [[nodiscard]] const PuzzleDefinition& GetDefinition() const noexcept {
        return definition_;
    }
    [[nodiscard]] float GetTotalLength() const noexcept { return definition_.totalLength; }
    [[nodiscard]] float GetRemainingLength() const noexcept;
    [[nodiscard]] float GetReservedLength() const noexcept;
    [[nodiscard]] bool IsSolved() const noexcept { return solved_; }
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] bool IsLengthExhausted() const noexcept;
    [[nodiscard]] std::size_t GetCompletedConnectionCount() const noexcept {
        return committedConnectionIndices_.size();
    }
    // Read-only route progress for optional systems such as scoring. Indices refer
    // to GetDefinition().nodes/connections; PuzzleBoard deliberately does not
    // assign points or interpret nodes as organs.
    [[nodiscard]] const std::vector<std::size_t>& GetVisitedNodeIndices() const noexcept {
        return visitedNodeIndices_;
    }
    [[nodiscard]] const std::vector<std::size_t>&
    GetCommittedConnectionIndices() const noexcept {
        return committedConnectionIndices_;
    }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    enum class PreviewState {
        Dragging,
        Retracting,
    };

    struct Segment final {
        BloodTentacle tentacle;
        TentacleStyle style{};
        std::size_t connectionIndex = 0;
        float committedLength = 0.0f;
    };

    struct Preview final {
        BloodTentacle tentacle;
        TentacleStyle style{};
        std::size_t connectionIndex = 0;
        PreviewState state = PreviewState::Dragging;
        float reservedLength = 0.0f;
        float retractElapsedSeconds = 0.0f;
    };

    void StartDrag(std::size_t connectionIndex) noexcept;
    [[nodiscard]] bool SelectPreviewConnection(std::size_t connectionIndex,
                                               Vec2 pointerTarget) noexcept;
    void UpdateDrag(const BoardPointerInput& input, float deltaSeconds) noexcept;
    void UpdateRetraction(float deltaSeconds) noexcept;
    void CommitConnection(std::size_t connectionIndex) noexcept;
    void BeginRetraction() noexcept;
    [[nodiscard]] bool CanCommitConnection(std::size_t connectionIndex,
                                           Vec2 pointerPosition) const noexcept;
    [[nodiscard]] std::optional<std::size_t> HitTestNode(Vec2 point) const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    FindOutgoingConnectionAt(Vec2 point) const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    GetFirstOutgoingConnection() const noexcept;
    [[nodiscard]] bool HasOutgoingConnection(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] TentacleStyle MakeStyle(const ConnectionDefinition& connection,
                                          std::size_t connectionIndex) const noexcept;

    PuzzleDefinition definition_{};
    std::vector<Segment> segments_;
    std::optional<Preview> preview_;
    std::vector<std::size_t> committedConnectionIndices_;
    std::vector<std::size_t> visitedNodeIndices_;
    std::size_t currentNodeIndex_ = 0;
    float committedLength_ = 0.0f;
    bool solved_ = false;
    bool initialized_ = false;
};

} // namespace object_connect
