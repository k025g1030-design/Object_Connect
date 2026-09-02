#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <numbers>
#include <utility>
#include <vector>

namespace object_connect {
namespace {

constexpr float kLengthEpsilon = 0.001f;
constexpr float kMaximumDeltaSeconds = 0.05f;
constexpr float kRetractionSeconds = 0.22f;
constexpr std::size_t kDefaultPointCount = 10;
constexpr float kDefaultFollowDelaySeconds = 0.03f;
constexpr float kDeadCollisionBackoff = 0.05f;

[[nodiscard]] float NormalizeDeltaSeconds(const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return 0.0f;
    }
    return (std::min)(deltaSeconds, kMaximumDeltaSeconds);
}

[[nodiscard]] float FiniteNonNegative(const float value) noexcept {
    return std::isfinite(value) ? (std::max)(0.0f, value) : 0.0f;
}

[[nodiscard]] float EffectiveSlackRatio(const float value) noexcept {
    return std::isfinite(value) ? (std::max)(1.0f, value) : 1.0f;
}

[[nodiscard]] bool CanBeSourceType(const NodeType type) noexcept {
    return type == NodeType::Root || type == NodeType::Follow;
}

[[nodiscard]] bool CanBeTargetType(const NodeType type) noexcept {
    return type == NodeType::Follow || type == NodeType::End;
}

} // namespace

bool PuzzleBoard::Initialize(const PuzzleDefinition& definition,
                             std::string& error) {
    definition_ = {};
    nodeStates_.clear();
    segments_.clear();
    preview_.reset();
    activatedNodeIndices_.clear();
    committedLines_.clear();
    committedLength_ = 0.0f;
    solved_ = false;
    initialized_ = false;
    error.clear();

    try {
        PuzzleDefinition nextDefinition = definition;
        std::vector<NodeState> nextNodeStates(definition.nodes.size());
        std::vector<std::size_t> nextActivatedNodes;
        nextActivatedNodes.reserve(definition.nodes.size());

        for (std::size_t index = 0; index < definition.nodes.size(); ++index) {
            const NodeDefinition& node = definition.nodes[index];
            if (node.HasPlacement() && node.type == NodeType::Root) {
                nextNodeStates[index].active = true;
                nextActivatedNodes.push_back(index);
            }
        }

        definition_ = std::move(nextDefinition);
        nodeStates_ = std::move(nextNodeStates);
        activatedNodeIndices_ = std::move(nextActivatedNodes);
        initialized_ = true;
        EvaluateSolved();
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize PuzzleBoard: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize PuzzleBoard because of an unknown error.";
    }

    definition_ = {};
    nodeStates_.clear();
    activatedNodeIndices_.clear();
    return false;
}

void PuzzleBoard::Update(const BoardPointerInput& input,
                         const float deltaSeconds) noexcept {
    if (!initialized_) {
        return;
    }

    const float normalizedDeltaSeconds = NormalizeDeltaSeconds(deltaSeconds);
    for (Segment& segment : segments_) {
        const std::optional<Vec2> root = GetNodeCenter(segment.line.fromNodeIndex);
        const std::optional<Vec2> tip = GetNodeCenter(segment.line.toNodeIndex);
        if (!root.has_value() || !tip.has_value()) {
            continue;
        }
        segment.tentacle.SetRootAnchor(*root);
        segment.tentacle.AttachTip(*tip);
        segment.tentacle.Update(normalizedDeltaSeconds);
    }

    if (preview_.has_value()) {
        if (preview_->state == PreviewState::Dragging) {
            UpdateDrag(input, normalizedDeltaSeconds);
        } else {
            UpdateRetraction(normalizedDeltaSeconds);
        }
        return;
    }

    if (solved_ || !input.leftPressed) {
        return;
    }

    const std::optional<std::size_t> source = FindSourceAt(input.position);
    if (!source.has_value()) {
        return;
    }
    StartDrag(*source);
    if (preview_.has_value()) {
        UpdateDrag(input, normalizedDeltaSeconds);
    }
}

void PuzzleBoard::CancelDrag(const bool immediate) noexcept {
    if (!preview_.has_value()) {
        return;
    }
    if (immediate || preview_->reservedLength <= kLengthEpsilon) {
        preview_.reset();
        return;
    }
    BeginRetraction();
}

PuzzleBoardSnapshot PuzzleBoard::MakeSnapshot() const {
    PuzzleBoardSnapshot snapshot{};
    snapshot.tentacles.reserve(segments_.size() + (preview_.has_value() ? 1u : 0u));
    snapshot.nodeStates.resize(definition_.nodes.size());

    for (const Segment& segment : segments_) {
        TentacleRenderSnapshot tentacle{};
        const std::span<const Vec2> points = segment.tentacle.GetPoints();
        tentacle.points.assign(points.begin(), points.end());
        tentacle.style = segment.style;
        snapshot.tentacles.push_back(std::move(tentacle));
    }
    if (preview_.has_value()) {
        TentacleRenderSnapshot tentacle{};
        const std::span<const Vec2> points = preview_->tentacle.GetPoints();
        tentacle.points.assign(points.begin(), points.end());
        tentacle.style = preview_->style;
        tentacle.preview = true;
        snapshot.tentacles.push_back(std::move(tentacle));
        snapshot.selectedSourceNodeIndex = preview_->sourceNodeIndex;
    }

    for (std::size_t index = 0; index < nodeStates_.size(); ++index) {
        const NodeState& state = nodeStates_[index];
        NodeRuntimeSnapshot& output = snapshot.nodeStates[index];
        output.drawable = IsDrawable(index);
        output.active = state.active;
        const bool selectedForDrag =
            preview_.has_value() &&
            preview_->state == PreviewState::Dragging &&
            preview_->sourceNodeIndex == index;
        output.availableSource = selectedForDrag ||
                                 (!preview_.has_value() && CanUseAsSource(index));
        output.incomingUsed = state.incomingUsed;
        output.outgoingUsed = state.outgoingUsed;
        output.committedOutgoingLength = state.committedOutgoingLength;
    }

    snapshot.totalLength = GetTotalLength();
    snapshot.remainingLength = GetRemainingLength();
    snapshot.reservedLength = GetReservedLength();
    snapshot.dragging = IsDragging();
    snapshot.retracting = preview_.has_value() &&
                          preview_->state == PreviewState::Retracting;
    snapshot.solved = solved_;
    snapshot.lengthExhausted = IsLengthExhausted();
    return snapshot;
}

float PuzzleBoard::GetTotalLength() const noexcept {
    return initialized_ ? FiniteNonNegative(definition_.totalLength) : 0.0f;
}

float PuzzleBoard::GetRemainingLength() const noexcept {
    if (!initialized_) {
        return 0.0f;
    }
    return (std::max)(0.0f, GetTotalLength() - committedLength_ -
                                GetReservedLength());
}

float PuzzleBoard::GetReservedLength() const noexcept {
    return preview_.has_value() ? preview_->reservedLength : 0.0f;
}

float PuzzleBoard::GetRemainingOutgoingLength(
    const std::size_t nodeIndex) const noexcept {
    if (!initialized_ || nodeIndex >= definition_.nodes.size() ||
        nodeIndex >= nodeStates_.size()) {
        return 0.0f;
    }
    float usedLength = nodeStates_[nodeIndex].committedOutgoingLength;
    if (preview_.has_value() && preview_->sourceNodeIndex == nodeIndex) {
        usedLength += preview_->reservedLength;
    }
    return (std::max)(0.0f,
                      FiniteNonNegative(definition_.nodes[nodeIndex].maxOutgoingLength) -
                          usedLength);
}

bool PuzzleBoard::IsDragging() const noexcept {
    return preview_.has_value() && preview_->state == PreviewState::Dragging;
}

bool PuzzleBoard::IsLengthExhausted() const noexcept {
    if (!initialized_ || solved_) {
        return false;
    }

    // Reservation is still changing while a preview is active. Report
    // exhaustion only from a stable board state, after commit or full refund.
    if (preview_.has_value()) {
        return false;
    }

    const float globalAvailable = GetRemainingLength();
    bool foundStructurallyAvailableConnection = false;
    for (std::size_t sourceIndex = 0; sourceIndex < definition_.nodes.size();
         ++sourceIndex) {
        if (!IsDrawable(sourceIndex) || sourceIndex >= nodeStates_.size() ||
            !nodeStates_[sourceIndex].active ||
            !CanBeSourceType(definition_.nodes[sourceIndex].type) ||
            nodeStates_[sourceIndex].outgoingUsed >=
                definition_.nodes[sourceIndex].maxOutgoing) {
            continue;
        }

        const std::optional<Vec2> source = GetNodeCenter(sourceIndex);
        if (!source.has_value()) {
            continue;
        }
        const float deployable = (std::min)(
            globalAvailable, GetRemainingOutgoingLength(sourceIndex));
        for (std::size_t targetIndex = 0; targetIndex < definition_.nodes.size();
             ++targetIndex) {
            if (!CanUseAsTarget(targetIndex) || targetIndex == sourceIndex ||
                IsDuplicateConnection(sourceIndex, targetIndex) ||
                WouldCreateCycle(sourceIndex, targetIndex)) {
                continue;
            }
            const std::optional<Vec2> target = GetNodeCenter(targetIndex);
            if (!target.has_value()) {
                continue;
            }
            const TentacleStyle style = MakeStyle(committedLines_.size());
            const float clearance =
                (std::max)(style.baseWidth, style.tipWidth) * 0.5f;
            if (IsBlockedByDeadNode(*source, *target, clearance)) {
                continue;
            }

            foundStructurallyAvailableConnection = true;
            const float required =
                Length(*target - *source) *
                EffectiveSlackRatio(definition_.minimumSlackRatio);
            if (deployable + kLengthEpsilon >= required) {
                return false;
            }
        }
    }
    return foundStructurallyAvailableConnection;
}

void PuzzleBoard::StartDrag(const std::size_t sourceNodeIndex) noexcept {
    if (!CanUseAsSource(sourceNodeIndex)) {
        return;
    }
    const std::optional<Vec2> root = GetNodeCenter(sourceNodeIndex);
    if (!root.has_value()) {
        return;
    }

    const float availableLength =
        (std::min)(GetRemainingLength(),
                   GetRemainingOutgoingLength(sourceNodeIndex));
    if (availableLength <= kLengthEpsilon) {
        return;
    }

    BloodTentacleSettings settings{};
    settings.pointCount = kDefaultPointCount;
    settings.followDelaySeconds = kDefaultFollowDelaySeconds;

    Preview next{};
    next.sourceNodeIndex = sourceNodeIndex;
    next.style = MakeStyle(committedLines_.size());
    std::string ignoredError;
    if (!next.tentacle.Initialize(*root, {1.0f, 0.0f}, availableLength,
                                  settings, ignoredError)) {
        return;
    }
    next.tentacle.SetDeployedLength(0.0f);
    next.tentacle.AttachTip(*root);
    next.tentacle.Update(1.0f / 15.0f);
    next.tentacle.FollowTip(*root);
    preview_ = std::move(next);
}

void PuzzleBoard::UpdateDrag(const BoardPointerInput& input,
                             const float deltaSeconds) noexcept {
    if (!preview_.has_value() || preview_->state != PreviewState::Dragging) {
        return;
    }

    const std::optional<Vec2> root =
        GetNodeCenter(preview_->sourceNodeIndex);
    if (!root.has_value()) {
        preview_.reset();
        return;
    }
    preview_->tentacle.SetRootAnchor(*root);

    Vec2 pointerTarget = *root;
    if (IsFinite(input.position)) {
        const float clearance =
            (std::max)(preview_->style.baseWidth,
                       preview_->style.tipWidth) * 0.5f;
        pointerTarget = ClampTipTargetToDeadNodes(
            *root, input.position, clearance);
        const float desiredLength =
            Length(pointerTarget - *root) *
            EffectiveSlackRatio(definition_.minimumSlackRatio);
        preview_->reservedLength = (std::max)(
            preview_->reservedLength,
            std::clamp(desiredLength, 0.0f,
                       preview_->tentacle.GetMaxLength()));
    }

    preview_->tentacle.SetDeployedLength(preview_->reservedLength);
    preview_->tentacle.FollowTip(pointerTarget);
    preview_->tentacle.Update(deltaSeconds);

    if (!input.leftReleased) {
        return;
    }

    const std::optional<std::size_t> target =
        FindCommitTargetAt(input.position);
    if (target.has_value()) {
        CommitConnection(*target);
        return;
    }
    BeginRetraction();
}

void PuzzleBoard::UpdateRetraction(const float deltaSeconds) noexcept {
    if (!preview_.has_value() || preview_->state != PreviewState::Retracting) {
        return;
    }

    const std::optional<Vec2> root =
        GetNodeCenter(preview_->sourceNodeIndex);
    if (!root.has_value()) {
        preview_.reset();
        return;
    }
    preview_->tentacle.SetRootAnchor(*root);
    preview_->tentacle.FollowTip(*root);

    const float oldTimeRemaining =
        (std::max)(0.0f, kRetractionSeconds - preview_->retractElapsedSeconds);
    preview_->retractElapsedSeconds =
        (std::min)(kRetractionSeconds,
                   preview_->retractElapsedSeconds + deltaSeconds);
    const float newTimeRemaining =
        (std::max)(0.0f, kRetractionSeconds - preview_->retractElapsedSeconds);
    if (oldTimeRemaining > 0.0f) {
        preview_->reservedLength *= newTimeRemaining / oldTimeRemaining;
    } else {
        preview_->reservedLength = 0.0f;
    }

    preview_->tentacle.SetDeployedLength(preview_->reservedLength);
    preview_->tentacle.Update(deltaSeconds);
    if (preview_->retractElapsedSeconds >= kRetractionSeconds ||
        preview_->reservedLength <= kLengthEpsilon) {
        preview_.reset();
    }
}

void PuzzleBoard::CommitConnection(const std::size_t targetNodeIndex) noexcept {
    if (!preview_.has_value() ||
        preview_->state != PreviewState::Dragging ||
        targetNodeIndex >= definition_.nodes.size()) {
        return;
    }

    const std::size_t sourceNodeIndex = preview_->sourceNodeIndex;
    const std::optional<Vec2> root = GetNodeCenter(sourceNodeIndex);
    const std::optional<Vec2> tip = GetNodeCenter(targetNodeIndex);
    if (!root.has_value() || !tip.has_value()) {
        BeginRetraction();
        return;
    }

    try {
        segments_.reserve(segments_.size() + 1);
        committedLines_.reserve(committedLines_.size() + 1);
    } catch (...) {
        BeginRetraction();
        return;
    }

    const float committedLength = preview_->reservedLength;
    preview_->tentacle.SetRootAnchor(*root);
    preview_->tentacle.AttachTip(*tip);

    CommittedLine line{sourceNodeIndex, targetNodeIndex, committedLength};
    Segment segment{};
    segment.tentacle = std::move(preview_->tentacle);
    segment.style = preview_->style;
    segment.line = line;
    segments_.push_back(std::move(segment));
    committedLines_.push_back(line);
    preview_.reset();

    NodeState& sourceState = nodeStates_[sourceNodeIndex];
    NodeState& targetState = nodeStates_[targetNodeIndex];
    ++sourceState.outgoingUsed;
    sourceState.committedOutgoingLength += committedLength;
    ++targetState.incomingUsed;
    committedLength_ =
        (std::min)(GetTotalLength(), committedLength_ + committedLength);
    ActivateNode(targetNodeIndex);
    EvaluateSolved();
}

void PuzzleBoard::BeginRetraction() noexcept {
    if (!preview_.has_value()) {
        return;
    }
    if (preview_->reservedLength <= kLengthEpsilon) {
        preview_.reset();
        return;
    }
    preview_->state = PreviewState::Retracting;
    preview_->retractElapsedSeconds = 0.0f;
    const std::optional<Vec2> root =
        GetNodeCenter(preview_->sourceNodeIndex);
    if (root.has_value()) {
        preview_->tentacle.FollowTip(*root);
    }
}

void PuzzleBoard::ActivateNode(const std::size_t nodeIndex) noexcept {
    if (nodeIndex >= nodeStates_.size() || !IsDrawable(nodeIndex) ||
        nodeStates_[nodeIndex].active) {
        return;
    }
    nodeStates_[nodeIndex].active = true;
    activatedNodeIndices_.push_back(nodeIndex);
}

void PuzzleBoard::EvaluateSolved() noexcept {
    bool hasPlacedEnd = false;
    for (std::size_t index = 0; index < definition_.nodes.size(); ++index) {
        if (!IsDrawable(index) || definition_.nodes[index].type != NodeType::End) {
            continue;
        }
        hasPlacedEnd = true;
        if (index >= nodeStates_.size() || !nodeStates_[index].active) {
            solved_ = false;
            return;
        }
    }
    solved_ = hasPlacedEnd;
}

bool PuzzleBoard::IsDrawable(const std::size_t nodeIndex) const noexcept {
    return nodeIndex < definition_.nodes.size() &&
           definition_.nodes[nodeIndex].HasPlacement();
}

bool PuzzleBoard::CanUseAsSource(const std::size_t nodeIndex) const noexcept {
    if (!initialized_ || solved_ || !IsDrawable(nodeIndex) ||
        nodeIndex >= nodeStates_.size()) {
        return false;
    }
    const NodeDefinition& node = definition_.nodes[nodeIndex];
    const NodeState& state = nodeStates_[nodeIndex];
    return state.active && CanBeSourceType(node.type) &&
           state.outgoingUsed < node.maxOutgoing &&
           GetRemainingLength() > kLengthEpsilon &&
           GetRemainingOutgoingLength(nodeIndex) > kLengthEpsilon;
}

bool PuzzleBoard::CanUseAsTarget(const std::size_t nodeIndex) const noexcept {
    if (!initialized_ || !IsDrawable(nodeIndex) ||
        nodeIndex >= nodeStates_.size()) {
        return false;
    }
    const NodeDefinition& node = definition_.nodes[nodeIndex];
    return CanBeTargetType(node.type) &&
           nodeStates_[nodeIndex].incomingUsed < node.maxIncoming;
}

bool PuzzleBoard::CanCommitTo(const std::size_t targetNodeIndex,
                              const Vec2 pointerPosition) const noexcept {
    if (!preview_.has_value() ||
        preview_->state != PreviewState::Dragging ||
        !CanUseAsTarget(targetNodeIndex) || !IsFinite(pointerPosition)) {
        return false;
    }

    const std::size_t sourceNodeIndex = preview_->sourceNodeIndex;
    if (sourceNodeIndex >= definition_.nodes.size() ||
        sourceNodeIndex >= nodeStates_.size() ||
        sourceNodeIndex == targetNodeIndex ||
        IsDuplicateConnection(sourceNodeIndex, targetNodeIndex) ||
        WouldCreateCycle(sourceNodeIndex, targetNodeIndex)) {
        return false;
    }

    const NodeDefinition& sourceNode = definition_.nodes[sourceNodeIndex];
    const NodeState& sourceState = nodeStates_[sourceNodeIndex];
    if (!sourceState.active || !CanBeSourceType(sourceNode.type) ||
        sourceState.outgoingUsed >= sourceNode.maxOutgoing) {
        return false;
    }

    const std::optional<AxisAlignedBox> targetBounds =
        GetNodeBounds(targetNodeIndex);
    const std::optional<Vec2> source = GetNodeCenter(sourceNodeIndex);
    const std::optional<Vec2> target = GetNodeCenter(targetNodeIndex);
    if (!targetBounds.has_value() || !source.has_value() || !target.has_value() ||
        !PointInAxisAlignedBox(pointerPosition, *targetBounds)) {
        return false;
    }

    const float requiredLength =
        Length(*target - *source) *
        EffectiveSlackRatio(definition_.minimumSlackRatio);
    if (preview_->reservedLength + kLengthEpsilon < requiredLength) {
        return false;
    }

    const float clearance =
        (std::max)(preview_->style.baseWidth, preview_->style.tipWidth) * 0.5f;
    return !IsBlockedByDeadNode(*source, *target, clearance);
}

bool PuzzleBoard::IsDuplicateConnection(
    const std::size_t sourceNodeIndex,
    const std::size_t targetNodeIndex) const noexcept {
    return std::any_of(
        committedLines_.begin(), committedLines_.end(),
        [sourceNodeIndex, targetNodeIndex](const CommittedLine& line) {
            return line.fromNodeIndex == sourceNodeIndex &&
                   line.toNodeIndex == targetNodeIndex;
        });
}

bool PuzzleBoard::WouldCreateCycle(const std::size_t sourceNodeIndex,
                                   const std::size_t targetNodeIndex) const noexcept {
    if (sourceNodeIndex == targetNodeIndex) {
        return true;
    }
    if (sourceNodeIndex >= definition_.nodes.size() ||
        targetNodeIndex >= definition_.nodes.size()) {
        return true;
    }

    try {
        std::vector<bool> visited(definition_.nodes.size(), false);
        std::vector<std::size_t> frontier;
        frontier.reserve(definition_.nodes.size());
        visited[targetNodeIndex] = true;
        frontier.push_back(targetNodeIndex);
        for (std::size_t cursor = 0; cursor < frontier.size(); ++cursor) {
            const std::size_t current = frontier[cursor];
            if (current == sourceNodeIndex) {
                return true;
            }
            for (const CommittedLine& line : committedLines_) {
                if (line.fromNodeIndex != current ||
                    line.toNodeIndex >= visited.size() || visited[line.toNodeIndex]) {
                    continue;
                }
                visited[line.toNodeIndex] = true;
                frontier.push_back(line.toNodeIndex);
            }
        }
        return false;
    } catch (...) {
        return true;
    }
}

bool PuzzleBoard::IsBlockedByDeadNode(const Vec2 start, const Vec2 end,
                                      const float clearance) const noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !std::isfinite(clearance) ||
        clearance < 0.0f) {
        return true;
    }
    for (std::size_t index = 0; index < definition_.nodes.size(); ++index) {
        if (definition_.nodes[index].type != NodeType::Dead || !IsDrawable(index)) {
            continue;
        }
        const std::optional<AxisAlignedBox> bounds = GetNodeBounds(index);
        if (!bounds.has_value()) {
            return true;
        }
        const AxisAlignedBox expanded = ExpandAxisAlignedBox(*bounds, clearance);
        if (!IsValidAxisAlignedBox(expanded) ||
            SegmentIntersectsAxisAlignedBox(start, end, expanded)) {
            return true;
        }
    }
    return false;
}

Vec2 PuzzleBoard::ClampTipTargetToDeadNodes(
    const Vec2 start, const Vec2 desiredEnd, const float clearance) const noexcept {
    if (!IsFinite(start) || !IsFinite(desiredEnd) || !std::isfinite(clearance) ||
        clearance < 0.0f) {
        return start;
    }

    float earliestEntryTime = 1.0f;
    bool blocked = false;
    for (std::size_t index = 0; index < definition_.nodes.size(); ++index) {
        if (definition_.nodes[index].type != NodeType::Dead || !IsDrawable(index)) {
            continue;
        }
        const std::optional<AxisAlignedBox> bounds = GetNodeBounds(index);
        if (!bounds.has_value()) {
            return start;
        }
        const AxisAlignedBox expanded = ExpandAxisAlignedBox(*bounds, clearance);
        const std::optional<float> entry =
            SegmentAxisAlignedBoxEntryTime(start, desiredEnd, expanded);
        if (entry.has_value() && *entry <= earliestEntryTime) {
            earliestEntryTime = *entry;
            blocked = true;
        }
    }
    if (!blocked) {
        return desiredEnd;
    }

    const Vec2 delta = desiredEnd - start;
    const float distance = Length(delta);
    if (!std::isfinite(distance) || distance <= kLengthEpsilon) {
        return start;
    }
    const float safeDistance =
        (std::max)(0.0f, distance * earliestEntryTime - kDeadCollisionBackoff);
    return start + delta * (safeDistance / distance);
}

std::optional<std::size_t> PuzzleBoard::FindSourceAt(
    const Vec2 point) const noexcept {
    if (!IsFinite(point)) {
        return std::nullopt;
    }
    for (std::size_t reverse = definition_.nodes.size(); reverse > 0; --reverse) {
        const std::size_t index = reverse - 1;
        const std::optional<AxisAlignedBox> bounds = GetNodeBounds(index);
        if (CanUseAsSource(index) && bounds.has_value() &&
            PointInAxisAlignedBox(point, *bounds)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> PuzzleBoard::FindCommitTargetAt(
    const Vec2 point) const noexcept {
    if (!preview_.has_value() || !IsFinite(point)) {
        return std::nullopt;
    }

    std::optional<std::size_t> bestTarget;
    float bestDistanceSquared = 0.0f;
    for (std::size_t index = 0; index < definition_.nodes.size(); ++index) {
        if (!CanCommitTo(index, point)) {
            continue;
        }
        const std::optional<Vec2> center = GetNodeCenter(index);
        if (!center.has_value()) {
            continue;
        }
        const float distanceSquared = LengthSquared(point - *center);
        if (!bestTarget.has_value() || distanceSquared < bestDistanceSquared) {
            bestTarget = index;
            bestDistanceSquared = distanceSquared;
        }
    }
    return bestTarget;
}

std::optional<AxisAlignedBox> PuzzleBoard::GetNodeBounds(
    const std::size_t nodeIndex) const noexcept {
    if (!IsDrawable(nodeIndex)) {
        return std::nullopt;
    }
    const NodeDefinition& node = definition_.nodes[nodeIndex];
    const std::optional<Vec2> topLeft = node.GetTopLeftPosition();
    if (!topLeft.has_value()) {
        return std::nullopt;
    }
    const Vec2 size = node.GetPixelSize();
    const AxisAlignedBox bounds{*topLeft, *topLeft + size};
    return IsValidAxisAlignedBox(bounds)
               ? std::optional<AxisAlignedBox>{bounds}
               : std::nullopt;
}

std::optional<Vec2> PuzzleBoard::GetNodeCenter(
    const std::size_t nodeIndex) const noexcept {
    if (!IsDrawable(nodeIndex)) {
        return std::nullopt;
    }
    const std::optional<Vec2> center =
        definition_.nodes[nodeIndex].GetCenterPosition();
    return center.has_value() && IsFinite(*center) ? center : std::nullopt;
}

TentacleStyle PuzzleBoard::MakeStyle(const std::size_t lineIndex) const noexcept {
    TentacleStyle style{};
    style.baseWidth = FiniteNonNegative(definition_.baseWidth);
    style.tipWidth = FiniteNonNegative(definition_.tipWidth);
    style.widthVariation = std::isfinite(definition_.widthVariation)
                               ? std::clamp(definition_.widthVariation, 0.0f, 0.95f)
                               : 0.0f;
    style.widthPhase = std::fmod(static_cast<float>(lineIndex) * 1.6180339f,
                                2.0f * std::numbers::pi_v<float>);
    style.color = definition_.vesselColor;
    return style;
}

} // namespace object_connect
