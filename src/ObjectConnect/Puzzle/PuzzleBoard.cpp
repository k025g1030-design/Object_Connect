#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <numbers>
#include <utility>

namespace object_connect {
namespace {

constexpr float kLengthEpsilon = 0.001f;
constexpr float kMaximumDeltaSeconds = 0.05f;
constexpr float kRetractionSeconds = 0.22f;
constexpr std::size_t kMinimumPointCount = 8;
constexpr std::size_t kMaximumPointCount = 12;

[[nodiscard]] float NormalizeDeltaSeconds(const float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return 0.0f;
    }
    return (std::min)(deltaSeconds, kMaximumDeltaSeconds);
}

[[nodiscard]] float ConnectionMinimumLength(
    const PuzzleDefinition& definition,
    const ConnectionDefinition& connection) noexcept {
    if (connection.fromNodeIndex >= definition.nodes.size() ||
        connection.toNodeIndex >= definition.nodes.size()) {
        return 0.0f;
    }
    const Vec2 from = definition.nodes[connection.fromNodeIndex].anchorPosition;
    const Vec2 to = definition.nodes[connection.toNodeIndex].anchorPosition;
    return Length(to - from) * definition.minimumSlackRatio;
}

[[nodiscard]] bool IsPositiveFinite(const float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

[[nodiscard]] bool HasCellCount(const std::size_t columns,
                                const std::size_t rows,
                                const std::size_t count) noexcept {
    if (columns == 0 || rows == 0 ||
        columns > (std::numeric_limits<std::size_t>::max)() / rows) {
        return false;
    }
    return columns * rows == count;
}

} // namespace

bool PuzzleBoard::Initialize(const PuzzleDefinition& definition, std::string& error) {
    definition_ = {};
    segments_.clear();
    preview_.reset();
    committedConnectionIndices_.clear();
    activatedNodeIndices_.clear();
    incomingConnectionCounts_.clear();
    outgoingConnectionCounts_.clear();
    activatedNodes_.clear();
    committedConnections_.clear();
    committedLength_ = 0.0f;
    solved_ = false;
    initialized_ = false;
    error.clear();

    if (definition.nodes.empty()) {
        error = "PuzzleBoard requires at least one node.";
        return false;
    }
    if (definition.rootNodeIndices.empty()) {
        error = "PuzzleBoard requires at least one root node.";
        return false;
    }
    if (definition.goalNodeIndices.empty()) {
        error = "PuzzleBoard requires at least one goal node.";
        return false;
    }
    if (!IsPositiveFinite(definition.totalLength)) {
        error = "PuzzleBoard total length must be finite and greater than zero.";
        return false;
    }
    if (!std::isfinite(definition.minimumSlackRatio) ||
        definition.minimumSlackRatio < 1.0f) {
        error = "PuzzleBoard minimum slack ratio must be finite and at least one.";
        return false;
    }
    if (!IsPositiveFinite(definition.baseWidth) ||
        !IsPositiveFinite(definition.tipWidth) ||
        !std::isfinite(definition.widthVariation) ||
        definition.widthVariation < 0.0f || definition.widthVariation >= 1.0f) {
        error = "PuzzleBoard vessel widths and width variation are invalid.";
        return false;
    }
    if (definition.obstacleTiles.columns != kPuzzleGridColumns ||
        definition.obstacleTiles.rows != kPuzzleGridRows ||
        !HasCellCount(definition.obstacleTiles.columns,
                      definition.obstacleTiles.rows,
                      definition.obstacleTiles.cells.size())) {
        error = "PuzzleBoard obstacle tile grid must match the logical puzzle grid.";
        return false;
    }

    for (const NodeDefinition& node : definition.nodes) {
        if (!IsFinite(node.anchorPosition)) {
            error = "PuzzleBoard nodes require finite anchor positions.";
            return false;
        }
        if (!HasCellCount(node.stamp.columns, node.stamp.rows,
                          node.stamp.cells.size()) ||
            !HasCellCount(node.stamp.columns, node.stamp.rows,
                          node.stamp.occupiedMask.size())) {
            error = "PuzzleBoard node stamps require complete tile and occupancy data.";
            return false;
        }
        if (node.stamp.anchor.column >= node.stamp.columns ||
            node.stamp.anchor.row >= node.stamp.rows ||
            !node.stamp.IsOccupied(node.stamp.anchor.column,
                                   node.stamp.anchor.row)) {
            error = "PuzzleBoard node anchors must select an occupied stamp tile.";
            return false;
        }
        bool hasOccupiedTile = false;
        bool occupiedTileOutsideGrid = false;
        for (std::size_t row = 0; row < node.stamp.rows; ++row) {
            for (std::size_t column = 0; column < node.stamp.columns; ++column) {
                if (!node.stamp.IsOccupied(column, row)) {
                    continue;
                }
                hasOccupiedTile = true;
                if (node.origin.column >
                        (std::numeric_limits<std::size_t>::max)() - column ||
                    node.origin.row >
                        (std::numeric_limits<std::size_t>::max)() - row ||
                    node.origin.column + column >= kPuzzleGridColumns ||
                    node.origin.row + row >= kPuzzleGridRows) {
                    occupiedTileOutsideGrid = true;
                }
            }
        }
        if (!hasOccupiedTile) {
            error = "PuzzleBoard node stamps require at least one occupied tile.";
            return false;
        }
        if (occupiedTileOutsideGrid) {
            error = "PuzzleBoard occupied node tiles must fit inside the logical puzzle grid.";
            return false;
        }
        const Vec2 expectedAnchor{
            (static_cast<float>(node.origin.column + node.stamp.anchor.column) +
             0.5f) * static_cast<float>(kPuzzleTileSize),
            (static_cast<float>(node.origin.row + node.stamp.anchor.row) + 0.5f) *
                static_cast<float>(kPuzzleTileSize),
        };
        if (LengthSquared(node.anchorPosition - expectedAnchor) >
            kLengthEpsilon * kLengthEpsilon) {
            error = "PuzzleBoard node anchor positions must match their resolved tile anchors.";
            return false;
        }
    }

    std::vector<bool> listedRoots(definition.nodes.size(), false);
    for (const std::size_t nodeIndex : definition.rootNodeIndices) {
        if (nodeIndex >= definition.nodes.size()) {
            error = "PuzzleBoard root node index is out of range.";
            return false;
        }
        if (listedRoots[nodeIndex]) {
            error = "PuzzleBoard root node indices must be unique.";
            return false;
        }
        listedRoots[nodeIndex] = true;
    }

    std::vector<bool> listedGoals(definition.nodes.size(), false);
    for (const std::size_t nodeIndex : definition.goalNodeIndices) {
        if (nodeIndex >= definition.nodes.size()) {
            error = "PuzzleBoard goal node index is out of range.";
            return false;
        }
        if (listedGoals[nodeIndex]) {
            error = "PuzzleBoard goal node indices must be unique.";
            return false;
        }
        listedGoals[nodeIndex] = true;
    }
    for (std::size_t nodeIndex = 0; nodeIndex < definition.nodes.size(); ++nodeIndex) {
        if (definition.nodes[nodeIndex].isRoot != listedRoots[nodeIndex] ||
            definition.nodes[nodeIndex].isGoal != listedGoals[nodeIndex]) {
            error = "PuzzleBoard node root and goal flags must match the resolved index lists.";
            return false;
        }
    }

    for (const ConnectionDefinition& connection : definition.connections) {
        if (connection.fromNodeIndex >= definition.nodes.size() ||
            connection.toNodeIndex >= definition.nodes.size()) {
            error = "PuzzleBoard connection node index is out of range.";
            return false;
        }
        if (connection.fromNodeIndex == connection.toNodeIndex) {
            error = "PuzzleBoard connections cannot join a node to itself.";
            return false;
        }
        if (connection.pointCount < kMinimumPointCount ||
            connection.pointCount > kMaximumPointCount) {
            error = "PuzzleBoard connection point count must be between 8 and 12.";
            return false;
        }
        if (!IsPositiveFinite(connection.thicknessScale) ||
            !std::isfinite(connection.followDelaySeconds) ||
            connection.followDelaySeconds < 0.0f ||
            !std::isfinite(connection.initialDirectionDegrees)) {
            error = "PuzzleBoard connection style contains an invalid value.";
            return false;
        }
        if (definition.nodes[connection.fromNodeIndex].maxOutgoing == 0 ||
            definition.nodes[connection.toNodeIndex].maxIncoming == 0) {
            error = "PuzzleBoard connections require source outgoing and target incoming capacity.";
            return false;
        }
        if (LengthSquared(definition.nodes[connection.toNodeIndex].anchorPosition -
                          definition.nodes[connection.fromNodeIndex].anchorPosition) <=
            kLengthEpsilon * kLengthEpsilon) {
            error = "PuzzleBoard connection anchors must have different positions.";
            return false;
        }
    }

    try {
        PuzzleDefinition nextDefinition = definition;
        std::vector<Segment> nextSegments;
        nextSegments.reserve(definition.connections.size());
        std::vector<std::size_t> nextCommittedConnections;
        nextCommittedConnections.reserve(definition.connections.size());
        std::vector<std::size_t> nextActivatedNodes;
        nextActivatedNodes.reserve(definition.nodes.size());
        std::vector<std::size_t> nextIncomingCounts(definition.nodes.size(), 0);
        std::vector<std::size_t> nextOutgoingCounts(definition.nodes.size(), 0);
        std::vector<bool> nextActivatedFlags(definition.nodes.size(), false);
        std::vector<bool> nextCommittedFlags(definition.connections.size(), false);
        for (const std::size_t rootNodeIndex : definition.rootNodeIndices) {
            nextActivatedFlags[rootNodeIndex] = true;
            nextActivatedNodes.push_back(rootNodeIndex);
        }

        definition_ = std::move(nextDefinition);
        segments_ = std::move(nextSegments);
        committedConnectionIndices_ = std::move(nextCommittedConnections);
        activatedNodeIndices_ = std::move(nextActivatedNodes);
        incomingConnectionCounts_ = std::move(nextIncomingCounts);
        outgoingConnectionCounts_ = std::move(nextOutgoingCounts);
        activatedNodes_ = std::move(nextActivatedFlags);
        committedConnections_ = std::move(nextCommittedFlags);
        initialized_ = true;
        solved_ = AreAllGoalsActivated();
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize PuzzleBoard: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize PuzzleBoard because of an unknown error.";
    }

    definition_ = {};
    segments_.clear();
    preview_.reset();
    committedConnectionIndices_.clear();
    activatedNodeIndices_.clear();
    incomingConnectionCounts_.clear();
    outgoingConnectionCounts_.clear();
    activatedNodes_.clear();
    committedConnections_.clear();
    committedLength_ = 0.0f;
    solved_ = false;
    initialized_ = false;
    return false;
}

void PuzzleBoard::Update(const BoardPointerInput& input,
                         const float deltaSeconds) noexcept {
    if (!initialized_) {
        return;
    }

    const float normalizedDeltaSeconds = NormalizeDeltaSeconds(deltaSeconds);
    for (Segment& segment : segments_) {
        if (segment.connectionIndex >= definition_.connections.size()) {
            continue;
        }
        const ConnectionDefinition& connection =
            definition_.connections[segment.connectionIndex];
        segment.tentacle.SetRootAnchor(
            definition_.nodes[connection.fromNodeIndex].anchorPosition);
        segment.tentacle.AttachTip(
            definition_.nodes[connection.toNodeIndex].anchorPosition);
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

    const std::optional<std::size_t> hitNode = HitTestNode(input.position);
    if (!hitNode.has_value() || !CanStartFromNode(*hitNode)) {
        return;
    }
    const std::optional<std::size_t> firstConnection =
        GetFirstAvailableOutgoingConnection(*hitNode);
    if (!firstConnection.has_value()) {
        return;
    }

    StartDrag(*hitNode, *firstConnection);
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
    snapshot.tentacles.reserve(segments_.size() +
                               (preview_.has_value() ? 1u : 0u));

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
    }

    snapshot.totalLength = GetTotalLength();
    snapshot.committedLength = GetCommittedLength();
    snapshot.remainingLength = GetRemainingLength();
    snapshot.reservedLength = GetReservedLength();
    snapshot.activatedNodeIndices = activatedNodeIndices_;
    snapshot.availableSourceNodeIndices.reserve(activatedNodeIndices_.size());
    if (!solved_ && GetRemainingLength() > kLengthEpsilon) {
        for (const std::size_t nodeIndex : activatedNodeIndices_) {
            if (CanStartFromNode(nodeIndex)) {
                snapshot.availableSourceNodeIndices.push_back(nodeIndex);
            }
        }
    }
    snapshot.selectedSourceNodeIndex = GetSelectedSourceNodeIndex();
    snapshot.incomingConnectionCounts = incomingConnectionCounts_;
    snapshot.outgoingConnectionCounts = outgoingConnectionCounts_;
    snapshot.dragging = IsDragging();
    snapshot.retracting = preview_.has_value() &&
                          preview_->state == PreviewState::Retracting;
    snapshot.solved = solved_;
    snapshot.lengthExhausted = IsLengthExhausted();
    snapshot.routeBlocked = IsRouteBlocked();
    return snapshot;
}

float PuzzleBoard::GetRemainingLength() const noexcept {
    if (!initialized_) {
        return 0.0f;
    }
    return (std::max)(0.0f, definition_.totalLength - committedLength_ -
                                GetReservedLength());
}

float PuzzleBoard::GetReservedLength() const noexcept {
    return preview_.has_value() ? preview_->reservedLength : 0.0f;
}

std::optional<std::size_t>
PuzzleBoard::GetSelectedSourceNodeIndex() const noexcept {
    if (!preview_.has_value()) {
        return std::nullopt;
    }
    return preview_->sourceNodeIndex;
}

bool PuzzleBoard::IsDragging() const noexcept {
    return preview_.has_value() && preview_->state == PreviewState::Dragging;
}

bool PuzzleBoard::IsLengthExhausted() const noexcept {
    if (!initialized_ || solved_ || preview_.has_value()) {
        return false;
    }

    const float availableForConnection =
        (std::max)(0.0f, definition_.totalLength - committedLength_);
    bool hasUsableConnection = false;
    for (std::size_t connectionIndex = 0;
         connectionIndex < definition_.connections.size(); ++connectionIndex) {
        if (!CanUseConnection(connectionIndex)) {
            continue;
        }
        hasUsableConnection = true;
        if (availableForConnection + kLengthEpsilon >=
            ConnectionMinimumLength(definition_,
                                    definition_.connections[connectionIndex])) {
            return false;
        }
    }
    return hasUsableConnection;
}

bool PuzzleBoard::IsRouteBlocked() const noexcept {
    if (!initialized_ || solved_ || preview_.has_value()) {
        return false;
    }
    for (std::size_t connectionIndex = 0;
         connectionIndex < definition_.connections.size(); ++connectionIndex) {
        if (CanUseConnection(connectionIndex)) {
            return false;
        }
    }
    return true;
}

void PuzzleBoard::StartDrag(const std::size_t sourceNodeIndex,
                            const std::size_t connectionIndex) noexcept {
    if (sourceNodeIndex >= definition_.nodes.size() ||
        connectionIndex >= definition_.connections.size() ||
        !CanUseConnection(connectionIndex)) {
        return;
    }
    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != sourceNodeIndex) {
        return;
    }

    const float availableLength =
        (std::max)(0.0f, definition_.totalLength - committedLength_);
    if (availableLength <= kLengthEpsilon) {
        return;
    }

    BloodTentacleSettings settings{};
    settings.pointCount = connection.pointCount;
    settings.followDelaySeconds = connection.followDelaySeconds;

    const float radians = connection.initialDirectionDegrees *
                          std::numbers::pi_v<float> / 180.0f;
    const Vec2 initialDirection{std::cos(radians), std::sin(radians)};
    const Vec2 root = definition_.nodes[sourceNodeIndex].anchorPosition;

    Preview next{};
    next.sourceNodeIndex = sourceNodeIndex;
    next.connectionIndex = connectionIndex;
    next.style = MakeStyle(connection, committedConnectionIndices_.size());
    std::string ignoredError;
    if (!next.tentacle.Initialize(root, initialDirection, availableLength,
                                  settings, ignoredError)) {
        return;
    }
    next.tentacle.SetDeployedLength(0.0f);
    next.tentacle.AttachTip(root);
    next.tentacle.Update(1.0f / 15.0f);
    next.tentacle.FollowTip(root);
    preview_ = std::move(next);
}

bool PuzzleBoard::SelectPreviewConnection(const std::size_t connectionIndex,
                                          const Vec2 pointerTarget) noexcept {
    if (!preview_.has_value() ||
        connectionIndex >= definition_.connections.size() ||
        !CanUseConnection(connectionIndex)) {
        return false;
    }
    if (preview_->connectionIndex == connectionIndex) {
        return true;
    }

    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != preview_->sourceNodeIndex) {
        return false;
    }

    const float availableLength =
        (std::max)(0.0f, definition_.totalLength - committedLength_);
    BloodTentacleSettings settings{};
    settings.pointCount = connection.pointCount;
    settings.followDelaySeconds = connection.followDelaySeconds;

    const float radians = connection.initialDirectionDegrees *
                          std::numbers::pi_v<float> / 180.0f;
    const Vec2 initialDirection{std::cos(radians), std::sin(radians)};
    const Vec2 root =
        definition_.nodes[preview_->sourceNodeIndex].anchorPosition;

    BloodTentacle replacement;
    std::string ignoredError;
    if (!replacement.Initialize(root, initialDirection, availableLength,
                                settings, ignoredError)) {
        return false;
    }

    replacement.SetDeployedLength(preview_->reservedLength);
    replacement.AttachTip(root);
    replacement.Update(1.0f / 15.0f);
    replacement.FollowTip(IsFinite(pointerTarget) ? pointerTarget : root);
    preview_->tentacle = std::move(replacement);
    preview_->connectionIndex = connectionIndex;
    preview_->style = MakeStyle(connection, committedConnectionIndices_.size());
    return true;
}

void PuzzleBoard::UpdateDrag(const BoardPointerInput& input,
                             const float deltaSeconds) noexcept {
    if (!preview_.has_value() || preview_->state != PreviewState::Dragging) {
        return;
    }
    if (preview_->sourceNodeIndex >= definition_.nodes.size() ||
        preview_->connectionIndex >= definition_.connections.size()) {
        preview_.reset();
        return;
    }

    const std::size_t sourceNodeIndex = preview_->sourceNodeIndex;
    const Vec2 root = definition_.nodes[sourceNodeIndex].anchorPosition;
    preview_->tentacle.SetRootAnchor(root);

    Vec2 pointerTarget = root;
    if (IsFinite(input.position)) {
        pointerTarget = input.position;
        const std::optional<std::size_t> hoveredConnection =
            FindOutgoingConnectionAt(sourceNodeIndex, pointerTarget);
        if (hoveredConnection.has_value()) {
            (void)SelectPreviewConnection(*hoveredConnection, pointerTarget);
        }

        float desiredLength =
            Length(pointerTarget - root) * definition_.minimumSlackRatio;
        if (hoveredConnection.has_value()) {
            const ConnectionDefinition& hovered =
                definition_.connections[*hoveredConnection];
            desiredLength = (std::max)(
                desiredLength, ConnectionMinimumLength(definition_, hovered));
        }
        desiredLength = std::clamp(desiredLength, 0.0f,
                                   preview_->tentacle.GetMaxLength());
        preview_->reservedLength =
            (std::max)(preview_->reservedLength, desiredLength);
    }

    preview_->tentacle.SetDeployedLength(preview_->reservedLength);
    preview_->tentacle.FollowTip(pointerTarget);
    preview_->tentacle.Update(deltaSeconds);

    if (!input.leftReleased) {
        return;
    }
    const std::optional<std::size_t> selectedConnection =
        FindOutgoingConnectionAt(sourceNodeIndex, input.position);
    if (selectedConnection.has_value() &&
        SelectPreviewConnection(*selectedConnection, input.position) &&
        CanCommitConnection(*selectedConnection, input.position)) {
        CommitConnection(*selectedConnection);
        return;
    }
    BeginRetraction();
}

void PuzzleBoard::UpdateRetraction(const float deltaSeconds) noexcept {
    if (!preview_.has_value() || preview_->state != PreviewState::Retracting) {
        return;
    }
    if (preview_->sourceNodeIndex >= definition_.nodes.size()) {
        preview_.reset();
        return;
    }

    const Vec2 root =
        definition_.nodes[preview_->sourceNodeIndex].anchorPosition;
    preview_->tentacle.SetRootAnchor(root);
    preview_->tentacle.FollowTip(root);

    const float oldTimeRemaining =
        (std::max)(0.0f,
                   kRetractionSeconds - preview_->retractElapsedSeconds);
    preview_->retractElapsedSeconds =
        (std::min)(kRetractionSeconds,
                   preview_->retractElapsedSeconds + deltaSeconds);
    const float newTimeRemaining =
        (std::max)(0.0f,
                   kRetractionSeconds - preview_->retractElapsedSeconds);
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

void PuzzleBoard::CommitConnection(const std::size_t connectionIndex) noexcept {
    if (!preview_.has_value() ||
        connectionIndex >= definition_.connections.size() ||
        preview_->connectionIndex != connectionIndex ||
        !CanUseConnection(connectionIndex)) {
        return;
    }
    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != preview_->sourceNodeIndex) {
        return;
    }

    preview_->tentacle.SetRootAnchor(
        definition_.nodes[connection.fromNodeIndex].anchorPosition);
    preview_->tentacle.AttachTip(
        definition_.nodes[connection.toNodeIndex].anchorPosition);

    Segment segment{};
    segment.tentacle = std::move(preview_->tentacle);
    segment.style = preview_->style;
    segment.connectionIndex = connectionIndex;
    segment.committedLength = preview_->reservedLength;
    committedLength_ =
        (std::min)(definition_.totalLength,
                   committedLength_ + segment.committedLength);
    segments_.push_back(std::move(segment));
    preview_.reset();

    committedConnections_[connectionIndex] = true;
    committedConnectionIndices_.push_back(connectionIndex);
    ++outgoingConnectionCounts_[connection.fromNodeIndex];
    ++incomingConnectionCounts_[connection.toNodeIndex];
    if (!activatedNodes_[connection.toNodeIndex]) {
        activatedNodes_[connection.toNodeIndex] = true;
        activatedNodeIndices_.push_back(connection.toNodeIndex);
    }
    solved_ = AreAllGoalsActivated();
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
    if (preview_->sourceNodeIndex < definition_.nodes.size()) {
        preview_->tentacle.FollowTip(
            definition_.nodes[preview_->sourceNodeIndex].anchorPosition);
    }
}

bool PuzzleBoard::CanCommitConnection(const std::size_t connectionIndex,
                                      const Vec2 pointerPosition) const noexcept {
    if (!preview_.has_value() || preview_->state != PreviewState::Dragging ||
        !IsFinite(pointerPosition) ||
        connectionIndex >= definition_.connections.size() ||
        !CanUseConnection(connectionIndex)) {
        return false;
    }
    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != preview_->sourceNodeIndex ||
        !PointHitsNodeMask(pointerPosition, connection.toNodeIndex)) {
        return false;
    }

    const float requiredLength = ConnectionMinimumLength(definition_, connection);
    if (preview_->reservedLength + kLengthEpsilon < requiredLength) {
        return false;
    }

    const TentacleStyle style =
        MakeStyle(connection, committedConnectionIndices_.size());
    const float pixelGridPadding =
        std::isfinite(style.pixelGridSize)
            ? (std::max)(0.0f, style.pixelGridSize)
            : kDefaultTentaclePixelGridSize;
    const float clearance =
        (std::max)(style.baseWidth, style.tipWidth) * 0.5f +
        pixelGridPadding;
    return !IsConnectionBlockedBySolidTiles(
        definition_.nodes[connection.fromNodeIndex].anchorPosition,
        definition_.nodes[connection.toNodeIndex].anchorPosition, clearance);
}

std::optional<std::size_t>
PuzzleBoard::HitTestNode(const Vec2 point) const noexcept {
    if (!IsFinite(point)) {
        return std::nullopt;
    }
    for (std::size_t nodeIndex = 0; nodeIndex < definition_.nodes.size();
         ++nodeIndex) {
        if (PointHitsNodeMask(point, nodeIndex)) {
            return nodeIndex;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> PuzzleBoard::FindOutgoingConnectionAt(
    const std::size_t sourceNodeIndex, const Vec2 point) const noexcept {
    if (!initialized_ || sourceNodeIndex >= definition_.nodes.size() ||
        !IsFinite(point)) {
        return std::nullopt;
    }

    for (std::size_t connectionIndex = 0;
         connectionIndex < definition_.connections.size(); ++connectionIndex) {
        const ConnectionDefinition& connection =
            definition_.connections[connectionIndex];
        if (connection.fromNodeIndex == sourceNodeIndex &&
            CanUseConnection(connectionIndex) &&
            PointHitsNodeMask(point, connection.toNodeIndex)) {
            return connectionIndex;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> PuzzleBoard::GetFirstAvailableOutgoingConnection(
    const std::size_t sourceNodeIndex) const noexcept {
    if (!initialized_ || sourceNodeIndex >= definition_.nodes.size()) {
        return std::nullopt;
    }
    for (std::size_t connectionIndex = 0;
         connectionIndex < definition_.connections.size(); ++connectionIndex) {
        if (definition_.connections[connectionIndex].fromNodeIndex ==
                sourceNodeIndex &&
            CanUseConnection(connectionIndex)) {
            return connectionIndex;
        }
    }
    return std::nullopt;
}

bool PuzzleBoard::CanUseConnection(const std::size_t connectionIndex) const noexcept {
    if (!initialized_ || connectionIndex >= definition_.connections.size() ||
        connectionIndex >= committedConnections_.size() ||
        committedConnections_[connectionIndex]) {
        return false;
    }

    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex >= definition_.nodes.size() ||
        connection.toNodeIndex >= definition_.nodes.size() ||
        !IsNodeActivated(connection.fromNodeIndex)) {
        return false;
    }
    return outgoingConnectionCounts_[connection.fromNodeIndex] <
               definition_.nodes[connection.fromNodeIndex].maxOutgoing &&
           incomingConnectionCounts_[connection.toNodeIndex] <
               definition_.nodes[connection.toNodeIndex].maxIncoming;
}

bool PuzzleBoard::CanStartFromNode(const std::size_t nodeIndex) const noexcept {
    return IsNodeActivated(nodeIndex) &&
           GetFirstAvailableOutgoingConnection(nodeIndex).has_value();
}

bool PuzzleBoard::IsNodeActivated(const std::size_t nodeIndex) const noexcept {
    return initialized_ && nodeIndex < activatedNodes_.size() &&
           activatedNodes_[nodeIndex];
}

bool PuzzleBoard::AreAllGoalsActivated() const noexcept {
    if (!initialized_ || definition_.goalNodeIndices.empty()) {
        return false;
    }
    return std::all_of(
        definition_.goalNodeIndices.begin(), definition_.goalNodeIndices.end(),
        [this](const std::size_t nodeIndex) {
            return nodeIndex < activatedNodes_.size() &&
                   activatedNodes_[nodeIndex];
        });
}

bool PuzzleBoard::PointHitsNodeMask(const Vec2 point,
                                    const std::size_t nodeIndex) const noexcept {
    if (!IsFinite(point) || nodeIndex >= definition_.nodes.size() ||
        point.x < 0.0f || point.y < 0.0f ||
        point.x >= static_cast<float>(kPuzzleCanvasWidth) ||
        point.y >= static_cast<float>(kPuzzleCanvasHeight)) {
        return false;
    }

    const std::size_t column =
        static_cast<std::size_t>(point.x / static_cast<float>(kPuzzleTileSize));
    const std::size_t row =
        static_cast<std::size_t>(point.y / static_cast<float>(kPuzzleTileSize));
    const NodeDefinition& node = definition_.nodes[nodeIndex];
    if (column < node.origin.column || row < node.origin.row) {
        return false;
    }
    const std::size_t localColumn = column - node.origin.column;
    const std::size_t localRow = row - node.origin.row;
    return node.stamp.IsOccupied(localColumn, localRow);
}

bool PuzzleBoard::IsConnectionBlockedBySolidTiles(
    const Vec2 start, const Vec2 end, const float clearance) const noexcept {
    if (!IsFinite(start) || !IsFinite(end) || !std::isfinite(clearance) ||
        clearance < 0.0f ||
        definition_.obstacleTiles.columns != kPuzzleGridColumns ||
        definition_.obstacleTiles.rows != kPuzzleGridRows ||
        !HasCellCount(definition_.obstacleTiles.columns,
                      definition_.obstacleTiles.rows,
                      definition_.obstacleTiles.cells.size())) {
        return true;
    }

    const float tileSize = static_cast<float>(kPuzzleTileSize);
    const float expandedSize = tileSize + clearance * 2.0f;
    for (std::size_t row = 0; row < definition_.obstacleTiles.rows; ++row) {
        for (std::size_t column = 0;
             column < definition_.obstacleTiles.columns; ++column) {
            if (definition_.obstacleTiles.cells[
                    row * definition_.obstacleTiles.columns + column] == 0) {
                continue;
            }
            const Vec2 center{
                (static_cast<float>(column) + 0.5f) * tileSize,
                (static_cast<float>(row) + 0.5f) * tileSize,
            };
            if (SegmentIntersectsRectangle(start, end, center, expandedSize,
                                           expandedSize)) {
                return true;
            }
        }
    }
    return false;
}

TentacleStyle PuzzleBoard::MakeStyle(
    const ConnectionDefinition& connection,
    const std::size_t connectionIndex) const noexcept {
    TentacleStyle style{};
    style.baseWidth = definition_.baseWidth * connection.thicknessScale;
    style.tipWidth = definition_.tipWidth * connection.thicknessScale;
    style.widthVariation = definition_.widthVariation;
    style.widthPhase = std::fmod(static_cast<float>(connectionIndex) * 1.6180339f,
                                 2.0f * std::numbers::pi_v<float>);
    style.color = definition_.vesselColor;
    return style;
}

} // namespace object_connect
