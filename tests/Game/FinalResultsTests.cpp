#include "TestSupport.hpp"

#include "ObjectConnect/Game/FinalResults.hpp"

#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace object_connect::tests {
namespace {

NodeDefinition MakeNode(std::string id, const NodeType type,
                        const bool placed = true) {
    NodeDefinition node{};
    node.id = std::move(id);
    node.type = type;
    node.widthTiles = 3;
    node.heightTiles = 3;
    if (placed) {
        node.tilePosition = TilePosition{1, 1};
    }
    return node;
}

PuzzleDefinition MakePuzzle(std::string id, std::string title,
                            std::vector<NodeDefinition> nodes) {
    PuzzleDefinition puzzle{};
    puzzle.id = std::move(id);
    puzzle.title = std::move(title);
    puzzle.nodes = std::move(nodes);
    return puzzle;
}

PuzzleBoardSnapshot MakeSnapshot(std::vector<bool> activeStates) {
    PuzzleBoardSnapshot snapshot{};
    snapshot.nodeStates.resize(activeStates.size());
    for (std::size_t index = 0; index < activeStates.size(); ++index) {
        snapshot.nodeStates[index].active = activeStates[index];
    }
    return snapshot;
}

void TestFinalResultsTarget(TestContext& context) {
    PuzzleDefinition puzzle{};
    context.Expect(!IsFinalResultsTarget(puzzle),
                   "a level without a next target does not open final results");

    puzzle.nextLevelId = std::string{kFinalResultsTargetId};
    context.Expect(IsFinalResultsTarget(puzzle),
                   "the reserved data target opens final results");

    puzzle.nextLevelId = "stage_02";
    context.Expect(!IsFinalResultsTarget(puzzle),
                   "a normal next level is not the final-results target");
}

void TestFinalResultsPageMath(TestContext& context) {
    context.Expect(GetFinalResultsPageCount(0) == 1 &&
                       GetFinalResultsPageCount(1) == 1 &&
                       GetFinalResultsPageCount(10) == 1,
                   "empty and single-page final results report one page");
    context.Expect(GetFinalResultsPageCount(11) == 2 &&
                       GetFinalResultsPageCount(20) == 2 &&
                       GetFinalResultsPageCount(100) == 10,
                   "final-results page count rounds stage rows upward");

    context.Expect(ClampFinalResultsPage(3, 0) == 0 &&
                       ClampFinalResultsPage(1, 1) == 0 &&
                       ClampFinalResultsPage(1, 10) == 0,
                   "page clamping keeps empty and one-page summaries on page zero");
    context.Expect(ClampFinalResultsPage(99, 11) == 1 &&
                       ClampFinalResultsPage(2, 20) == 1 &&
                       ClampFinalResultsPage(99, 100) == 9,
                   "page clamping saturates at the last available page");

    context.Expect(MoveFinalResultsPage(0, -1, 100) == 0 &&
                       MoveFinalResultsPage(
                           0, (std::numeric_limits<std::ptrdiff_t>::min)(),
                           100) == 0,
                   "backward page movement saturates at the first page");
    context.Expect(MoveFinalResultsPage(9, 1, 100) == 9 &&
                       MoveFinalResultsPage(
                           0, (std::numeric_limits<std::ptrdiff_t>::max)(),
                           100) == 9,
                   "forward page movement saturates at the last page");
    context.Expect(MoveFinalResultsPage(5, -2, 100) == 3 &&
                       MoveFinalResultsPage(5, 3, 100) == 8 &&
                       MoveFinalResultsPage(99, -1, 100) == 8,
                   "page movement applies signed deltas after clamping current page");
    context.Expect(MoveFinalResultsPage(1, 1, 1) == 0 &&
                       MoveFinalResultsPage(1, -1, 10) == 0 &&
                       MoveFinalResultsPage(0, 1, 11) == 1 &&
                       MoveFinalResultsPage(0, 5, 20) == 1,
                   "page movement handles 1, 10, 11, and 20 stage boundaries");
}

void TestRoleFilteringAndConnectionCounts(TestContext& context) {
    PuzzleDefinition puzzle = MakePuzzle(
        "stage_20", "stage 20",
        {
            MakeNode("heart", NodeType::Root),
            MakeNode("lung", NodeType::Follow),
            MakeNode("liver", NodeType::Follow),
            MakeNode("brain", NodeType::End),
            MakeNode("bone", NodeType::Dead),
            MakeNode("hidden_root", NodeType::Root, false),
            MakeNode("hidden_follow", NodeType::Follow, false),
            MakeNode("hidden_end", NodeType::End, false),
        });
    PuzzleBoardSnapshot snapshot =
        MakeSnapshot({true, true, false, true, true, true, true, true});
    snapshot.nodeStates[0].incomingUsed = 0;
    snapshot.nodeStates[1].outgoingUsed = 0;

    RunCompletionTracker tracker;
    tracker.RecordCompletedStage(puzzle, snapshot);
    const FinalResultsSummary summary = tracker.BuildSummary();

    context.Expect(summary.stages.size() == 1,
                   "recording one completion appends one stage row");
    context.Expect(summary.stages[0].puzzleId == "stage_20" &&
                       summary.stages[0].stageTitle == "stage 20",
                   "a stage row owns its authored ID and title");
    context.Expect(summary.stages[0].connectedOrganCount == 3 &&
                       summary.stages[0].totalOrganCount == 4,
                   "placed roots, follows, and ends count while dead and hidden nodes do not");
}

void TestShortSnapshotCountsUnconnected(TestContext& context) {
    const PuzzleDefinition puzzle = MakePuzzle(
        "short_snapshot", "short snapshot",
        {
            MakeNode("heart", NodeType::Root),
            MakeNode("kidney", NodeType::Follow),
            MakeNode("stomach", NodeType::Follow),
            MakeNode("brain", NodeType::End),
        });

    RunCompletionTracker tracker;
    tracker.RecordCompletedStage(puzzle, MakeSnapshot({true, true}));
    const FinalResultsSummary summary = tracker.BuildSummary();
    context.Expect(summary.stages.size() == 1 &&
                       summary.stages[0].connectedOrganCount == 2 &&
                       summary.stages[0].totalOrganCount == 4,
                   "missing runtime states remain in the denominator but not the numerator");
}

void TestStageOrderAndRetry(TestContext& context) {
    const PuzzleDefinition first = MakePuzzle(
        "stage_01", "stage 01",
        {MakeNode("heart", NodeType::Root),
         MakeNode("brain", NodeType::End)});
    const PuzzleDefinition second = MakePuzzle(
        "stage_04", "stage 04",
        {MakeNode("heart", NodeType::Root),
         MakeNode("lung", NodeType::Follow),
         MakeNode("brain", NodeType::End)});

    RunCompletionTracker tracker;
    tracker.RecordCompletedStage(first, MakeSnapshot({true, true}));
    tracker.RecordCompletedStage(second, MakeSnapshot({true, false, true}));
    FinalResultsSummary summary = tracker.BuildSummary();
    context.Expect(summary.stages.size() == 2 &&
                       summary.stages[0].puzzleId == "stage_01" &&
                       summary.stages[1].puzzleId == "stage_04" &&
                       summary.stages[0].connectedOrganCount == 2 &&
                       summary.stages[0].totalOrganCount == 2 &&
                       summary.stages[1].connectedOrganCount == 2 &&
                       summary.stages[1].totalOrganCount == 3,
                   "completed stages retain record order and independent counts");

    context.Expect(!tracker.RemoveLastCompletedStage("stage_01"),
                   "retry cannot remove a non-tail completion");
    context.Expect(tracker.BuildSummary().stages.size() == 2,
                   "a mismatched retry leaves completion history intact");
    context.Expect(tracker.RemoveLastCompletedStage("stage_04"),
                   "retry removes the matching most-recent completion");
    summary = tracker.BuildSummary();
    context.Expect(summary.stages.size() == 1 &&
                       summary.stages[0].puzzleId == "stage_01",
                   "retry rebuilding excludes only the removed stage");

    tracker.RecordCompletedStage(first, MakeSnapshot({true, true}));
    context.Expect(tracker.BuildSummary().stages.size() == 2,
                   "recording the same puzzle again appends another completion");
    tracker.Reset();
    context.Expect(tracker.BuildSummary().stages.empty(),
                   "reset clears the whole run history");
}

void TestDirectStageSelection(TestContext& context) {
    const PuzzleDefinition stage20 = MakePuzzle(
        "stage_20", "stage 20",
        {MakeNode("heart", NodeType::Root),
         MakeNode("stomach", NodeType::Follow),
         MakeNode("brain", NodeType::End)});
    RunCompletionTracker tracker;
    tracker.RecordCompletedStage(stage20, MakeSnapshot({true, false, true}));

    const FinalResultsSummary summary = tracker.BuildSummary();
    context.Expect(summary.stages.size() == 1 &&
                       summary.stages[0].puzzleId == "stage_20" &&
                       summary.stages[0].connectedOrganCount == 2 &&
                       summary.stages[0].totalOrganCount == 3,
                   "a directly selected final stage produces one self-contained row");
}

} // namespace

void RunFinalResultsTests(TestContext& context) {
    TestFinalResultsTarget(context);
    TestFinalResultsPageMath(context);
    TestRoleFilteringAndConnectionCounts(context);
    TestShortSnapshotCountsUnconnected(context);
    TestStageOrderAndRetry(context);
    TestDirectStageSelection(context);
}

} // namespace object_connect::tests
