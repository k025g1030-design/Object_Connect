#include "../TestSupport.hpp"

#include "RetroFPS/Game/GameConfig.hpp"
#include "RetroFPS/Game/GameFlow.hpp"

namespace fps::tests {
namespace {

void TestMainMenu(TestContext& context) {
    GameFlow flow;
    context.Expect(flow.GetScreen() == GameScreen::MainMenu, "flow starts at main menu");
    context.Expect(flow.GetSelectedItem() == 0, "start game is initially selected");

    GameFlowInput next{};
    next.nextPressed = true;
    static_cast<void>(flow.Update(next));
    context.Expect(flow.GetSelectedItem() == 1, "keyboard moves main-menu selection");

    GameFlowInput confirm{};
    confirm.confirmPressed = true;
    const GameFlowResult controls = flow.Update(confirm);
    context.Expect(controls.screenChanged, "controls selection changes screen");
    context.Expect(flow.GetScreen() == GameScreen::Controls, "controls screen opens");

    GameFlowInput back{};
    back.escapePressed = true;
    static_cast<void>(flow.Update(back));
    context.Expect(flow.GetScreen() == GameScreen::MainMenu, "escape returns from controls");

    GameFlowInput clickStart{};
    clickStart.hoveredItem = 0;
    clickStart.mousePrimaryPressed = true;
    const GameFlowResult started = flow.Update(clickStart);
    context.Expect(started.action == GameFlowAction::StartGame, "mouse starts game");
    context.Expect(flow.GetScreen() == GameScreen::Playing, "start enters playing");
    context.Expect(!started.simulateGameplay, "start transition does not simulate gameplay");
}

void TestPauseAndFocus(TestContext& context) {
    GameFlow flow;
    GameFlowInput start{};
    start.confirmPressed = true;
    static_cast<void>(flow.Update(start));

    GameFlowInput pause{};
    pause.escapePressed = true;
    const GameFlowResult paused = flow.Update(pause);
    context.Expect(paused.screenChanged, "escape pauses gameplay");
    context.Expect(flow.GetScreen() == GameScreen::Paused, "paused screen is active");
    context.Expect(!paused.simulateGameplay, "pause transition does not simulate gameplay");

    const GameFlowResult frozen = flow.Update({});
    context.Expect(!frozen.simulateGameplay, "paused frames do not simulate gameplay");

    const GameFlowResult resumed = flow.Update(pause);
    context.Expect(resumed.screenChanged, "escape resumes gameplay");
    context.Expect(flow.GetScreen() == GameScreen::Playing, "resume returns to playing");
    context.Expect(!resumed.simulateGameplay, "resume transition waits until the next frame");

    const GameFlowResult running = flow.Update({});
    context.Expect(running.simulateGameplay, "stable playing frames simulate gameplay");

    GameFlowInput focusLost{};
    focusLost.focusLost = true;
    static_cast<void>(flow.Update(focusLost));
    context.Expect(flow.GetScreen() == GameScreen::Paused, "focus loss pauses gameplay");

    GameFlowInput mainMenu{};
    mainMenu.hoveredItem = 1;
    mainMenu.mousePrimaryPressed = true;
    const GameFlowResult reset = flow.Update(mainMenu);
    context.Expect(
        reset.action == GameFlowAction::ResetToMainMenu,
        "pause menu requests gameplay reset");
    context.Expect(flow.GetScreen() == GameScreen::MainMenu, "pause menu returns to main menu");
}

void TestSelectionAndQuit(TestContext& context) {
    GameFlow flow;
    GameFlowInput previous{};
    previous.previousPressed = true;
    static_cast<void>(flow.Update(previous));
    context.Expect(flow.GetSelectedItem() == 2, "selection wraps upward");

    GameFlowInput quit{};
    quit.confirmPressed = true;
    const GameFlowResult result = flow.Update(quit);
    context.Expect(result.action == GameFlowAction::QuitGame, "main menu requests quit");

    flow.ReturnToMainMenu();
    context.Expect(flow.GetSelectedItem() == 0, "returning to main resets selection");
}

void TestMapProgression(TestContext& context) {
    const GameConfig defaultConfig{};
    context.Expect(defaultConfig.mapPaths.size() == 2, "default campaign has two maps");
    if (defaultConfig.mapPaths.size() == 2) {
        context.Expect(
            defaultConfig.mapPaths[0].filename() == "mvp_map.txt",
            "default campaign starts with the MVP map");
        context.Expect(
            defaultConfig.mapPaths[1].filename() == "mvp_map_02.txt",
            "default campaign includes the second map");
    }

    const std::optional<std::size_t> secondMap = TryGetNextMapIndex(0, 2);
    context.Expect(secondMap == 1, "first map advances to the second map");
    context.Expect(!TryGetNextMapIndex(1, 2).has_value(), "last map completes campaign");
    context.Expect(!TryGetNextMapIndex(0, 1).has_value(), "single-map campaign completes");
    context.Expect(!TryGetNextMapIndex(2, 2).has_value(), "invalid map index cannot advance");
}

} // namespace

void RunGameFlowTests(TestContext& context) {
    TestMainMenu(context);
    TestPauseAndFocus(context);
    TestSelectionAndQuit(context);
    TestMapProgression(context);
}

} // namespace fps::tests
