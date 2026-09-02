#include "TestSupport.hpp"

#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <string>

namespace object_connect::tests {
namespace {

[[nodiscard]] PuzzleDefinition MakeLinearPuzzle(const float totalLength = 500.0f) {
    PuzzleDefinition puzzle{};
    puzzle.id = "linear";
    puzzle.title = "LINEAR";
    puzzle.startNodeId = "start";
    puzzle.startNodeIndex = 0;
    puzzle.totalLength = totalLength;
    puzzle.minimumSlackRatio = 1.0f;
    puzzle.baseWidth = 20.0f;
    puzzle.tipWidth = 7.0f;
    puzzle.widthVariation = 0.12f;
    puzzle.vesselColor = {0.7f, 0.1f, 0.2f, 1.0f};

    puzzle.nodes = {
        NodeDefinition{"start", "A", {100.0f, 100.0f}, 20.0f, {}},
        NodeDefinition{"middle", "B", {300.0f, 100.0f}, 20.0f, {}},
        NodeDefinition{"end", "C", {500.0f, 100.0f}, 20.0f, {}},
    };

    ConnectionDefinition first{};
    first.fromNodeId = "start";
    first.toNodeId = "middle";
    first.fromNodeIndex = 0;
    first.toNodeIndex = 1;
    first.pointCount = 8;

    ConnectionDefinition second{};
    second.fromNodeId = "middle";
    second.toNodeId = "end";
    second.fromNodeIndex = 1;
    second.toNodeIndex = 2;
    second.pointCount = 10;
    second.thicknessScale = 0.8f;
    second.initialDirectionDegrees = 15.0f;

    puzzle.connections = {first, second};
    return puzzle;
}

[[nodiscard]] PuzzleDefinition MakeRouteChoicePuzzle() {
    PuzzleDefinition puzzle{};
    puzzle.id = "route_choice";
    puzzle.title = "ROUTE CHOICE";
    puzzle.startNodeId = "heart";
    puzzle.startNodeIndex = 0;
    puzzle.totalLength = 900.0f;
    puzzle.minimumSlackRatio = 1.0f;
    puzzle.baseWidth = 20.0f;
    puzzle.tipWidth = 7.0f;
    puzzle.widthVariation = 0.12f;
    puzzle.vesselColor = {0.7f, 0.1f, 0.2f, 1.0f};

    puzzle.nodes = {
        NodeDefinition{"heart", "HEART", {100.0f, 300.0f}, 24.0f, {}},
        NodeDefinition{"lung", "LUNG", {300.0f, 140.0f}, 24.0f, {}},
        NodeDefinition{"kidney", "KIDNEY", {300.0f, 460.0f}, 24.0f, {}},
        NodeDefinition{"brain", "BRAIN", {600.0f, 300.0f}, 24.0f, {}},
    };

    const auto connection = [](const char* fromId, const char* toId,
                               const std::size_t fromIndex,
                               const std::size_t toIndex,
                               const std::size_t pointCount) {
        ConnectionDefinition result{};
        result.fromNodeId = fromId;
        result.toNodeId = toId;
        result.fromNodeIndex = fromIndex;
        result.toNodeIndex = toIndex;
        result.pointCount = pointCount;
        return result;
    };

    // CSV order is deliberately different from either complete route. Runtime
    // selection must follow the current tip and the chosen target, not row order.
    puzzle.connections = {
        connection("heart", "lung", 0, 1, 8),
        connection("lung", "brain", 1, 3, 9),
        connection("heart", "kidney", 0, 2, 11),
        connection("kidney", "brain", 2, 3, 12),
        connection("heart", "brain", 0, 3, 10),
    };
    return puzzle;
}

[[nodiscard]] BoardPointerInput PressAt(const Vec2 position) {
    return {position, true, true, false};
}

[[nodiscard]] BoardPointerInput DragTo(const Vec2 position) {
    return {position, false, true, false};
}

[[nodiscard]] BoardPointerInput ReleaseAt(const Vec2 position) {
    return {position, false, false, true};
}

void AdvanceRetraction(PuzzleBoard& board) {
    for (int index = 0; index < 5; ++index) {
        board.Update({}, 0.05f);
    }
}

void TestInitializationAndValidation(TestContext& context) {
    PuzzleBoard board;
    context.Expect(!board.IsInitialized(), "board starts uninitialized");

    std::string error;
    const PuzzleDefinition valid = MakeLinearPuzzle();
    context.Expect(board.Initialize(valid, error), "valid puzzle initializes");
    context.Expect(error.empty(), "valid initialization clears its error");
    context.Expect(board.GetTotalLength() == 500.0f,
                   "board exposes the single global length budget");
    context.Expect(board.GetRemainingLength() == 500.0f,
                   "an untouched board has its full budget remaining");

    const PuzzleBoardSnapshot snapshot = board.MakeSnapshot();
    context.Expect(snapshot.activeNodeIndex == 0,
                   "the first connection source is initially active");
    context.Expect(!snapshot.solved && snapshot.tentacles.empty(),
                   "a fresh puzzle has no completed vessel segments");

    PuzzleDefinition invalidLength = MakeLinearPuzzle();
    invalidLength.totalLength = 0.0f;
    context.Expect(!board.Initialize(invalidLength, error),
                   "zero total length is rejected");
    context.Expect(!board.IsInitialized(),
                   "failed reinitialization leaves no stale active puzzle");

    PuzzleDefinition invalidPoints = MakeLinearPuzzle();
    invalidPoints.connections.front().pointCount = 7;
    context.Expect(!board.Initialize(invalidPoints, error),
                   "gameplay connections require at least eight particles");

    PuzzleDefinition invalidStart = MakeLinearPuzzle();
    invalidStart.startNodeIndex = 2;
    context.Expect(!board.Initialize(invalidStart, error),
                   "the first unlocked connection must use the start node");
}

void TestSequentialUnlockAndMonotonicReserve(TestContext& context) {
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(MakeLinearPuzzle(), error),
                   "linear board initializes for interaction test");

    board.Update(PressAt({300.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(!board.IsDragging(),
                   "a locked future source cannot begin a drag");

    board.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(board.IsDragging(), "the current source begins a drag");
    context.Expect(board.GetReservedLength() <= 0.001f,
                   "pressing the source begins with no reserved cable");

    board.Update(DragTo({380.0f, 100.0f}), 1.0f / 60.0f);
    const float longReserve = board.GetReservedLength();
    context.Expect(NearlyEqual(longReserve, 280.0f, 0.01f),
                   "drag distance reserves cable from the global budget");
    context.Expect(NearlyEqual(board.GetRemainingLength() + longReserve,
                               board.GetTotalLength(), 0.01f),
                   "drag reservation plus remaining length preserves the global budget");
    board.Update(DragTo({180.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(NearlyEqual(board.GetReservedLength(), longReserve, 0.01f),
                   "drag reserve never decreases when the pointer comes back");

    board.Update(ReleaseAt({300.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 1,
                   "release on the expected target commits the connection");
    context.Expect(!board.IsDragging(), "committed preview leaves drag state");
    context.Expect(NearlyEqual(board.GetRemainingLength(), 220.0f, 0.01f),
                   "commit spends the full high-water reserve, not only direct distance");
    context.Expect(NearlyEqual(board.GetRemainingLength() + longReserve,
                               board.GetTotalLength(), 0.01f),
                   "committed and remaining lengths preserve the global budget");
    context.Expect(board.MakeSnapshot().activeNodeIndex == 1,
                   "commit makes the chosen target the only active source");

    board.Update(PressAt({300.0f, 100.0f}), 1.0f / 60.0f);
    board.Update(ReleaseAt({500.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(board.IsSolved(), "reaching the linear route terminal solves the board");
    context.Expect(board.GetCompletedConnectionCount() == 2,
                   "all connections chosen on the route are retained after solving");
    const PuzzleBoardSnapshot solved = board.MakeSnapshot();
    context.Expect(solved.tentacles.size() == 2 && solved.solved,
                   "solved snapshot contains both attached ribbons");
    context.Expect(!solved.activeNodeIndex.has_value(),
                   "solved snapshot has no further active source");

    context.Expect(board.Initialize(MakeLinearPuzzle(), error),
                   "retry can rebuild the puzzle session");
    context.Expect(NearlyEqual(board.GetRemainingLength(), 500.0f, 0.01f) &&
                       board.GetCompletedConnectionCount() == 0 &&
                       !board.IsSolved(),
                   "a rebuilt session restores the full global length and path state");
}

void TestInvalidTargetRetractsAndReturnsBudget(TestContext& context) {
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(MakeLinearPuzzle(), error),
                   "board initializes for invalid-target test");

    board.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    board.Update(DragTo({500.0f, 100.0f}), 1.0f / 60.0f);
    const float reservedBeforeRelease = board.GetReservedLength();
    board.Update(ReleaseAt({500.0f, 100.0f}), 1.0f / 60.0f);

    const PuzzleBoardSnapshot retracting = board.MakeSnapshot();
    context.Expect(!retracting.dragging && retracting.retracting,
                   "wrong target begins a visible retraction");
    context.Expect(board.GetCompletedConnectionCount() == 0,
                   "wrong target never commits a connection");
    context.Expect(NearlyEqual(board.GetReservedLength(), reservedBeforeRelease, 0.01f),
                   "retraction begins from the drag high-water length");

    board.Update({}, 0.05f);
    context.Expect(board.GetReservedLength() < reservedBeforeRelease,
                   "retraction progressively releases reserved cable");
    context.Expect(NearlyEqual(board.GetRemainingLength() +
                                   board.GetReservedLength(),
                               board.GetTotalLength(), 0.01f),
                   "retraction refunds cable without changing the total budget");
    AdvanceRetraction(board);
    context.Expect(!board.MakeSnapshot().retracting,
                   "retraction removes its preview after the animation");
    context.Expect(NearlyEqual(board.GetRemainingLength(), board.GetTotalLength(), 0.01f),
                   "failed drag returns all cable to the global budget");
}

void TestRouteChoiceAndProgressInterface(TestContext& context) {
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(MakeRouteChoicePuzzle(), error),
                   "route-choice board initializes");

    board.Update(PressAt({100.0f, 300.0f}), 1.0f / 60.0f);
    board.Update(ReleaseAt({300.0f, 460.0f}), 1.0f / 60.0f);
    context.Expect(board.GetCompletedConnectionCount() == 1 && !board.IsSolved(),
                   "choosing one outgoing target commits only that vessel segment");
    context.Expect(board.MakeSnapshot().activeNodeIndex == 2,
                   "the chosen target becomes the only active vessel tip");
    context.Expect(board.MakeSnapshot().tentacles.front().points.size() == 11,
                   "the selected edge keeps its own tentacle settings");

    const std::vector<std::size_t>& firstVisited = board.GetVisitedNodeIndices();
    const std::vector<std::size_t>& firstConnections =
        board.GetCommittedConnectionIndices();
    context.Expect(firstVisited.size() == 2 && firstVisited[0] == 0 &&
                       firstVisited[1] == 2,
                   "read-only progress exposes the nodes visited by the chosen route");
    context.Expect(firstConnections.size() == 1 && firstConnections[0] == 2,
                   "read-only progress exposes the committed candidate edge");

    board.Update(PressAt({100.0f, 300.0f}), 1.0f / 60.0f);
    context.Expect(!board.IsDragging(),
                   "an earlier branch point cannot create a second vessel branch");
    board.Update(PressAt({300.0f, 460.0f}), 1.0f / 60.0f);
    board.Update(ReleaseAt({600.0f, 300.0f}), 1.0f / 60.0f);
    context.Expect(board.IsSolved(),
                   "reaching the route graph terminal solves the abstract puzzle");
    context.Expect(board.GetVisitedNodeIndices().size() == 3 &&
                       board.GetVisitedNodeIndices().back() == 3,
                   "the completed route is available for a later scoring system");

    context.Expect(board.Initialize(MakeRouteChoicePuzzle(), error),
                   "route-choice board can be rebuilt for a direct route");
    board.Update(PressAt({100.0f, 300.0f}), 1.0f / 60.0f);
    board.Update(ReleaseAt({600.0f, 300.0f}), 1.0f / 60.0f);
    context.Expect(board.IsSolved() && board.GetCompletedConnectionCount() == 1,
                   "a direct connection to the same abstract terminal is allowed");
    context.Expect(board.GetVisitedNodeIndices().size() == 2 &&
                       board.GetCommittedConnectionIndices().front() == 4,
                   "direct and organ-rich routes remain distinguishable for future scoring");
}

void TestLineOfSightAndLengthValidation(TestContext& context) {
    PuzzleDefinition blocked = MakeLinearPuzzle();
    ObstacleDefinition wall{};
    wall.id = "wall";
    wall.shape = ObstacleShape::Rectangle;
    wall.center = {200.0f, 100.0f};
    wall.width = 40.0f;
    wall.height = 100.0f;
    blocked.obstacles.push_back(wall);

    PuzzleBoard blockedBoard;
    std::string error;
    context.Expect(blockedBoard.Initialize(blocked, error),
                   "board accepts obstacle data for runtime LOS validation");
    blockedBoard.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    blockedBoard.Update(ReleaseAt({300.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(blockedBoard.MakeSnapshot().retracting,
                   "an obstacle-blocked target retracts instead of committing");
    context.Expect(blockedBoard.GetCompletedConnectionCount() == 0,
                   "blocked line of sight cannot complete a segment");

    PuzzleBoard shortBoard;
    context.Expect(shortBoard.Initialize(MakeLinearPuzzle(150.0f), error),
                   "short budget can initialize and report exhaustion at runtime");
    context.Expect(shortBoard.IsLengthExhausted(),
                   "budget shorter than the next minimum route is exhausted");
    shortBoard.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    shortBoard.Update(ReleaseAt({300.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(shortBoard.GetCompletedConnectionCount() == 0,
                   "target commit fails when its minimum length exceeds the budget");
    context.Expect(shortBoard.MakeSnapshot().retracting,
                   "insufficient-length release retracts its preview");

    PuzzleBoard wasteBoard;
    context.Expect(wasteBoard.Initialize(MakeLinearPuzzle(400.0f), error),
                   "exact minimum global budget initializes");
    wasteBoard.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    wasteBoard.Update(DragTo({350.0f, 100.0f}), 1.0f / 60.0f);
    wasteBoard.Update(ReleaseAt({300.0f, 100.0f}), 1.0f / 60.0f);
    context.Expect(wasteBoard.GetCompletedConnectionCount() == 1,
                   "over-deployed first connection still commits at its target");
    context.Expect(wasteBoard.IsLengthExhausted(),
                   "wasted cable can leave too little for the next unlocked connection");
}

void TestCancelSnapshotAndAttachedUpdates(TestContext& context) {
    PuzzleBoard board;
    std::string error;
    context.Expect(board.Initialize(MakeLinearPuzzle(), error),
                   "board initializes for cancel and snapshot test");

    board.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    board.Update(DragTo({220.0f, 140.0f}), 1.0f / 60.0f);
    PuzzleBoardSnapshot preview = board.MakeSnapshot();
    context.Expect(preview.tentacles.size() == 1 && preview.tentacles.front().preview,
                   "snapshot marks the active tentacle as a preview");
    context.Expect(preview.dragging && preview.reservedLength > 0.0f,
                   "snapshot reports drag and reservation state");

    board.CancelDrag(true);
    context.Expect(!board.IsDragging() && board.MakeSnapshot().tentacles.empty(),
                   "immediate cancellation removes the preview without committing");
    context.Expect(NearlyEqual(board.GetRemainingLength(), 500.0f, 0.01f),
                   "immediate cancellation restores the complete budget");

    board.Update(PressAt({100.0f, 100.0f}), 1.0f / 60.0f);
    board.Update(ReleaseAt({300.0f, 100.0f}), 1.0f / 60.0f);
    board.Update({}, 1.0f / 30.0f);
    const PuzzleBoardSnapshot attached = board.MakeSnapshot();
    context.Expect(attached.tentacles.size() == 1 && !attached.tentacles.front().preview,
                   "committed connection remains as an attached segment");
    const std::vector<Vec2>& points = attached.tentacles.front().points;
    context.Expect(!points.empty(), "attached segment exposes simulation points");
    if (!points.empty()) {
        context.Expect(points.front() == Vec2{100.0f, 100.0f},
                       "attached segment update keeps its root on the source node");
        context.Expect(points.back() == Vec2{300.0f, 100.0f},
                       "attached segment update keeps its tip on the target node");
    }
}

} // namespace

void RunPuzzleBoardTests(TestContext& context) {
    TestInitializationAndValidation(context);
    TestSequentialUnlockAndMonotonicReserve(context);
    TestInvalidTargetRetractsAndReturnsBudget(context);
    TestRouteChoiceAndProgressInterface(context);
    TestLineOfSightAndLengthValidation(context);
    TestCancelSnapshotAndAttachedUpdates(context);
}

} // namespace object_connect::tests
