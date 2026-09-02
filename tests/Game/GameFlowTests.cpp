#include "TestSupport.hpp"

#include "ObjectConnect/Game/GameFlow.hpp"

namespace object_connect::tests {
namespace {

constexpr std::size_t kPuzzleCount = 3;

[[nodiscard]] GameFlowResult ClickItem(GameFlow& flow, const std::size_t item,
                                       const bool currentPuzzleIsLast = false) {
    GameFlowInput input{};
    input.hoveredItem = item;
    input.mousePrimaryPressed = true;
    return flow.Update(input, kPuzzleCount, currentPuzzleIsLast);
}

void TestMainMenu(TestContext& context) {
    GameFlow flow;
    context.Expect(flow.GetScreen() == GameScreen::MainMenu,
                   "flow starts at the main menu");
    context.Expect(flow.GetItemCount(kPuzzleCount, false) == 2,
                   "main menu contains play and exit");

    GameFlowInput previous{};
    previous.previousPressed = true;
    static_cast<void>(flow.Update(previous, kPuzzleCount, false));
    context.Expect(flow.GetSelectedItem() == 1,
                   "main-menu navigation wraps to exit");

    GameFlowInput confirm{};
    confirm.confirmPressed = true;
    const GameFlowResult quit = flow.Update(confirm, kPuzzleCount, false);
    context.Expect(quit.command == GameCommand::QuitGame,
                   "main-menu exit requests game shutdown");

    flow.ReturnToMainMenu();
    const GameFlowResult play = ClickItem(flow, 0);
    context.Expect(play.command == GameCommand::OpenLevelSelect,
                   "play requests the level-select screen");
    context.Expect(!play.screenChanged,
                   "level-select request waits for the caller to commit it");
    context.Expect(flow.GetScreen() == GameScreen::MainMenu,
                   "deferred play request leaves the current screen active");

    flow.EnterLevelSelect();
    context.Expect(flow.GetScreen() == GameScreen::LevelSelect,
                   "caller can commit the level-select screen");
    context.Expect(flow.GetSelectedItem() == 0,
                   "screen commits reset menu selection");
}

void TestLevelSelect(TestContext& context) {
    GameFlow flow;
    flow.EnterLevelSelect();
    context.Expect(flow.GetItemCount(kPuzzleCount, false) == kPuzzleCount + 1,
                   "level select adds one back item after every puzzle");

    const GameFlowResult puzzle = ClickItem(flow, 1);
    context.Expect(puzzle.command == GameCommand::StartPuzzle,
                   "a level item requests puzzle startup");
    context.Expect(puzzle.puzzleIndex == 1,
                   "level startup identifies the selected puzzle");
    context.Expect(!puzzle.screenChanged,
                   "puzzle startup waits for construction before entering play");

    const GameFlowResult back = ClickItem(flow, kPuzzleCount);
    context.Expect(back.command == GameCommand::ReturnToMainMenu,
                   "the final level-select item returns to the main menu");

    GameFlowInput escape{};
    escape.escapePressed = true;
    const GameFlowResult escaped = flow.Update(escape, kPuzzleCount, false);
    context.Expect(escaped.command == GameCommand::ReturnToMainMenu,
                   "escape also requests the main menu from level select");

    GameFlow emptyFlow;
    emptyFlow.EnterLevelSelect();
    context.Expect(emptyFlow.GetItemCount(0, false) == 1,
                   "an empty catalog still exposes a back item");
    const GameFlowResult emptyBack = emptyFlow.Update({.confirmPressed = true}, 0, false);
    context.Expect(emptyBack.command == GameCommand::ReturnToMainMenu,
                   "the only empty-catalog item returns to the main menu");
}

void TestPlayingAndPause(TestContext& context) {
    GameFlow flow;
    flow.EnterPlaying();
    const GameFlowResult running = flow.Update({}, kPuzzleCount, false);
    context.Expect(running.simulatePuzzle,
                   "stable playing frames advance the puzzle");

    GameFlowInput pause{};
    pause.escapePressed = true;
    const GameFlowResult paused = flow.Update(pause, kPuzzleCount, false);
    context.Expect(paused.screenChanged && !paused.simulatePuzzle,
                   "escape enters pause without simulating that frame");
    context.Expect(flow.GetScreen() == GameScreen::Paused,
                   "pause becomes the active screen");
    context.Expect(flow.GetItemCount(kPuzzleCount, false) == 4,
                   "pause contains resume, level select, main menu, and exit");

    const GameFlowResult resumed = flow.Update(pause, kPuzzleCount, false);
    context.Expect(resumed.screenChanged && flow.GetScreen() == GameScreen::Playing,
                   "escape resumes from pause");
    context.Expect(!resumed.simulatePuzzle,
                   "resume waits until the next frame to simulate");

    flow.EnterPaused();
    const GameFlowResult resumeButton = ClickItem(flow, 0);
    context.Expect(resumeButton.screenChanged && flow.GetScreen() == GameScreen::Playing,
                   "pause resume item changes screen immediately");

    flow.EnterPaused();
    context.Expect(ClickItem(flow, 1).command == GameCommand::OpenLevelSelect,
                   "pause can request level select");
    context.Expect(ClickItem(flow, 2).command == GameCommand::ReturnToMainMenu,
                   "pause can request the main menu");
    context.Expect(ClickItem(flow, 3).command == GameCommand::QuitGame,
                   "pause can request game shutdown");

    flow.EnterPlaying();
    GameFlowInput focusLost{};
    focusLost.focusLost = true;
    const GameFlowResult focusPause = flow.Update(focusLost, kPuzzleCount, false);
    context.Expect(focusPause.screenChanged && flow.GetScreen() == GameScreen::Paused,
                   "focus loss pauses active gameplay");
}

void TestSolvedMenus(TestContext& context) {
    GameFlow flow;
    flow.EnterSolved();
    context.Expect(flow.GetItemCount(kPuzzleCount, false) == 3,
                   "non-final solved menu contains next, level select, and retry");
    context.Expect(ClickItem(flow, 0).command == GameCommand::NextPuzzle,
                   "non-final solved menu can advance to the next puzzle");
    context.Expect(ClickItem(flow, 1).command == GameCommand::OpenLevelSelect,
                   "non-final solved menu can open level select");
    context.Expect(ClickItem(flow, 2).command == GameCommand::RetryPuzzle,
                   "non-final solved menu can retry the current puzzle");

    flow.EnterSolved();
    context.Expect(flow.GetItemCount(kPuzzleCount, true) == 2,
                   "final solved menu omits next puzzle");
    context.Expect(ClickItem(flow, 0, true).command == GameCommand::OpenLevelSelect,
                   "final solved menu returns to level select");
    context.Expect(ClickItem(flow, 1, true).command == GameCommand::RetryPuzzle,
                   "final solved menu retains retry");

    GameFlowInput escape{};
    escape.escapePressed = true;
    const GameFlowResult escaped = flow.Update(escape, kPuzzleCount, true);
    context.Expect(escaped.command == GameCommand::OpenLevelSelect,
                   "escape requests level select from solved");
    context.Expect(!escaped.simulatePuzzle,
                   "solved screens never simulate the puzzle");
}

void TestSelectionTracksDynamicMenus(TestContext& context) {
    GameFlow flow;
    flow.EnterLevelSelect();
    static_cast<void>(ClickItem(flow, 3));
    context.Expect(flow.GetSelectedItem() == 3,
                   "mouse hover updates the selected level-select item");

    static_cast<void>(flow.Update({}, 1, false));
    context.Expect(flow.GetSelectedItem() == 0,
                   "selection resets when a dynamic menu becomes shorter");

    GameFlowInput both{};
    both.previousPressed = true;
    both.nextPressed = true;
    static_cast<void>(flow.Update(both, 1, false));
    context.Expect(flow.GetSelectedItem() == 0,
                   "opposing navigation inputs cancel each other");

    flow.ReturnToMainMenu();
    GameFlowInput movedOverPlay{};
    movedOverPlay.mouseMoved = true;
    movedOverPlay.hoveredItem = 0;
    static_cast<void>(flow.Update(movedOverPlay, kPuzzleCount, false));

    GameFlowInput keyboardNextWithStationaryPointer{};
    keyboardNextWithStationaryPointer.nextPressed = true;
    keyboardNextWithStationaryPointer.hoveredItem = 0;
    static_cast<void>(flow.Update(keyboardNextWithStationaryPointer,
                                  kPuzzleCount, false));
    context.Expect(flow.GetSelectedItem() == 1,
                   "a stationary mouse does not steal keyboard selection");

    GameFlowInput confirmWithStationaryPointer{};
    confirmWithStationaryPointer.confirmPressed = true;
    confirmWithStationaryPointer.hoveredItem = 0;
    const GameFlowResult keyboardChoice =
        flow.Update(confirmWithStationaryPointer, kPuzzleCount, false);
    context.Expect(keyboardChoice.command == GameCommand::QuitGame,
                   "keyboard confirmation keeps the selection despite persistent hover");
}

} // namespace

void RunGameFlowTests(TestContext& context) {
    TestMainMenu(context);
    TestLevelSelect(context);
    TestPlayingAndPause(context);
    TestSolvedMenus(context);
    TestSelectionTracksDynamicMenus(context);
}

} // namespace object_connect::tests
