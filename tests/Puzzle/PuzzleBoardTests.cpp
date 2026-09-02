#include "TestSupport.hpp"

#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace object_connect::tests {
namespace {

constexpr float kFrameSeconds = 1.0f / 60.0f;

[[nodiscard]] TileGrid EmptyGrid() {
    TileGrid grid{};
    grid.columns = kPuzzleGridColumns;
    grid.rows = kPuzzleGridRows;
    grid.cells.assign(grid.columns * grid.rows, 0);
    return grid;
}

[[nodiscard]] TileStamp CrossStamp() {
    TileStamp stamp{};
    stamp.columns = 3;
    stamp.rows = 3;
    stamp.cells = {
        0, 1, 0,
        1, 1, 1,
        0, 1, 0,
    };
    stamp.occupiedMask = {
        0, 1, 0,
        1, 1, 1,
        0, 1, 0,
    };
    stamp.anchor = {1, 1};
    return stamp;
}

[[nodiscard]] NodeDefinition MakeNode(const char* id,
                                      const TileCoordinate origin,
                                      const bool isRoot,
                                      const bool isGoal,
                                      const std::size_t maxIncoming,
                                      const std::size_t maxOutgoing) {
    NodeDefinition node{};
    node.id = id;
    node.typeId = "cross";
    node.typeIndex = 0;
    node.origin = origin;
    node.stamp = CrossStamp();
    node.bounds = {origin.column, origin.row, node.stamp.columns, node.stamp.rows};
    node.anchorPosition = {
        (static_cast<float>(origin.column + node.stamp.anchor.column) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
        (static_cast<float>(origin.row + node.stamp.anchor.row) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
    };
    node.isRoot = isRoot;
    node.isGoal = isGoal;
    node.maxIncoming = maxIncoming;
    node.maxOutgoing = maxOutgoing;
    return node;
}

[[nodiscard]] ConnectionDefinition MakeConnection(
    const char* id, const PuzzleDefinition& puzzle,
    const std::size_t fromNodeIndex, const std::size_t toNodeIndex,
    const std::size_t pointCount = 8) {
    ConnectionDefinition connection{};
    connection.id = id;
    connection.fromNodeId = puzzle.nodes[fromNodeIndex].id;
    connection.toNodeId = puzzle.nodes[toNodeIndex].id;
    connection.fromNodeIndex = fromNodeIndex;
    connection.toNodeIndex = toNodeIndex;
    connection.pointCount = pointCount;
    connection.thicknessScale = 1.0f;
    return connection;
}

[[nodiscard]] PuzzleDefinition MakeBasePuzzle(const char* id,
                                              const float totalLength) {
    PuzzleDefinition puzzle{};
    puzzle.id = id;
    puzzle.title = id;
    puzzle.totalLength = totalLength;
    puzzle.minimumSlackRatio = 1.0f;
    puzzle.baseWidth = 16.0f;
    puzzle.tipWidth = 16.0f;
    puzzle.widthVariation = 0.0f;
    puzzle.backgroundTiles = EmptyGrid();
    puzzle.obstacleTiles = EmptyGrid();
    return puzzle;
}

[[nodiscard]] PuzzleDefinition MakeBranchMergePuzzle(
    const float totalLength = 3000.0f) {
    PuzzleDefinition puzzle = MakeBasePuzzle("branch_merge", totalLength);
    puzzle.nodes = {
        MakeNode("root_a", {4, 8}, true, false, 0, 3),
        MakeNode("root_b", {4, 28}, true, false, 0, 1),
        MakeNode("branch", {18, 8}, false, false, 1, 1),
        MakeNode("merge", {32, 18}, false, false, 2, 1),
        MakeNode("goal_a", {32, 4}, false, true, 1, 0),
        MakeNode("goal_b", {52, 18}, false, true, 1, 0),
    };
    puzzle.rootNodeIndices = {0, 1};
    puzzle.goalNodeIndices = {4, 5};
    puzzle.connections = {
        MakeConnection("root_a_branch", puzzle, 0, 2, 8),
        MakeConnection("root_a_goal_a", puzzle, 0, 4, 9),
        MakeConnection("root_b_merge", puzzle, 1, 3, 10),
        MakeConnection("branch_merge", puzzle, 2, 3, 11),
        MakeConnection("merge_goal_b", puzzle, 3, 5, 12),
        MakeConnection("root_a_merge", puzzle, 0, 3, 8),
    };
    return puzzle;
}

[[nodiscard]] PuzzleDefinition MakeLineOfSightPuzzle(
    const std::size_t obstacleRow) {
    PuzzleDefinition puzzle = MakeBasePuzzle("line_of_sight", 1000.0f);
    puzzle.nodes = {
        MakeNode("root", {4, 9}, true, false, 0, 1),
        MakeNode("goal", {20, 9}, false, true, 1, 0),
    };
    puzzle.rootNodeIndices = {0};
    puzzle.goalNodeIndices = {1};
    puzzle.connections = {MakeConnection("root_goal", puzzle, 0, 1)};
    if (obstacleRow < puzzle.obstacleTiles.rows) {
        puzzle.obstacleTiles.cells[
            obstacleRow * puzzle.obstacleTiles.columns + 12] = 1;
    }
    return puzzle;
}

[[nodiscard]] PuzzleDefinition MakeExhaustionPuzzle() {
    PuzzleDefinition puzzle = MakeBasePuzzle("exhaustion", 200.0f);
    puzzle.nodes = {
        MakeNode("far_root", {4, 5}, true, false, 0, 1),
        MakeNode("near_root", {4, 20}, true, false, 0, 1),
        MakeNode("far_goal", {40, 5}, false, true, 1, 0),
        MakeNode("near_goal", {12, 20}, false, true, 1, 0),
        MakeNode("inactive_relay", {38, 5}, false, false, 1, 1),
    };
    puzzle.rootNodeIndices = {0, 1};
    puzzle.goalNodeIndices = {2, 3};
    puzzle.connections = {
        MakeConnection("far_front", puzzle, 0, 4),
        MakeConnection("near", puzzle, 1, 3),
        MakeConnection("cheap_inactive_tail", puzzle, 4, 2),
    };
    return puzzle;
}

[[nodiscard]] PuzzleDefinition MakeRouteBlockedPuzzle() {
    PuzzleDefinition puzzle = MakeBasePuzzle("route_blocked", 1000.0f);
    puzzle.nodes = {
        MakeNode("root", {4, 5}, true, false, 0, 1),
        MakeNode("near_goal", {12, 5}, false, true, 1, 0),
        MakeNode("relay", {4, 20}, false, false, 1, 1),
        MakeNode("far_goal", {12, 20}, false, true, 1, 0),
    };
    puzzle.rootNodeIndices = {0};
    puzzle.goalNodeIndices = {1, 3};
    puzzle.connections = {
        MakeConnection("root_near_goal", puzzle, 0, 1),
        MakeConnection("root_relay", puzzle, 0, 2),
        MakeConnection("relay_far_goal", puzzle, 2, 3),
    };
    return puzzle;
}

[[nodiscard]] BoardPointerInput PressAt(const Vec2 position) {
    BoardPointerInput input{};
    input.position = position;
    input.leftPressed = true;
    input.leftHeld = true;
    return input;
}

[[nodiscard]] BoardPointerInput DragTo(const Vec2 position) {
    BoardPointerInput input{};
    input.position = position;
    input.leftHeld = true;
    return input;
}

[[nodiscard]] BoardPointerInput ReleaseAt(const Vec2 position) {
    BoardPointerInput input{};
    input.position = position;
    input.leftReleased = true;
    return input;
}

void Commit(PuzzleBoard& board, const PuzzleDefinition& puzzle,
            const std::size_t fromNodeIndex, const std::size_t toNodeIndex) {
    board.Update(PressAt(puzzle.nodes[fromNodeIndex].anchorPosition), kFrameSeconds);
    board.Update(ReleaseAt(puzzle.nodes[toNodeIndex].anchorPosition), kFrameSeconds);
}

void AdvanceRetraction(PuzzleBoard& board) {
    for (int step = 0; step < 8; ++step) {
        board.Update({}, 0.05f);
    }
}

[[nodiscard]] bool Contains(const std::vector<std::size_t>& values,
                            const std::size_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool AllZero(const std::vector<std::size_t>& values) {
    return std::all_of(values.begin(), values.end(),
                       [](const std::size_t value) { return value == 0; });
}

void ExpectLengthInvariant(TestContext& context,
                           const PuzzleBoardSnapshot& snapshot,
                           const char* description) {
    context.Expect(
        NearlyEqual(snapshot.totalLength,
                    snapshot.committedLength + snapshot.reservedLength +
                        snapshot.remainingLength,
                    0.02f),
        description);
}

void TestInitializationRootsMasksAndSnapshot(TestContext& context) {
    const PuzzleDefinition puzzle = MakeBranchMergePuzzle();
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "multi-root branch-and-merge board initializes");
    context.Expect(error.empty(), "successful board initialization clears its error");

    const PuzzleBoardSnapshot initial = board.MakeSnapshot();
    context.Expect(initial.activatedNodeIndices == std::vector<std::size_t>{0, 1},
                   "all roots are activated in authored order at initialization");
    context.Expect(initial.availableSourceNodeIndices ==
                       std::vector<std::size_t>{0, 1},
                   "all activated roots with authorized capacity are available sources");
    context.Expect(!initial.selectedSourceNodeIndex.has_value(),
                   "an idle board has no selected source");
    context.Expect(initial.incomingConnectionCounts.size() == puzzle.nodes.size() &&
                       initial.outgoingConnectionCounts.size() == puzzle.nodes.size() &&
                       AllZero(initial.incomingConnectionCounts) &&
                       AllZero(initial.outgoingConnectionCounts),
                   "per-node incoming and outgoing usage start at zero");
    context.Expect(initial.tentacles.empty() && !initial.solved,
                   "a fresh board has no vessel segments and is not solved");
    ExpectLengthInvariant(context, initial,
                          "the untouched board preserves the global length invariant");

    board.Update(PressAt(puzzle.nodes[2].anchorPosition), kFrameSeconds);
    context.Expect(!board.IsDragging(),
                   "an inactive node cannot become a preview source");

    const NodeDefinition& root = puzzle.nodes[0];
    const Vec2 emptyMaskTile{
        (static_cast<float>(root.origin.column) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
        (static_cast<float>(root.origin.row) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
    };
    board.Update(PressAt(emptyMaskTile), kFrameSeconds);
    context.Expect(!board.IsDragging(),
                   "transparent tiles inside node bounds do not pass hit testing");

    board.Update(PressAt(root.anchorPosition), kFrameSeconds);
    context.Expect(board.IsDragging() &&
                       board.GetSelectedSourceNodeIndex() == std::optional<std::size_t>{0},
                   "an occupied tile on an available root selects that source");
    board.CancelDrag(true);

    PuzzleDefinition missingRoots = puzzle;
    missingRoots.rootNodeIndices.clear();
    context.Expect(!board.Initialize(missingRoots, error),
                   "a board without resolved roots is rejected");

    PuzzleDefinition missingGoals = puzzle;
    missingGoals.goalNodeIndices.clear();
    context.Expect(!board.Initialize(missingGoals, error),
                   "a board without resolved goals is rejected");

    PuzzleDefinition duplicateRoot = puzzle;
    duplicateRoot.rootNodeIndices.push_back(0);
    context.Expect(!board.Initialize(duplicateRoot, error),
                   "duplicate resolved root indices are rejected");

    PuzzleDefinition mismatchedRole = puzzle;
    mismatchedRole.nodes[0].isRoot = false;
    context.Expect(!board.Initialize(mismatchedRole, error),
                   "node role flags must match the resolved role indices");

    PuzzleDefinition malformedStamp = puzzle;
    malformedStamp.nodes[0].stamp.occupiedMask.pop_back();
    context.Expect(!board.Initialize(malformedStamp, error),
                   "incomplete node occupancy masks are rejected");

    PuzzleDefinition mismatchedAnchor = puzzle;
    mismatchedAnchor.nodes[0].anchorPosition.x +=
        static_cast<float>(kPuzzleTileSize);
    context.Expect(!board.Initialize(mismatchedAnchor, error),
                   "node anchors must remain aligned with their occupied stamp tile");

    PuzzleDefinition transparentTailOutside = puzzle;
    NodeDefinition edgeDecoy =
        MakeNode("edge_decoy", {77, 40}, false, false, 0, 0);
    edgeDecoy.stamp.columns = 4;
    edgeDecoy.stamp.cells = {
        0, 1, 0, 0,
        1, 1, 1, 0,
        0, 1, 0, 0,
    };
    edgeDecoy.stamp.occupiedMask = {
        0, 1, 0, 0,
        1, 1, 1, 0,
        0, 1, 0, 0,
    };
    edgeDecoy.bounds.columns = edgeDecoy.stamp.columns;
    transparentTailOutside.nodes.push_back(std::move(edgeDecoy));
    context.Expect(board.Initialize(transparentTailOutside, error),
                   "transparent stamp tail tiles may extend beyond the canvas");
}

void TestBranchMergeCapacitiesAndGoals(TestContext& context) {
    const PuzzleDefinition puzzle = MakeBranchMergePuzzle();
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "capacity test board initializes");

    Commit(board, puzzle, 0, 2);
    PuzzleBoardSnapshot snapshot = board.MakeSnapshot();
    context.Expect(board.GetCompletedConnectionCount() == 1 &&
                       board.GetCommittedConnectionIndices() ==
                           std::vector<std::size_t>{0},
                   "the chosen authorized edge commits once");
    context.Expect(snapshot.activatedNodeIndices ==
                       std::vector<std::size_t>{0, 1, 2},
                   "a committed target activates without consuming its source");
    context.Expect(Contains(snapshot.availableSourceNodeIndices, 0) &&
                       Contains(snapshot.availableSourceNodeIndices, 2),
                   "the branch source remains available and its target can extend");
    context.Expect(snapshot.outgoingConnectionCounts[0] == 1 &&
                       snapshot.incomingConnectionCounts[2] == 1,
                   "commit consumes one source-out and one target-in slot");
    context.Expect(snapshot.tentacles.size() == 1 &&
                       snapshot.tentacles.front().points.front() ==
                           puzzle.nodes[0].anchorPosition &&
                       snapshot.tentacles.front().points.back() ==
                           puzzle.nodes[2].anchorPosition,
                   "an attached segment remains pinned to both tile anchors");

    Commit(board, puzzle, 0, 2);
    context.Expect(board.GetCompletedConnectionCount() == 1 &&
                       board.MakeSnapshot().retracting,
                   "a committed edge cannot be selected a second time");
    board.CancelDrag(true);

    Commit(board, puzzle, 0, 4);
    snapshot = board.MakeSnapshot();
    context.Expect(Contains(snapshot.activatedNodeIndices, 4) && !snapshot.solved,
                   "activating one of several goals does not solve the board");
    context.Expect(snapshot.outgoingConnectionCounts[0] == 2,
                   "a branching source records each committed outgoing edge");

    Commit(board, puzzle, 1, 3);
    Commit(board, puzzle, 2, 3);
    snapshot = board.MakeSnapshot();
    context.Expect(snapshot.incomingConnectionCounts[3] == 2,
                   "a merge target accepts incoming edges up to its capacity");
    context.Expect(std::count(snapshot.activatedNodeIndices.begin(),
                              snapshot.activatedNodeIndices.end(), 3) == 1,
                   "merging into an already active target does not duplicate activation");

    board.Update(PressAt(puzzle.nodes[0].anchorPosition), kFrameSeconds);
    context.Expect(!board.IsDragging() &&
                       !Contains(board.MakeSnapshot().availableSourceNodeIndices, 0),
                   "a target with full incoming capacity disables its remaining edge");

    Commit(board, puzzle, 3, 5);
    snapshot = board.MakeSnapshot();
    context.Expect(board.IsSolved() && snapshot.solved,
                   "the board solves only after every configured goal is active");
    context.Expect(snapshot.availableSourceNodeIndices.empty(),
                   "a solved board exposes no available preview sources");
    context.Expect(board.GetActivatedNodeIndices() ==
                       std::vector<std::size_t>({0, 1, 2, 4, 3, 5}),
                   "the scoring interface exposes stable activation order");
    context.Expect(board.GetCommittedConnectionIndices() ==
                       std::vector<std::size_t>({0, 1, 2, 3, 4}),
                   "the scoring interface exposes committed edge order");
    context.Expect(board.GetOutgoingConnectionCounts()[0] == 2 &&
                       board.GetIncomingConnectionCounts()[3] == 2,
                   "read-only capacity counters match the committed graph");

    board.Update(PressAt(puzzle.nodes[3].anchorPosition), kFrameSeconds);
    context.Expect(!board.IsDragging(), "solved boards ignore new drag attempts");
    ExpectLengthInvariant(context, snapshot,
                          "branching and merging preserve the global length invariant");
}

void TestUniquePreviewCancellationPauseAndRetry(TestContext& context) {
    const PuzzleDefinition puzzle = MakeBranchMergePuzzle();
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "preview lifecycle board initializes");

    board.Update(PressAt(puzzle.nodes[0].anchorPosition), kFrameSeconds);
    board.Update(DragTo({700.0f, 600.0f}), kFrameSeconds);
    const float highWaterReserve = board.GetReservedLength();
    PuzzleBoardSnapshot dragging = board.MakeSnapshot();
    context.Expect(dragging.tentacles.size() == 1 &&
                       dragging.tentacles.front().preview &&
                       dragging.selectedSourceNodeIndex ==
                           std::optional<std::size_t>{0},
                   "a preview records its selected source and is the only temporary strand");

    board.Update(PressAt(puzzle.nodes[1].anchorPosition), kFrameSeconds);
    dragging = board.MakeSnapshot();
    context.Expect(dragging.tentacles.size() == 1 &&
                       dragging.selectedSourceNodeIndex ==
                           std::optional<std::size_t>{0},
                   "pressing another active root cannot open a second concurrent preview");
    board.Update(DragTo(puzzle.nodes[0].anchorPosition), kFrameSeconds);
    context.Expect(NearlyEqual(board.GetReservedLength(), highWaterReserve, 0.01f),
                   "preview reservation remains monotonic when the pointer returns");
    ExpectLengthInvariant(context, board.MakeSnapshot(),
                          "dragging preserves committed plus reserved plus remaining length");

    board.CancelDrag(false);
    PuzzleBoardSnapshot retracting = board.MakeSnapshot();
    context.Expect(retracting.retracting && !retracting.dragging &&
                       retracting.selectedSourceNodeIndex ==
                           std::optional<std::size_t>{0},
                   "animated cancellation keeps the selected source during retraction");
    board.Update({}, 0.05f);
    ExpectLengthInvariant(context, board.MakeSnapshot(),
                          "partial retraction refunds length without changing the total");
    AdvanceRetraction(board);
    context.Expect(!board.GetSelectedSourceNodeIndex().has_value() &&
                       NearlyEqual(board.GetRemainingLength(), puzzle.totalLength, 0.01f) &&
                       board.GetCompletedConnectionCount() == 0,
                   "completed retraction refunds the full reservation and clears selection");

    board.Update(PressAt(puzzle.nodes[0].anchorPosition), kFrameSeconds);
    board.Update(DragTo({500.0f, 500.0f}), kFrameSeconds);
    board.CancelDrag(true);
    context.Expect(!board.IsDragging() &&
                       !board.GetSelectedSourceNodeIndex().has_value() &&
                       NearlyEqual(board.GetRemainingLength(), puzzle.totalLength, 0.01f),
                   "pause-style immediate cancellation removes the preview and refunds it");

    board.Update(PressAt(puzzle.nodes[0].anchorPosition), kFrameSeconds);
    board.Update(DragTo({600.0f, 500.0f}), kFrameSeconds);
    const float committedHighWater = board.GetReservedLength();
    board.Update(ReleaseAt(puzzle.nodes[2].anchorPosition), kFrameSeconds);
    const PuzzleBoardSnapshot committed = board.MakeSnapshot();
    context.Expect(NearlyEqual(committed.committedLength, committedHighWater, 0.02f),
                   "commit spends the preview high-water reservation");
    ExpectLengthInvariant(context, committed,
                          "committing transfers reservation into committed length exactly once");

    context.Expect(board.Initialize(puzzle, error),
                   "retry can rebuild the multi-root board");
    const PuzzleBoardSnapshot retried = board.MakeSnapshot();
    context.Expect(retried.activatedNodeIndices ==
                       std::vector<std::size_t>({0, 1}) &&
                       retried.tentacles.empty() &&
                       retried.committedLength == 0.0f &&
                       AllZero(retried.incomingConnectionCounts) &&
                       AllZero(retried.outgoingConnectionCounts),
                   "retry clears graph progress, capacities, simulation and spent length");
}

void TestTileMaskTargetsAndExpandedSolidLineOfSight(TestContext& context) {
    PuzzleBoard board;
    std::string error;

    const PuzzleDefinition clear = MakeLineOfSightPuzzle(kPuzzleGridRows);
    context.Expect(board.Initialize(clear, error),
                   "clear tile-line puzzle initializes");
    const NodeDefinition& target = clear.nodes[1];
    const Vec2 emptyTargetTile{
        (static_cast<float>(target.origin.column) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
        (static_cast<float>(target.origin.row) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
    };
    board.Update(PressAt(clear.nodes[0].anchorPosition), kFrameSeconds);
    board.Update(ReleaseAt(emptyTargetTile), kFrameSeconds);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "a transparent target tile retracts instead of committing");
    board.CancelDrag(true);

    const Vec2 occupiedNonAnchorTargetTile{
        (static_cast<float>(target.origin.column) + 0.5f) *
            static_cast<float>(kPuzzleTileSize),
        (static_cast<float>(target.origin.row) + 1.5f) *
            static_cast<float>(kPuzzleTileSize),
    };
    board.Update(PressAt(clear.nodes[0].anchorPosition), kFrameSeconds);
    board.Update(ReleaseAt(occupiedNonAnchorTargetTile), kFrameSeconds);
    const float anchorMinimum =
        Length(clear.nodes[1].anchorPosition - clear.nodes[0].anchorPosition) *
        clear.minimumSlackRatio;
    context.Expect(board.GetCompletedConnectionCount() == 1 && board.IsSolved(),
                   "any occupied target cell can commit, not only its anchor cell");
    context.Expect(board.GetCommittedLength() + 0.01f >= anchorMinimum,
                   "an occupied non-anchor hit still reserves the anchor-to-anchor minimum");

    // The line is at y=168. Row 11 starts at y=176, so the raw tile does not
    // intersect it; the vessel half-width plus the 2px pixel-grid pad does.
    const PuzzleDefinition nearSolid = MakeLineOfSightPuzzle(11);
    context.Expect(board.Initialize(nearSolid, error),
                   "expanded-solid LOS puzzle initializes");
    Commit(board, nearSolid, 0, 1);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "a solid tile inside expanded vessel clearance blocks commitment");
    board.CancelDrag(true);

    const PuzzleDefinition distantSolid = MakeLineOfSightPuzzle(12);
    context.Expect(board.Initialize(distantSolid, error),
                   "distant-solid LOS puzzle initializes");
    Commit(board, distantSolid, 0, 1);
    context.Expect(board.GetCompletedConnectionCount() == 1 && board.IsSolved(),
                   "a solid tile outside expanded clearance leaves the edge usable");

}

void TestGlobalLengthExhaustionAcrossSources(TestContext& context) {
    const PuzzleDefinition puzzle = MakeExhaustionPuzzle();
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "multi-source exhaustion puzzle initializes");
    context.Expect(!board.IsLengthExhausted(),
                   "one affordable edge prevents global exhaustion even when another is too long");

    Commit(board, puzzle, 1, 3);
    const PuzzleBoardSnapshot afterNearGoal = board.MakeSnapshot();
    context.Expect(!board.IsSolved() && board.IsLengthExhausted(),
                   "an affordable edge from an inactive relay does not prevent exhaustion");
    context.Expect(Contains(afterNearGoal.activatedNodeIndices, 3) &&
                       !Contains(afterNearGoal.activatedNodeIndices, 2),
                   "length exhaustion does not activate an unreached goal");
    ExpectLengthInvariant(context, afterNearGoal,
                          "global exhaustion preserves the shared length budget");

    board.Update(PressAt(puzzle.nodes[0].anchorPosition), kFrameSeconds);
    board.Update(ReleaseAt(puzzle.nodes[4].anchorPosition), kFrameSeconds);
    context.Expect(board.GetCompletedConnectionCount() == 1 &&
                       board.MakeSnapshot().retracting,
                   "an edge longer than the remaining global budget cannot commit");

    const PuzzleDefinition routeBlocked = MakeRouteBlockedPuzzle();
    context.Expect(board.Initialize(routeBlocked, error),
                   "capacity-choice dead-end puzzle initializes");
    Commit(board, routeBlocked, 0, 1);
    const PuzzleBoardSnapshot blocked = board.MakeSnapshot();
    context.Expect(blocked.routeBlocked && board.IsRouteBlocked() &&
                       !blocked.lengthExhausted && !board.IsLengthExhausted(),
                   "a capacity dead end is distinct from insufficient length");
    context.Expect(blocked.remainingLength > 500.0f && !blocked.solved,
                   "route-blocked feedback can occur while substantial length remains");
    ExpectLengthInvariant(context, blocked,
                          "a route-blocked board preserves the global length invariant");
}

void TestBundledCatalogInitializesBoards(TestContext& context) {
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(
        PuzzleCatalogLoader::Load("data/catalog.json",
                                  std::string{OBJECT_CONNECT_TEST_RESOURCE_ROOT},
                                  catalog, error),
        "the bundled normalized catalog loads for PuzzleBoard integration");
    for (const PuzzleDefinition& definition : catalog.GetPuzzles()) {
        PuzzleBoard board;
        context.Expect(board.Initialize(definition, error),
                       "every bundled puzzle satisfies PuzzleBoard runtime invariants");
    }

    const auto solvePath = [&context, &catalog](
                               const char* puzzleId,
                               const std::vector<std::pair<const char*, const char*>>& path) {
        const PuzzleDefinition* const puzzle = catalog.Find(puzzleId);
        context.Expect(puzzle != nullptr,
                       "the bundled solvability test finds its stable puzzle ID");
        if (puzzle == nullptr) {
            return;
        }

        PuzzleBoard board;
        std::string initializeError;
        context.Expect(board.Initialize(*puzzle, initializeError),
                       "the bundled solvability test initializes its board");
        if (!board.IsInitialized()) {
            return;
        }

        for (const auto& [fromId, toId] : path) {
            const auto findNode = [puzzle](const char* id) {
                return std::find_if(
                    puzzle->nodes.begin(), puzzle->nodes.end(),
                    [id](const NodeDefinition& node) { return node.id == id; });
            };
            const auto from = findNode(fromId);
            const auto to = findNode(toId);
            context.Expect(from != puzzle->nodes.end() && to != puzzle->nodes.end(),
                           "the bundled solution uses existing stable node IDs");
            if (from == puzzle->nodes.end() || to == puzzle->nodes.end()) {
                return;
            }
            Commit(board, *puzzle,
                   static_cast<std::size_t>(from - puzzle->nodes.begin()),
                   static_cast<std::size_t>(to - puzzle->nodes.begin()));
            context.Expect(!board.MakeSnapshot().retracting,
                           "the bundled authored solution edge commits at runtime");
            if (board.MakeSnapshot().retracting) {
                return;
            }
        }

        context.Expect(board.IsSolved(),
                       "the bundled authored route activates every configured goal");
        ExpectLengthInvariant(context, board.MakeSnapshot(),
                              "the bundled authored route preserves the global length invariant");
    };

    solvePath("first_link", {{"heart_a", "brain_a"}});
    solvePath("around_block", {{"heart_a", "lung_a"},
                                {"lung_a", "brain_a"}});
    solvePath("clot_path", {{"heart_a", "lung_a"},
                             {"lung_a", "liver_a"},
                             {"liver_a", "lung_c"},
                             {"lung_c", "brain_a"},
                             {"lung_c", "brain_b"}});
}

} // namespace

void RunPuzzleBoardTests(TestContext& context) {
    TestInitializationRootsMasksAndSnapshot(context);
    TestBranchMergeCapacitiesAndGoals(context);
    TestUniquePreviewCancellationPauseAndRetry(context);
    TestTileMaskTargetsAndExpandedSolidLineOfSight(context);
    TestGlobalLengthExhaustionAcrossSources(context);
    TestBundledCatalogInitializesBoards(context);
}

} // namespace object_connect::tests
