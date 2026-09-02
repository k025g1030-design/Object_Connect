#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Geometry/Geometry2D.hpp"
#include "ObjectConnect/Tentacle/BloodTentacle.hpp"
#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include <cstddef>
#include <optional>
#include <span>
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

struct NodeRuntimeSnapshot final {
    bool drawable = false;
    bool active = false;
    bool availableSource = false;
    std::size_t incomingUsed = 0;
    std::size_t outgoingUsed = 0;
    float committedOutgoingLength = 0.0f;
};

struct CommittedLine final {
    std::size_t fromNodeIndex = 0;
    std::size_t toNodeIndex = 0;
    float committedLength = 0.0f;
};

struct PuzzleBoardSnapshot final {
    std::vector<TentacleRenderSnapshot> tentacles;
    std::vector<NodeRuntimeSnapshot> nodeStates;
    float totalLength = 0.0f;
    float remainingLength = 0.0f;
    float reservedLength = 0.0f;
    std::optional<std::size_t> selectedSourceNodeIndex;
    bool dragging = false;
    bool retracting = false;
    bool solved = false;
    bool lengthExhausted = false;
};

class PuzzleBoard final {
public:
    [[nodiscard]] bool Initialize(const PuzzleDefinition& definition,
                                  std::string& error);
    void Update(const BoardPointerInput& input, float deltaSeconds) noexcept;
    void CancelDrag(bool immediate) noexcept;

    [[nodiscard]] PuzzleBoardSnapshot MakeSnapshot() const;
    [[nodiscard]] const PuzzleDefinition& GetDefinition() const noexcept {
        return definition_;
    }
    [[nodiscard]] float GetTotalLength() const noexcept;
    [[nodiscard]] float GetRemainingLength() const noexcept;
    [[nodiscard]] float GetReservedLength() const noexcept;
    [[nodiscard]] float GetRemainingOutgoingLength(
        std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool IsSolved() const noexcept { return solved_; }
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] bool IsLengthExhausted() const noexcept;
    [[nodiscard]] std::size_t GetCompletedConnectionCount() const noexcept {
        return committedLines_.size();
    }
    [[nodiscard]] const std::vector<std::size_t>&
    GetActivatedNodeIndices() const noexcept {
        return activatedNodeIndices_;
    }
    [[nodiscard]] const std::vector<CommittedLine>& GetCommittedLines() const noexcept {
        return committedLines_;
    }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    enum class PreviewState {
        Dragging,
        Retracting,
    };

    struct NodeState final {
        bool active = false;
        std::size_t incomingUsed = 0;
        std::size_t outgoingUsed = 0;
        float committedOutgoingLength = 0.0f;
    };

    struct Segment final {
        BloodTentacle tentacle;
        TentacleStyle style{};
        CommittedLine line{};
    };

    struct Preview final {
        BloodTentacle tentacle;
        TentacleStyle style{};
        std::size_t sourceNodeIndex = 0;
        PreviewState state = PreviewState::Dragging;
        float reservedLength = 0.0f;
        float retractElapsedSeconds = 0.0f;
    };

    void StartDrag(std::size_t sourceNodeIndex) noexcept;
    void UpdateDrag(const BoardPointerInput& input, float deltaSeconds) noexcept;
    void UpdateRetraction(float deltaSeconds) noexcept;
    void CommitConnection(std::size_t targetNodeIndex) noexcept;
    void BeginRetraction() noexcept;
    void ActivateNode(std::size_t nodeIndex) noexcept;
    void EvaluateSolved() noexcept;

    [[nodiscard]] bool IsDrawable(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool CanUseAsSource(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool CanUseAsTarget(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool CanCommitTo(std::size_t targetNodeIndex,
                                   Vec2 pointerPosition) const noexcept;
    [[nodiscard]] bool IsDuplicateConnection(std::size_t sourceNodeIndex,
                                             std::size_t targetNodeIndex) const noexcept;
    [[nodiscard]] bool WouldCreateCycle(std::size_t sourceNodeIndex,
                                        std::size_t targetNodeIndex) const noexcept;
    [[nodiscard]] bool IsBlockedByDeadNode(Vec2 start, Vec2 end,
                                           float clearance) const noexcept;
    [[nodiscard]] Vec2 ClampTipTargetToDeadNodes(
        Vec2 start, Vec2 desiredEnd, float clearance) const noexcept;
    [[nodiscard]] std::optional<std::size_t> FindSourceAt(Vec2 point) const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    FindCommitTargetAt(Vec2 point) const noexcept;
    [[nodiscard]] std::optional<AxisAlignedBox>
    GetNodeBounds(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] std::optional<Vec2>
    GetNodeCenter(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] TentacleStyle MakeStyle(std::size_t lineIndex) const noexcept;

    PuzzleDefinition definition_{};
    std::vector<NodeState> nodeStates_;
    std::vector<Segment> segments_;
    std::optional<Preview> preview_;
    std::vector<std::size_t> activatedNodeIndices_;
    std::vector<CommittedLine> committedLines_;
    float committedLength_ = 0.0f;
    bool solved_ = false;
    bool initialized_ = false;
};

} // namespace object_connect
