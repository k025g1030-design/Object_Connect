#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include "ObjectConnect/Geometry/Geometry2D.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
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

[[nodiscard]] float ConnectionMinimumLength(const PuzzleDefinition& definition,
                                            const ConnectionDefinition& connection) noexcept {
    if (connection.fromNodeIndex >= definition.nodes.size() ||
        connection.toNodeIndex >= definition.nodes.size()) {
        return 0.0f;
    }
    const Vec2 from = definition.nodes[connection.fromNodeIndex].position;
    const Vec2 to = definition.nodes[connection.toNodeIndex].position;
    return Length(to - from) * definition.minimumSlackRatio;
}

[[nodiscard]] bool IsPositiveFinite(const float value) noexcept {
    return std::isfinite(value) && value > 0.0f;
}

} // namespace

bool PuzzleBoard::Initialize(const PuzzleDefinition& definition, std::string& error) {
    definition_ = {};
    segments_.clear();
    preview_.reset();
    committedConnectionIndices_.clear();
    visitedNodeIndices_.clear();
    currentNodeIndex_ = 0;
    committedLength_ = 0.0f;
    solved_ = false;
    initialized_ = false;
    error.clear();

    if (definition.nodes.empty()) {
        error = "PuzzleBoard requires at least one node.";
        return false;
    }
    if (definition.connections.empty()) {
        error = "PuzzleBoard requires at least one connection.";
        return false;
    }
    if (definition.startNodeIndex >= definition.nodes.size()) {
        error = "PuzzleBoard start node index is out of range.";
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
    if (!IsPositiveFinite(definition.baseWidth) || !IsPositiveFinite(definition.tipWidth) ||
        !std::isfinite(definition.widthVariation) || definition.widthVariation < 0.0f ||
        definition.widthVariation >= 1.0f) {
        error = "PuzzleBoard vessel widths and width variation are invalid.";
        return false;
    }

    for (const NodeDefinition& node : definition.nodes) {
        if (!IsFinite(node.position) || !IsPositiveFinite(node.radius)) {
            error = "PuzzleBoard nodes require finite positions and positive radii.";
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
        if (LengthSquared(definition.nodes[connection.toNodeIndex].position -
                          definition.nodes[connection.fromNodeIndex].position) <=
            kLengthEpsilon * kLengthEpsilon) {
            error = "PuzzleBoard connection endpoints must have different positions.";
            return false;
        }
    }
    const bool startHasConnection = std::any_of(
        definition.connections.begin(), definition.connections.end(),
        [&definition](const ConnectionDefinition& connection) {
            return connection.fromNodeIndex == definition.startNodeIndex;
        });
    if (!startHasConnection) {
        error = "PuzzleBoard start node must have at least one outgoing connection.";
        return false;
    }

    for (const ObstacleDefinition& obstacle : definition.obstacles) {
        if (!IsFinite(obstacle.center)) {
            error = "PuzzleBoard obstacle position must be finite.";
            return false;
        }
        switch (obstacle.shape) {
        case ObstacleShape::Rectangle:
            if (!IsPositiveFinite(obstacle.width) || !IsPositiveFinite(obstacle.height)) {
                error = "PuzzleBoard rectangle obstacles require positive dimensions.";
                return false;
            }
            break;
        case ObstacleShape::Circle:
            if (!IsPositiveFinite(obstacle.radius)) {
                error = "PuzzleBoard circle obstacles require a positive radius.";
                return false;
            }
            break;
        default:
            error = "PuzzleBoard obstacle has an unknown shape.";
            return false;
        }
    }

    try {
        PuzzleDefinition nextDefinition = definition;
        std::vector<Segment> nextSegments;
        nextSegments.reserve(definition.connections.size());
        std::vector<std::size_t> nextCommittedConnections;
        nextCommittedConnections.reserve(definition.connections.size());
        std::vector<std::size_t> nextVisitedNodes;
        nextVisitedNodes.reserve(definition.connections.size() + 1);
        nextVisitedNodes.push_back(definition.startNodeIndex);
        definition_ = std::move(nextDefinition);
        segments_ = std::move(nextSegments);
        committedConnectionIndices_ = std::move(nextCommittedConnections);
        visitedNodeIndices_ = std::move(nextVisitedNodes);
        currentNodeIndex_ = definition_.startNodeIndex;
        initialized_ = true;
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize PuzzleBoard: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize PuzzleBoard because of an unknown error.";
    }
    definition_ = {};
    segments_.clear();
    committedConnectionIndices_.clear();
    visitedNodeIndices_.clear();
    return false;
}

void PuzzleBoard::Update(const BoardPointerInput& input, const float deltaSeconds) noexcept {
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
        segment.tentacle.SetRootAnchor(definition_.nodes[connection.fromNodeIndex].position);
        segment.tentacle.AttachTip(definition_.nodes[connection.toNodeIndex].position);
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
    // A target is not known until the player drags over one. The first outgoing
    // edge supplies the initial preview settings; SelectPreviewConnection swaps
    // them to the chosen edge before commit.
    const std::optional<std::size_t> firstConnection = GetFirstOutgoingConnection();
    if (!firstConnection.has_value() || !hitNode.has_value() ||
        *hitNode != currentNodeIndex_) {
        return;
    }

    StartDrag(*firstConnection);
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
    snapshot.remainingLength = GetRemainingLength();
    snapshot.reservedLength = GetReservedLength();
    if (initialized_ && !solved_ && HasOutgoingConnection(currentNodeIndex_)) {
        snapshot.activeNodeIndex = currentNodeIndex_;
    }
    snapshot.dragging = IsDragging();
    snapshot.retracting = preview_.has_value() &&
                          preview_->state == PreviewState::Retracting;
    snapshot.solved = solved_;
    snapshot.lengthExhausted = IsLengthExhausted();
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

bool PuzzleBoard::IsDragging() const noexcept {
    return preview_.has_value() && preview_->state == PreviewState::Dragging;
}

bool PuzzleBoard::IsLengthExhausted() const noexcept {
    if (!initialized_ || solved_) {
        return false;
    }
    if (GetRemainingLength() <= kLengthEpsilon) {
        return true;
    }
    if (!HasOutgoingConnection(currentNodeIndex_)) {
        return false;
    }
    const float availableForConnection =
        (std::max)(0.0f, definition_.totalLength - committedLength_);
    for (const ConnectionDefinition& connection : definition_.connections) {
        if (connection.fromNodeIndex == currentNodeIndex_ &&
            availableForConnection + kLengthEpsilon >=
                ConnectionMinimumLength(definition_, connection)) {
            return false;
        }
    }
    return true;
}

void PuzzleBoard::StartDrag(const std::size_t connectionIndex) noexcept {
    if (connectionIndex >= definition_.connections.size()) {
        return;
    }
    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
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
    const Vec2 root = definition_.nodes[connection.fromNodeIndex].position;

    Preview next{};
    next.connectionIndex = connectionIndex;
    next.style = MakeStyle(connection, committedConnectionIndices_.size());
    std::string ignoredError;
    if (!next.tentacle.Initialize(root, initialDirection, availableLength, settings,
                                  ignoredError)) {
        return;
    }
    next.tentacle.SetDeployedLength(0.0f);
    // Initialize lays out the chain at its maximum reach. Collapse it once while
    // both ends are at the source so a new drag visually grows out of the node
    // instead of flashing a full-budget strand for one frame.
    next.tentacle.AttachTip(root);
    next.tentacle.Update(1.0f / 15.0f);
    next.tentacle.FollowTip(root);
    preview_ = std::move(next);
}

bool PuzzleBoard::SelectPreviewConnection(const std::size_t connectionIndex,
                                          const Vec2 pointerTarget) noexcept {
    if (!preview_.has_value() || connectionIndex >= definition_.connections.size()) {
        return false;
    }
    if (preview_->connectionIndex == connectionIndex) {
        return true;
    }

    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != currentNodeIndex_) {
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
    const Vec2 root = definition_.nodes[currentNodeIndex_].position;

    // pointCount and follow delay belong to an edge, so selecting another target
    // rebuilds only the temporary preview while preserving its length reservation.
    BloodTentacle replacement;
    std::string ignoredError;
    if (!replacement.Initialize(root, initialDirection, availableLength, settings,
                                ignoredError)) {
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
    if (preview_->connectionIndex >= definition_.connections.size()) {
        preview_.reset();
        return;
    }

    const Vec2 root = definition_.nodes[currentNodeIndex_].position;
    preview_->tentacle.SetRootAnchor(root);

    Vec2 pointerTarget = root;
    if (IsFinite(input.position)) {
        pointerTarget = input.position;
        const std::optional<std::size_t> hoveredConnection =
            FindOutgoingConnectionAt(pointerTarget);
        if (hoveredConnection.has_value()) {
            (void)SelectPreviewConnection(*hoveredConnection, pointerTarget);
        }

        float desiredLength = Length(pointerTarget - root) * definition_.minimumSlackRatio;
        if (hoveredConnection.has_value()) {
            const ConnectionDefinition& hovered =
                definition_.connections[*hoveredConnection];
            desiredLength = (std::max)(desiredLength,
                                       ConnectionMinimumLength(definition_, hovered));
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
        FindOutgoingConnectionAt(input.position);
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
    if (currentNodeIndex_ >= definition_.nodes.size()) {
        preview_.reset();
        return;
    }

    const Vec2 root = definition_.nodes[currentNodeIndex_].position;
    preview_->tentacle.SetRootAnchor(root);
    preview_->tentacle.FollowTip(root);

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

void PuzzleBoard::CommitConnection(const std::size_t connectionIndex) noexcept {
    if (!preview_.has_value() || connectionIndex >= definition_.connections.size() ||
        preview_->connectionIndex != connectionIndex) {
        return;
    }
    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != currentNodeIndex_) {
        return;
    }

    preview_->tentacle.SetRootAnchor(
        definition_.nodes[connection.fromNodeIndex].position);
    preview_->tentacle.AttachTip(definition_.nodes[connection.toNodeIndex].position);

    Segment segment{};
    segment.tentacle = std::move(preview_->tentacle);
    segment.style = preview_->style;
    segment.connectionIndex = connectionIndex;
    segment.committedLength = preview_->reservedLength;
    committedLength_ = (std::min)(definition_.totalLength,
                                  committedLength_ + segment.committedLength);
    segments_.push_back(std::move(segment));
    preview_.reset();

    committedConnectionIndices_.push_back(connectionIndex);
    currentNodeIndex_ = connection.toNodeIndex;
    visitedNodeIndices_.push_back(currentNodeIndex_);
    solved_ = !HasOutgoingConnection(currentNodeIndex_);
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
    if (currentNodeIndex_ < definition_.nodes.size()) {
        preview_->tentacle.FollowTip(definition_.nodes[currentNodeIndex_].position);
    }
}

bool PuzzleBoard::CanCommitConnection(const std::size_t connectionIndex,
                                      const Vec2 pointerPosition) const noexcept {
    if (!preview_.has_value() || preview_->state != PreviewState::Dragging ||
        !IsFinite(pointerPosition) || connectionIndex >= definition_.connections.size()) {
        return false;
    }
    const ConnectionDefinition& connection = definition_.connections[connectionIndex];
    if (connection.fromNodeIndex != currentNodeIndex_) {
        return false;
    }

    const NodeDefinition& fromNode = definition_.nodes[connection.fromNodeIndex];
    const NodeDefinition& toNode = definition_.nodes[connection.toNodeIndex];
    if (!PointInCircle(pointerPosition, toNode.position, toNode.radius)) {
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
        (std::max)(style.baseWidth, style.tipWidth) * 0.5f + pixelGridPadding;
    return !IsConnectionBlocked(fromNode.position, toNode.position,
                                definition_.obstacles, clearance);
}

std::optional<std::size_t> PuzzleBoard::HitTestNode(const Vec2 point) const noexcept {
    if (!IsFinite(point)) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < definition_.nodes.size(); ++index) {
        const NodeDefinition& node = definition_.nodes[index];
        if (PointInCircle(point, node.position, node.radius)) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t>
PuzzleBoard::FindOutgoingConnectionAt(const Vec2 point) const noexcept {
    if (!initialized_ || !IsFinite(point)) {
        return std::nullopt;
    }

    std::optional<std::size_t> bestConnection;
    float bestDistanceSquared = 0.0f;
    for (std::size_t index = 0; index < definition_.connections.size(); ++index) {
        const ConnectionDefinition& connection = definition_.connections[index];
        if (connection.fromNodeIndex != currentNodeIndex_ ||
            connection.toNodeIndex >= definition_.nodes.size()) {
            continue;
        }
        const NodeDefinition& target = definition_.nodes[connection.toNodeIndex];
        if (!PointInCircle(point, target.position, target.radius)) {
            continue;
        }
        const float distanceSquared = LengthSquared(point - target.position);
        if (!bestConnection.has_value() || distanceSquared < bestDistanceSquared) {
            bestConnection = index;
            bestDistanceSquared = distanceSquared;
        }
    }
    return bestConnection;
}

std::optional<std::size_t> PuzzleBoard::GetFirstOutgoingConnection() const noexcept {
    if (!initialized_) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < definition_.connections.size(); ++index) {
        if (definition_.connections[index].fromNodeIndex == currentNodeIndex_) {
            return index;
        }
    }
    return std::nullopt;
}

bool PuzzleBoard::HasOutgoingConnection(const std::size_t nodeIndex) const noexcept {
    return std::any_of(
        definition_.connections.begin(), definition_.connections.end(),
        [nodeIndex](const ConnectionDefinition& connection) {
            return connection.fromNodeIndex == nodeIndex;
        });
}

TentacleStyle PuzzleBoard::MakeStyle(const ConnectionDefinition& connection,
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
