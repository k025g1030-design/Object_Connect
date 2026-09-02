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
    float committedLength = 0.0f;
    float remainingLength = 0.0f;
    float reservedLength = 0.0f;
    std::vector<std::size_t> activatedNodeIndices;
    std::vector<std::size_t> availableSourceNodeIndices;
    std::optional<std::size_t> selectedSourceNodeIndex;
    std::vector<std::size_t> incomingConnectionCounts;
    std::vector<std::size_t> outgoingConnectionCounts;
    bool dragging = false;
    bool retracting = false;
    bool solved = false;
    bool lengthExhausted = false;
    bool routeBlocked = false;
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
    [[nodiscard]] float GetCommittedLength() const noexcept { return committedLength_; }
    [[nodiscard]] float GetRemainingLength() const noexcept;
    [[nodiscard]] float GetReservedLength() const noexcept;
    [[nodiscard]] bool IsSolved() const noexcept { return solved_; }
    [[nodiscard]] bool IsDragging() const noexcept;
    [[nodiscard]] bool IsLengthExhausted() const noexcept;
    [[nodiscard]] bool IsRouteBlocked() const noexcept;
    [[nodiscard]] std::size_t GetCompletedConnectionCount() const noexcept {
        return committedConnectionIndices_.size();
    }
    // Read-only graph progress for optional systems such as scoring. Indices refer
    // to GetDefinition().nodes/connections; PuzzleBoard deliberately does not
    // assign points or interpret nodes as organs.
    [[nodiscard]] const std::vector<std::size_t>&
    GetActivatedNodeIndices() const noexcept {
        return activatedNodeIndices_;
    }
    [[nodiscard]] const std::vector<std::size_t>&
    GetCommittedConnectionIndices() const noexcept {
        return committedConnectionIndices_;
    }
    [[nodiscard]] const std::vector<std::size_t>&
    GetIncomingConnectionCounts() const noexcept {
        return incomingConnectionCounts_;
    }
    [[nodiscard]] const std::vector<std::size_t>&
    GetOutgoingConnectionCounts() const noexcept {
        return outgoingConnectionCounts_;
    }
    [[nodiscard]] std::optional<std::size_t>
    GetSelectedSourceNodeIndex() const noexcept;
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
        std::size_t sourceNodeIndex = 0;
        std::size_t connectionIndex = 0;
        PreviewState state = PreviewState::Dragging;
        float reservedLength = 0.0f;
        float retractElapsedSeconds = 0.0f;
    };

    void StartDrag(std::size_t sourceNodeIndex, std::size_t connectionIndex) noexcept;
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
    FindOutgoingConnectionAt(std::size_t sourceNodeIndex, Vec2 point) const noexcept;
    [[nodiscard]] std::optional<std::size_t>
    GetFirstAvailableOutgoingConnection(std::size_t sourceNodeIndex) const noexcept;
    [[nodiscard]] bool CanUseConnection(std::size_t connectionIndex) const noexcept;
    [[nodiscard]] bool CanStartFromNode(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool IsNodeActivated(std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool AreAllGoalsActivated() const noexcept;
    [[nodiscard]] bool PointHitsNodeMask(Vec2 point,
                                         std::size_t nodeIndex) const noexcept;
    [[nodiscard]] bool IsConnectionBlockedBySolidTiles(
        Vec2 start, Vec2 end, float clearance) const noexcept;
    [[nodiscard]] TentacleStyle MakeStyle(const ConnectionDefinition& connection,
                                          std::size_t connectionIndex) const noexcept;

    PuzzleDefinition definition_{};
    std::vector<Segment> segments_;
    std::optional<Preview> preview_;
    std::vector<std::size_t> committedConnectionIndices_;
    std::vector<std::size_t> activatedNodeIndices_;
    std::vector<std::size_t> incomingConnectionCounts_;
    std::vector<std::size_t> outgoingConnectionCounts_;
    std::vector<bool> activatedNodes_;
    std::vector<bool> committedConnections_;
    float committedLength_ = 0.0f;
    bool solved_ = false;
    bool initialized_ = false;
};

} // namespace object_connect
