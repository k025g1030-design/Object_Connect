#include "TestSupport.hpp"

#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string>

namespace object_connect::tests {
namespace {

[[nodiscard]] NodeDefinition MakeNode(
    const char* id, const NodeType type, const std::uint32_t tileX,
    const std::uint32_t tileY, const std::uint32_t maxIncoming,
    const std::uint32_t maxOutgoing, const float maxOutgoingLength,
    const bool placed = true) {
    NodeDefinition node{};
    node.id = id;
    node.sourcePresetId = id;
    node.type = type;
    node.texturePath = "nodes/test.png";
    node.widthTiles = 2;
    node.heightTiles = 2;
    node.displayName = id;
    if (placed) {
        node.tilePosition = TilePosition{tileX, tileY};
    }
    node.maxIncoming = maxIncoming;
    node.maxOutgoing = maxOutgoing;
    node.maxOutgoingLength = maxOutgoingLength;
    return node;
}

[[nodiscard]] PuzzleDefinition MakePuzzle() {
    PuzzleDefinition puzzle{};
    puzzle.id = "board_test";
    puzzle.title = "BOARD TEST";
    puzzle.mapPath = "maps/board_test.csv";
    puzzle.totalLength = 5000.0f;
    puzzle.minimumSlackRatio = 1.0f;
    puzzle.baseWidth = 16.0f;
    puzzle.tipWidth = 16.0f;
    puzzle.widthVariation = 0.16f;
    return puzzle;
}

[[nodiscard]] Vec2 Center(const PuzzleDefinition& puzzle,
                          const std::size_t nodeIndex) {
    return *puzzle.nodes[nodeIndex].GetCenterPosition();
}

[[nodiscard]] BoardPointerInput PressAt(const Vec2 position) {
    return {position, true, true, false, std::nullopt};
}

[[nodiscard]] BoardPointerInput DragTo(const Vec2 position) {
    return {position, false, true, false, std::nullopt};
}

[[nodiscard]] BoardPointerInput DragToWrapped(
    const Vec2 canonicalPosition, const Vec2 unwrappedPosition) {
    BoardPointerInput input = DragTo(canonicalPosition);
    input.unwrappedPosition = unwrappedPosition;
    return input;
}

[[nodiscard]] BoardPointerInput ReleaseAt(const Vec2 position) {
    return {position, false, false, true, std::nullopt};
}

[[nodiscard]] BoardPointerInput ReleaseAtWrapped(
    const Vec2 canonicalPosition, const Vec2 unwrappedPosition) {
    BoardPointerInput input = ReleaseAt(canonicalPosition);
    input.unwrappedPosition = unwrappedPosition;
    return input;
}

void BeginDrag(PuzzleBoard& board, const PuzzleDefinition& puzzle,
               const std::size_t sourceNodeIndex) {
    board.Update(PressAt(Center(puzzle, sourceNodeIndex)), 1.0f / 60.0f);
}

void Connect(PuzzleBoard& board, const PuzzleDefinition& puzzle,
             const std::size_t sourceNodeIndex,
             const std::size_t targetNodeIndex) {
    BeginDrag(board, puzzle, sourceNodeIndex);
    board.Update(ReleaseAt(Center(puzzle, targetNodeIndex)), 1.0f / 60.0f);
}

void FinishRetraction(PuzzleBoard& board) {
    for (int step = 0; step < 8; ++step) {
        board.Update({}, 0.05f);
    }
}

void ExpectRejectedConnection(TestContext& context, PuzzleBoard& board,
                              const PuzzleDefinition& puzzle,
                              const std::size_t sourceNodeIndex,
                              const std::size_t targetNodeIndex,
                              const std::size_t expectedLineCount,
                              const std::string_view description) {
    BeginDrag(board, puzzle, sourceNodeIndex);
    if (!board.IsDragging()) {
        context.Expect(board.GetCompletedConnectionCount() == expectedLineCount,
                       description);
        return;
    }
    board.Update(ReleaseAt(Center(puzzle, targetNodeIndex)), 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == expectedLineCount,
                   description);
    FinishRetraction(board);
}

void TestInitializationAndMissingPlacement(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.nodes = {
        MakeNode("root_a", NodeType::Root, 2, 2, 0, 2, 1000.0f),
        MakeNode("root_b", NodeType::Root, 2, 12, 0, 2, 1000.0f),
        MakeNode("hidden_root", NodeType::Root, 0, 0, 0, 2, 1000.0f, false),
        MakeNode("follow", NodeType::Follow, 12, 2, 2, 2, 1000.0f),
        MakeNode("hidden_end", NodeType::End, 0, 0, 1, 0, 0.0f, false),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "board accepts maps without a placed end");
    context.Expect(error.empty(), "successful board initialization clears errors");
    context.Expect(board.GetActivatedNodeIndices().size() == 2 &&
                       board.GetActivatedNodeIndices()[0] == 0 &&
                       board.GetActivatedNodeIndices()[1] == 1,
                   "all and only placed roots start active");
    context.Expect(!board.IsSolved(),
                   "zero placed end nodes never produces vacuous completion");

    const PuzzleBoardSnapshot snapshot = board.MakeSnapshot();
    context.Expect(snapshot.nodeStates.size() == puzzle.nodes.size(),
                   "snapshot has one runtime state per authored node");
    context.Expect(snapshot.nodeStates[0].drawable &&
                       snapshot.nodeStates[0].active &&
                       snapshot.nodeStates[0].availableSource,
                   "placed active root is drawable and selectable");
    context.Expect(!snapshot.nodeStates[2].drawable &&
                       !snapshot.nodeStates[2].active &&
                       !snapshot.nodeStates[4].drawable,
                   "missing-placement nodes are absent from runtime interaction");

    board.Update(PressAt({1.0f, 1.0f}), 1.0f / 60.0f);
    context.Expect(!board.IsDragging(),
                   "missing-placement root cannot create a preview");

    PuzzleDefinition empty = MakePuzzle();
    context.Expect(board.Initialize(empty, error),
                   "empty map is allowed even though it cannot be solved");
    context.Expect(!board.IsSolved() && board.GetActivatedNodeIndices().empty(),
                   "empty map stays idle and unsolved");
}

void TestMultiRootBranchMergeAndAllEnds(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.nodes = {
        MakeNode("root_a", NodeType::Root, 2, 2, 0, 3, 2000.0f),
        MakeNode("root_b", NodeType::Root, 2, 12, 0, 2, 2000.0f),
        MakeNode("branch_top", NodeType::Follow, 12, 2, 2, 2, 2000.0f),
        MakeNode("branch_bottom", NodeType::Follow, 12, 12, 2, 2, 2000.0f),
        MakeNode("merge", NodeType::Follow, 22, 7, 2, 2, 2000.0f),
        MakeNode("end_top", NodeType::End, 32, 2, 2, 0, 0.0f),
        MakeNode("end_bottom", NodeType::End, 32, 12, 2, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "multi-root branch-and-merge board initializes");

    BeginDrag(board, puzzle, 0);
    const PuzzleBoardSnapshot selectingRoot = board.MakeSnapshot();
    context.Expect(selectingRoot.nodeStates[0].availableSource &&
                       !selectingRoot.nodeStates[1].availableSource,
                   "an active preview keeps only its selected source highlighted");
    board.CancelDrag(true);

    Connect(board, puzzle, 0, 2);
    Connect(board, puzzle, 0, 3);
    context.Expect(board.MakeSnapshot().nodeStates[0].outgoingUsed == 2,
                   "one root can branch while outgoing capacity remains");
    context.Expect(board.MakeSnapshot().nodeStates[2].active &&
                       board.MakeSnapshot().nodeStates[3].active,
                   "first incoming line activates each follow node");

    Connect(board, puzzle, 2, 4);
    Connect(board, puzzle, 3, 4);
    const PuzzleBoardSnapshot merged = board.MakeSnapshot();
    context.Expect(merged.nodeStates[4].active &&
                       merged.nodeStates[4].incomingUsed == 2,
                   "already-active target accepts another incoming line for a merge");

    Connect(board, puzzle, 4, 5);
    context.Expect(!board.IsSolved(),
                   "activating only one of multiple ends does not solve the board");
    Connect(board, puzzle, 4, 6);
    context.Expect(board.IsSolved(),
                   "all placed end nodes active solves the board");
    context.Expect(board.GetCompletedConnectionCount() == 6,
                   "branch, merge, and two goal lines remain committed");
    context.Expect(board.GetActivatedNodeIndices().size() == puzzle.nodes.size(),
                   "activated-node scoring interface contains every reached node once");
    context.Expect(board.GetCommittedLines().size() == 6 &&
                       board.GetCommittedLines()[3].fromNodeIndex == 3 &&
                       board.GetCommittedLines()[3].toNodeIndex == 4,
                   "committed-line scoring interface exposes runtime endpoints");
    context.Expect(board.MakeSnapshot().tentacles.size() == 6 &&
                       board.MakeSnapshot().tentacles.front().points.size() == 10,
                   "runtime-created lines use the default ten-point tentacle");
}

void TestRolesAndCapacities(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.nodes = {
        MakeNode("root_a", NodeType::Root, 2, 2, 0, 1, 1000.0f),
        MakeNode("root_b", NodeType::Root, 2, 12, 0, 2, 1000.0f),
        MakeNode("follow", NodeType::Follow, 12, 2, 1, 3, 1000.0f),
        MakeNode("other_follow", NodeType::Follow, 12, 12, 2, 2, 1000.0f),
        MakeNode("end", NodeType::End, 22, 2, 2, 4, 1000.0f),
        MakeNode("dead", NodeType::Dead, 30, 15, 4, 4, 1000.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "role-and-capacity board initializes");

    BeginDrag(board, puzzle, 2);
    context.Expect(!board.IsDragging(),
                   "inactive follow node cannot be a source");
    Connect(board, puzzle, 0, 2);
    context.Expect(board.GetCompletedConnectionCount() == 1,
                   "root activates a follow target");

    BeginDrag(board, puzzle, 0);
    context.Expect(!board.IsDragging(),
                   "source stops creating lines at max outgoing count");
    ExpectRejectedConnection(context, board, puzzle, 1, 2, 1,
                             "target stops accepting lines at max incoming count");
    ExpectRejectedConnection(context, board, puzzle, 2, 0, 1,
                             "root node can never be a target");
    ExpectRejectedConnection(context, board, puzzle, 2, 5, 1,
                             "dead node can never be a target");

    Connect(board, puzzle, 2, 4);
    context.Expect(board.IsSolved(), "single placed end activates normally");
    BeginDrag(board, puzzle, 4);
    context.Expect(!board.IsDragging(),
                   "end node can never be a source even with authored outgoing values");
}

void TestDuplicateSelfAndDirectedCycle(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 2, 2, 0, 3, 2000.0f),
        MakeNode("a", NodeType::Follow, 12, 2, 4, 4, 2000.0f),
        MakeNode("b", NodeType::Follow, 22, 2, 4, 4, 2000.0f),
        MakeNode("end", NodeType::End, 32, 2, 4, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error), "cycle-test board initializes");
    Connect(board, puzzle, 0, 1);
    Connect(board, puzzle, 1, 2);
    context.Expect(board.GetCompletedConnectionCount() == 2,
                   "acyclic prefix commits");

    ExpectRejectedConnection(context, board, puzzle, 1, 2, 2,
                             "exact duplicate directed line is rejected");
    ExpectRejectedConnection(context, board, puzzle, 2, 1, 2,
                             "line that closes a directed cycle is rejected");
    ExpectRejectedConnection(context, board, puzzle, 1, 1, 2,
                             "self connection is rejected");

    Connect(board, puzzle, 2, 3);
    context.Expect(board.IsSolved(),
                   "valid acyclic continuation still solves after rejected attempts");
}

void TestGlobalAndPerSourceLengthBudgets(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.totalLength = 300.0f;
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 2, 2, 0, 2, 150.0f),
        MakeNode("near", NodeType::Follow, 7, 2, 2, 0, 0.0f),
        MakeNode("far", NodeType::End, 8, 2, 2, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error), "budget-test board initializes");

    BeginDrag(board, puzzle, 0);
    const Vec2 root = Center(puzzle, 0);
    board.Update(DragTo(root + Vec2{100.0f, 0.0f}), 1.0f / 60.0f);
    const float highWater = board.GetReservedLength();
    context.Expect(NearlyEqual(highWater, 100.0f, 0.01f),
                   "preview reserves its source-to-pointer high-water length");
    context.Expect(NearlyEqual(board.GetRemainingLength() + highWater,
                               board.GetTotalLength(), 0.01f),
                   "global committed + preview + remaining invariant holds");
    context.Expect(NearlyEqual(board.GetRemainingOutgoingLength(0) + highWater,
                               puzzle.nodes[0].maxOutgoingLength, 0.01f),
                   "node-local committed + preview + remaining invariant holds");
    board.Update(DragTo(Center(puzzle, 1)), 1.0f / 60.0f);
    context.Expect(NearlyEqual(board.GetReservedLength(), highWater, 0.01f),
                   "moving back never shortens preview rest length");

    board.CancelDrag(true);
    context.Expect(NearlyEqual(board.GetRemainingLength(), 300.0f, 0.01f) &&
                       NearlyEqual(board.GetRemainingOutgoingLength(0), 150.0f, 0.01f),
                   "cancelling refunds both global and source-local reservations");

    BeginDrag(board, puzzle, 0);
    board.Update(DragTo(root + Vec2{100.0f, 0.0f}), 1.0f / 60.0f);
    board.Update(ReleaseAt(Center(puzzle, 1)), 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 1 &&
                       NearlyEqual(board.GetRemainingLength(), 200.0f, 0.01f) &&
                       NearlyEqual(board.GetRemainingOutgoingLength(0), 50.0f, 0.01f),
                   "commit permanently spends global and shared source budgets");

    ExpectRejectedConnection(context, board, puzzle, 0, 2, 1,
                             "second outgoing cannot exceed source-shared remaining length");
    context.Expect(board.IsLengthExhausted(),
                   "idle board reports local length exhaustion despite global remainder");

    context.Expect(board.Initialize(puzzle, error), "retry rebuild initializes again");
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       NearlyEqual(board.GetRemainingLength(), 300.0f, 0.01f) &&
                       NearlyEqual(board.GetRemainingOutgoingLength(0), 150.0f, 0.01f),
                   "retry rebuild restores global and every node-local budget");

    PuzzleDefinition globallyShort = MakePuzzle();
    globallyShort.totalLength = 70.0f;
    globallyShort.nodes = {
        MakeNode("root", NodeType::Root, 2, 2, 0, 1, 500.0f),
        MakeNode("end", NodeType::End, 7, 2, 1, 0, 0.0f),
    };
    context.Expect(board.Initialize(globallyShort, error),
                   "globally short but structurally valid board initializes");
    BeginDrag(board, globallyShort, 0);
    board.Update(ReleaseAt(Center(globallyShort, 1)), 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 0,
                   "global budget independently caps a source with ample local budget");
    FinishRetraction(board);
    context.Expect(board.IsLengthExhausted(),
                   "idle board reports global length exhaustion");
}

void TestSourceSelectionTransitionContract(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 2, 2, 0, 2, 1000.0f),
        MakeNode("inactive", NodeType::Follow, 12, 2, 1, 1, 1000.0f),
        MakeNode("dead", NodeType::Dead, 22, 2, 0, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "source-selection transition board initializes");

    board.Update(PressAt({1.0f, 1.0f}), 1.0f / 60.0f);
    context.Expect(!board.IsDragging(),
                   "blank space does not begin a source-selection transition");
    board.Update(PressAt(Center(puzzle, 1)), 1.0f / 60.0f);
    board.Update(PressAt(Center(puzzle, 2)), 1.0f / 60.0f);
    context.Expect(!board.IsDragging(),
                   "inactive and dead nodes do not begin source selection");

    const bool wasDragging = board.IsDragging();
    board.Update(PressAt(Center(puzzle, 0)), 1.0f / 60.0f);
    context.Expect(!wasDragging && board.IsDragging(),
                   "an active source produces exactly the false-to-true cue transition");

    const bool wasDraggingWhileHeld = board.IsDragging();
    board.Update(DragTo(Center(puzzle, 0)), 1.0f / 60.0f);
    context.Expect(wasDraggingWhileHeld && board.IsDragging(),
                   "holding an existing selection does not produce another transition");

    board.CancelDrag(true);
    context.Expect(!board.IsDragging(),
                   "cancelling resets the source-selection transition state");
    board.Update(PressAt(Center(puzzle, 0)), 1.0f / 60.0f);
    context.Expect(board.IsDragging(),
                   "the same source can be selected again after cancellation");
}

void TestTargetConnectionHitPadding(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 2, 2, 0, 1, 1000.0f),
        MakeNode("end", NodeType::End, 12, 2, 1, 0, 0.0f),
    };

    const Vec2 sourceCenter = Center(puzzle, 0);
    const Vec2 targetCenter = Center(puzzle, 1);
    const Vec2 targetTopLeft = *puzzle.nodes[1].GetTopLeftPosition();
    const float expectedLength = Length(targetCenter - sourceCenter);
    const Vec2 insidePadding{
        targetTopLeft.x - kPuzzleTileSize * 0.5f, targetCenter.y};

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "target-hit-padding board initializes");
    BeginDrag(board, puzzle, 0);
    board.Update(ReleaseAt(insidePadding), 1.0f / 60.0f);

    const PuzzleBoardSnapshot connected = board.MakeSnapshot();
    const bool hasCommittedLine = board.GetCommittedLines().size() == 1;
    context.Expect(hasCommittedLine &&
                       board.GetCommittedLines().front().toNodeIndex == 1,
                   "release outside the visual node but inside one-tile padding connects");
    context.Expect(
        !connected.tentacles.empty() &&
            !connected.tentacles[0].points.empty() &&
            NearlyEqual(connected.tentacles[0].points.back().x,
                        targetCenter.x, 0.01f) &&
            NearlyEqual(connected.tentacles[0].points.back().y,
                        targetCenter.y, 0.01f),
        "expanded target detection keeps the vessel endpoint at the node center");
    if (hasCommittedLine) {
        context.Expect(
            NearlyEqual(board.GetCommittedLines().front().committedLength,
                        expectedLength, 0.01f),
            "near-side padding reserves the real center path without bypassing length");
    }

    context.Expect(board.Initialize(puzzle, error),
                   "target-hit-padding rejection board initializes");
    BeginDrag(board, puzzle, 0);
    board.Update(DragTo(targetCenter), 1.0f / 60.0f);
    const Vec2 outsidePadding{
        targetTopLeft.x - kPuzzleTileSize - 0.25f, targetCenter.y};
    board.Update(ReleaseAt(outsidePadding), 1.0f / 60.0f);
    const PuzzleBoardSnapshot rejected = board.MakeSnapshot();
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       rejected.retracting,
                   "release beyond one-tile target padding remains rejected");
}

void TestLengthExhaustionRespectsWrapEdges(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {160.0f, 160.0f}};
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.totalLength = 34.0f;
    puzzle.minimumSlackRatio = 1.05f;
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 8, 1, 0, 1, 34.0f),
        MakeNode("end", NodeType::End, 0, 1, 1, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, bounds, error),
                   "non-wrapping length-warning board initializes");
    context.Expect(board.IsLengthExhausted(),
                   "disabled wrapping keeps the legacy direct-distance warning");

    puzzle.wrapEdges = true;
    context.Expect(board.Initialize(puzzle, bounds, error),
                   "wrapping length-warning board initializes");
    context.Expect(!board.IsLengthExhausted() &&
                       !board.MakeSnapshot().lengthExhausted,
                   "an affordable adjacent target image suppresses the length warning");

    BeginDrag(board, puzzle, 0);
    board.Update(ReleaseAtWrapped(Center(puzzle, 1),
                                  Center(puzzle, 1) + Vec2{160.0f, 0.0f}),
                 1.0f / 60.0f);
    context.Expect(board.IsSolved() &&
                       board.GetCompletedConnectionCount() == 1 &&
                       NearlyEqual(board.GetCommittedLines()[0].committedLength,
                                   33.6f, 0.01f),
                   "the route accepted by the warning probe is an actual wrapped commit");

    puzzle.totalLength = 33.5f;
    puzzle.nodes[0].maxOutgoingLength = 33.5f;
    context.Expect(board.Initialize(puzzle, bounds, error),
                   "short wrapping length-warning board initializes");
    context.Expect(board.IsLengthExhausted(),
                   "wrapping still reports exhaustion when every image exceeds the budget");
}

void TestRetractionRefund(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.totalLength = 400.0f;
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 2, 2, 0, 2, 300.0f),
        MakeNode("end", NodeType::End, 12, 2, 1, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, error),
                   "retraction board initializes");
    BeginDrag(board, puzzle, 0);
    board.Update(DragTo({250.0f, 250.0f}), 1.0f / 60.0f);
    const float beforeRelease = board.GetReservedLength();
    board.Update(ReleaseAt({600.0f, 600.0f}), 1.0f / 60.0f);
    context.Expect(board.MakeSnapshot().retracting,
                   "invalid target begins visible retraction");
    board.Update({}, 0.05f);
    context.Expect(board.GetReservedLength() < beforeRelease,
                   "retraction progressively returns reserved length");
    context.Expect(NearlyEqual(board.GetRemainingLength() +
                                   board.GetReservedLength(),
                               board.GetTotalLength(), 0.01f),
                   "global invariant holds throughout retraction");
    context.Expect(NearlyEqual(board.GetRemainingOutgoingLength(0) +
                                   board.GetReservedLength(),
                               puzzle.nodes[0].maxOutgoingLength, 0.01f),
                   "source-local invariant holds throughout retraction");
    FinishRetraction(board);
    context.Expect(NearlyEqual(board.GetRemainingLength(), 400.0f, 0.01f) &&
                       NearlyEqual(board.GetRemainingOutgoingLength(0), 300.0f, 0.01f),
                   "completed retraction refunds both budgets in full");
}

void TestDeadNodeBlockingAndMissingDead(TestContext& context) {
    PuzzleDefinition blocked = MakePuzzle();
    blocked.nodes = {
        MakeNode("root", NodeType::Root, 2, 5, 0, 1, 1000.0f),
        MakeNode("end", NodeType::End, 12, 5, 1, 0, 0.0f),
        MakeNode("dead", NodeType::Dead, 7, 4, 0, 4, 1000.0f),
        MakeNode("hidden_end", NodeType::End, 0, 0, 1, 0, 0.0f, false),
    };
    blocked.nodes[2].heightTiles = 4;

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(blocked, error),
                   "dead-node board initializes");
    board.Update(PressAt(Center(blocked, 2)), 1.0f / 60.0f);
    context.Expect(!board.IsDragging() &&
                       !board.MakeSnapshot().nodeStates[2].availableSource,
                   "dead node cannot become a source even with authored outgoing capacity");

    BeginDrag(board, blocked, 0);
    for (int frame = 0; frame < 12; ++frame) {
        board.Update(DragTo(Center(blocked, 1)), 1.0f / 60.0f);
    }
    const PuzzleBoardSnapshot stopped = board.MakeSnapshot();
    const float expandedDeadNearEdge =
        static_cast<float>(blocked.nodes[2].tilePosition->x) * kPuzzleTileSize -
        blocked.baseWidth * 0.5f;
    context.Expect(stopped.dragging && !stopped.tentacles.empty() &&
                       stopped.tentacles.back().points.back().x < expandedDeadNearEdge,
                   "preview tip stays on the source side when cursor crosses a dead AABB");
    context.Expect(stopped.reservedLength <
                       Length(Center(blocked, 1) - Center(blocked, 0)),
                   "blocked cursor does not reserve length beyond the dead boundary");
    board.Update(ReleaseAt(Center(blocked, 2)), 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "dead node is never accepted as a preview target");
    FinishRetraction(board);

    context.Expect(board.Initialize(blocked, error),
                   "dead-node board can restart for commit LOS test");
    Connect(board, blocked, 0, 1);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "source-target segment intersecting expanded dead AABB is rejected");
    FinishRetraction(board);

    PuzzleDefinition clear = blocked;
    clear.nodes[2].tilePosition.reset();
    context.Expect(board.Initialize(clear, error),
                   "map with missing-placement dead node initializes");
    Connect(board, clear, 0, 1);
    context.Expect(board.IsSolved() && board.GetCompletedConnectionCount() == 1,
                   "missing-placement dead and end nodes have no gameplay presence");
}

void TestWrappedBoardPathAndBoundsContract(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {160.0f, 160.0f}};
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.wrapEdges = true;
    puzzle.totalLength = 200.0f;
    puzzle.minimumSlackRatio = 1.05f;
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 8, 1, 0, 1, 200.0f),
        MakeNode("end", NodeType::End, 0, 1, 1, 0, 0.0f),
        MakeNode("middle_dead", NodeType::Dead, 4, 1, 0, 0, 0.0f),
        MakeNode("middle_follow", NodeType::Follow, 5, 1, 1, 1, 100.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(!board.Initialize(puzzle, error) &&
                       !board.IsInitialized() &&
                       error.find("bounds") != std::string::npos,
                   "wrap-enabled boards require explicit playfield bounds");
    context.Expect(board.Initialize(puzzle, bounds, error),
                   "wrap-enabled board initializes with session bounds");

    BeginDrag(board, puzzle, 0);
    board.Update(ReleaseAtWrapped(Center(puzzle, 1),
                                  Center(puzzle, 1) + Vec2{160.0f, 0.0f}),
                 1.0f / 60.0f);
    const PuzzleBoardSnapshot wrapped = board.MakeSnapshot();
    context.Expect(board.IsSolved() &&
                       board.GetCompletedConnectionCount() == 1,
                   "pointer winding commits to the canonical node across an edge");
    context.Expect(wrapped.playfieldBounds.has_value() &&
                       wrapped.playfieldBounds->minimum == bounds.minimum &&
                       wrapped.playfieldBounds->maximum == bounds.maximum,
                   "snapshot carries the exact session bounds used by geometry");
    context.Expect(wrapped.tentacles.size() == 1 &&
                       wrapped.tentacles[0].drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}, {-160.0f, 0.0f}},
                   "renderer snapshot consumes authoritative path image translations");
    context.Expect(wrapped.nodeStates.size() == 4 &&
                       !wrapped.nodeStates[3].active,
                   "a node located only in the portal gap is not activated");
    context.Expect(board.GetCommittedLines().size() == 1 &&
                       NearlyEqual(
                           board.GetCommittedLines()[0].committedLength,
                           33.6f, 0.01f) &&
                       NearlyEqual(board.GetRemainingLength(), 166.4f, 0.01f),
                   "portal jump adds no length and existing slack applies only to visible travel");
}

void TestWrappedPreviewRetracesThroughPortal(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {160.0f, 160.0f}};
    PuzzleDefinition puzzle = MakePuzzle();
    puzzle.wrapEdges = true;
    puzzle.totalLength = 200.0f;
    puzzle.nodes = {
        MakeNode("root", NodeType::Root, 8, 1, 0, 1, 200.0f),
        MakeNode("end", NodeType::End, 2, 6, 1, 0, 0.0f),
    };

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(puzzle, bounds, error),
                   "wrapped retraction board initializes");
    BeginDrag(board, puzzle, 0);
    const Vec2 canonicalTip{40.0f, 32.0f};
    const Vec2 unwrappedTip{200.0f, 32.0f};
    for (int frame = 0; frame < 8; ++frame) {
        board.Update(DragToWrapped(canonicalTip, unwrappedTip),
                     1.0f / 60.0f);
    }

    const PuzzleBoardSnapshot extended = board.MakeSnapshot();
    context.Expect(extended.dragging && extended.tentacles.size() == 1 &&
                       !extended.tentacles[0].points.empty() &&
                       extended.tentacles[0].points.back().x > bounds.maximum.x &&
                       extended.tentacles[0].drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}, {-160.0f, 0.0f}},
                   "preview visibly reaches the entrance side before release");
    if (extended.tentacles.empty() || extended.tentacles[0].points.empty()) {
        return;
    }
    const float releasedTipX = extended.tentacles[0].points.back().x;

    board.Update(ReleaseAtWrapped(canonicalTip, unwrappedTip),
                 1.0f / 60.0f);
    context.Expect(board.MakeSnapshot().retracting &&
                       board.GetCompletedConnectionCount() == 0,
                   "releasing away from a node begins wrapped retraction");

    board.Update({}, 0.05f);
    const PuzzleBoardSnapshot firstStep = board.MakeSnapshot();
    context.Expect(firstStep.retracting && firstStep.tentacles.size() == 1 &&
                       !firstStep.tentacles[0].points.empty() &&
                       firstStep.tentacles[0].points.back().x < releasedTipX &&
                       firstStep.tentacles[0].points.back().x > bounds.maximum.x &&
                       firstStep.tentacles[0].drawTranslations.size() == 2,
                   "wrapped tip moves back on the entrance side instead of jumping to the root");

    board.Update({}, 0.05f);
    board.Update({}, 0.05f);
    const PuzzleBoardSnapshot beforePortal = board.MakeSnapshot();
    context.Expect(beforePortal.retracting &&
                       beforePortal.tentacles.size() == 1 &&
                       !beforePortal.tentacles[0].points.empty() &&
                       beforePortal.tentacles[0].points.back().x >
                           bounds.maximum.x &&
                       beforePortal.tentacles[0].drawTranslations.size() == 2,
                   "retraction keeps the entrance projection until the tip reaches the portal");

    board.Update({}, 0.05f);
    const PuzzleBoardSnapshot afterPortal = board.MakeSnapshot();
    context.Expect(afterPortal.retracting && afterPortal.tentacles.size() == 1 &&
                       !afterPortal.tentacles[0].points.empty() &&
                       afterPortal.tentacles[0].points.back().x <
                           bounds.maximum.x &&
                       afterPortal.tentacles[0].drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}},
                   "tip exits the portal in reverse and drops the obsolete entrance projection");

    FinishRetraction(board);
    context.Expect(!board.MakeSnapshot().retracting &&
                       board.MakeSnapshot().tentacles.empty() &&
                       NearlyEqual(board.GetRemainingLength(),
                                   board.GetTotalLength(), 0.01f),
                   "wrapped retraction clears the preview and refunds its full reservation");
}

void TestWrappedBoardCollisionLengthAndStateIsolation(TestContext& context) {
    const AxisAlignedBox bounds{{0.0f, 0.0f}, {160.0f, 160.0f}};
    PuzzleDefinition blocked = MakePuzzle();
    blocked.wrapEdges = true;
    blocked.totalLength = 200.0f;
    blocked.nodes = {
        MakeNode("root", NodeType::Root, 8, 1, 0, 1, 200.0f),
        MakeNode("end", NodeType::End, 2, 1, 1, 0, 0.0f),
        MakeNode("entrance_dead", NodeType::Dead, 0, 1, 0, 0, 0.0f),
    };
    blocked.nodes[2].widthTiles = 1;

    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(blocked, bounds, error),
                   "wrapped collision board initializes");
    BeginDrag(board, blocked, 0);
    board.Update(ReleaseAtWrapped(Center(blocked, 1),
                                  Center(blocked, 1) + Vec2{160.0f, 0.0f}),
                 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "dead node on the entrance-side visible segment still blocks");
    FinishRetraction(board);

    PuzzleDefinition exitBlocked = MakePuzzle();
    exitBlocked.wrapEdges = true;
    exitBlocked.totalLength = 200.0f;
    exitBlocked.nodes = {
        MakeNode("root", NodeType::Root, 7, 1, 0, 1, 200.0f),
        MakeNode("end", NodeType::End, 0, 1, 1, 0, 0.0f),
        MakeNode("exit_dead", NodeType::Dead, 9, 1, 0, 0, 0.0f),
    };
    exitBlocked.nodes[2].widthTiles = 1;
    context.Expect(board.Initialize(exitBlocked, bounds, error),
                   "exit-side wrapped collision board initializes");
    BeginDrag(board, exitBlocked, 0);
    board.Update(ReleaseAtWrapped(Center(exitBlocked, 1),
                                  Center(exitBlocked, 1) + Vec2{160.0f, 0.0f}),
                 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "dead node on the exit-side visible segment still blocks");
    FinishRetraction(board);

    PuzzleDefinition tooShort = MakePuzzle();
    tooShort.wrapEdges = true;
    tooShort.totalLength = 33.5f;
    tooShort.minimumSlackRatio = 1.05f;
    tooShort.nodes = {
        MakeNode("root", NodeType::Root, 8, 1, 0, 1, 33.5f),
        MakeNode("end", NodeType::End, 0, 1, 1, 0, 0.0f),
    };
    context.Expect(board.Initialize(tooShort, bounds, error),
                   "short wrapped board initializes");
    BeginDrag(board, tooShort, 0);
    board.Update(ReleaseAtWrapped(Center(tooShort, 1),
                                  Center(tooShort, 1) + Vec2{160.0f, 0.0f}),
                 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 0 &&
                       board.MakeSnapshot().retracting,
                   "wrapping cannot bypass the existing length limit");
    FinishRetraction(board);

    PuzzleDefinition closed = tooShort;
    closed.wrapEdges = false;
    closed.totalLength = 200.0f;
    closed.nodes[0].maxOutgoingLength = 200.0f;
    context.Expect(board.Initialize(closed, bounds, error),
                   "same board can reload with wrapping disabled");
    BeginDrag(board, closed, 0);
    board.Update(ReleaseAtWrapped(Center(closed, 1),
                                  Center(closed, 1) + Vec2{160.0f, 0.0f}),
                 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 1 &&
                       board.MakeSnapshot().tentacles[0].drawTranslations ==
                           std::vector<Vec2>{{0.0f, 0.0f}},
                   "disabled state ignores stale unwrapped input and uses the legacy direct path");

    PuzzleDefinition missingSetting = closed;
    context.Expect(board.Initialize(missingSetting, error) &&
                       !board.MakeSnapshot().playfieldBounds.has_value() &&
                       board.MakeSnapshot().tentacles.empty() &&
                       board.GetCompletedConnectionCount() == 0,
                   "legacy reload is bounds-independent and clears wrapped render state");
}

void TestBundledFirstLinkIntegration(TestContext& context) {
    PuzzleCatalog catalog;
    std::string error;
    context.Expect(
        PuzzleCatalogLoader::Load(PuzzleDataPaths{},
                                  std::string{OBJECT_CONNECT_TEST_RESOURCE_ROOT},
                                  catalog, error),
        "bundled level and map CSV files load for board integration");
    const PuzzleDefinition* const puzzle = catalog.Find("first_link");
    if (puzzle == nullptr) {
        context.Fail("bundled catalog exposes first_link");
        return;
    }

    const auto rootIt = std::find_if(
        puzzle->nodes.begin(), puzzle->nodes.end(),
        [](const NodeDefinition& node) { return node.type == NodeType::Root; });
    const auto endIt = std::find_if(
        puzzle->nodes.begin(), puzzle->nodes.end(),
        [](const NodeDefinition& node) { return node.type == NodeType::End; });
    if (rootIt == puzzle->nodes.end() || endIt == puzzle->nodes.end()) {
        context.Fail("first_link map snapshot contains root and end roles");
        return;
    }
    const std::size_t rootIndex =
        static_cast<std::size_t>(std::distance(puzzle->nodes.begin(), rootIt));
    const std::size_t endIndex =
        static_cast<std::size_t>(std::distance(puzzle->nodes.begin(), endIt));

    const AxisAlignedBox bounds{{0.0f, 0.0f}, {1280.0f, 720.0f}};
    PuzzleBoard board;
    const bool initialized = board.Initialize(*puzzle, bounds, error);
    context.Expect(initialized,
                   "board initializes directly from loaded first_link map snapshot");
    if (!initialized) {
        return;
    }
    const PuzzleBoardSnapshot initial = board.MakeSnapshot();
    context.Expect(initial.nodeStates[rootIndex].active &&
                       initial.nodeStates[rootIndex].availableSource &&
                       !initial.nodeStates[endIndex].active &&
                       !initial.lengthExhausted,
                   "loaded map roles drive initial root/end runtime state");
    context.Expect(rootIt->maxOutgoing == 1 && endIt->maxIncoming == 1 &&
                       NearlyEqual(board.GetRemainingOutgoingLength(rootIndex),
                                   rootIt->maxOutgoingLength, 0.01f),
                   "loaded map capacities and local budget reach the board unchanged");

    const Vec2 canonicalEnd = Center(*puzzle, endIndex);
    context.Expect(puzzle->wrapEdges,
                   "authored first_link integration level enables edge wrapping");
    const Vec2 releaseEnd = canonicalEnd + Vec2{
        -(bounds.maximum.x - bounds.minimum.x), 0.0f};
    const float expectedCommittedLength =
        Length(releaseEnd - Center(*puzzle, rootIndex)) *
        puzzle->minimumSlackRatio;
    context.Expect(
        expectedCommittedLength <= puzzle->totalLength &&
            Length(canonicalEnd - Center(*puzzle, rootIndex)) *
                    puzzle->minimumSlackRatio >
                puzzle->totalLength,
        "first_link authoring requires the left-boundary route to fit its budget");

    BeginDrag(board, *puzzle, rootIndex);
    board.Update(ReleaseAtWrapped(canonicalEnd, releaseEnd), 1.0f / 60.0f);
    context.Expect(board.IsSolved() && board.GetCompletedConnectionCount() == 1,
                   "loaded first_link root can connect to its end and solve");
    if (board.GetCommittedLines().empty()) {
        return;
    }
    context.Expect(board.GetCommittedLines().front().fromNodeIndex == rootIndex &&
                       board.GetCommittedLines().front().toNodeIndex == endIndex,
                   "loaded-map connection is exposed through runtime scoring data");
    context.Expect(
        NearlyEqual(board.GetCommittedLines().front().committedLength,
                    expectedCommittedLength, 0.01f),
        "first_link consumes the authored left-boundary crossing length");
}

} // namespace

void RunPuzzleBoardTests(TestContext& context) {
    TestInitializationAndMissingPlacement(context);
    TestSourceSelectionTransitionContract(context);
    TestTargetConnectionHitPadding(context);
    TestMultiRootBranchMergeAndAllEnds(context);
    TestRolesAndCapacities(context);
    TestDuplicateSelfAndDirectedCycle(context);
    TestGlobalAndPerSourceLengthBudgets(context);
    TestLengthExhaustionRespectsWrapEdges(context);
    TestRetractionRefund(context);
    TestDeadNodeBlockingAndMissingDead(context);
    TestWrappedBoardPathAndBoundsContract(context);
    TestWrappedPreviewRetracesThroughPortal(context);
    TestWrappedBoardCollisionLengthAndStateIsolation(context);
    TestBundledFirstLinkIntegration(context);
}

} // namespace object_connect::tests
