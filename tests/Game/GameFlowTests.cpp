#include "../TestSupport.hpp"

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
    context.Expect(started.action == GameFlowAction::RequestStartGame, "mouse requests game start");
    context.Expect(!started.screenChanged, "start request defers the screen change");
    context.Expect(flow.GetScreen() == GameScreen::MainMenu,
                   "start request keeps the main menu active until committed");
    context.Expect(!started.simulateGameplay, "start request does not simulate gameplay");

    flow.EnterPlaying();
    context.Expect(flow.GetScreen() == GameScreen::Playing, "start commit enters playing");
    context.Expect(flow.GetSelectedItem() == 0, "start commit resets menu selection");
}

void TestPauseAndFocus(TestContext& context) {
    GameFlow flow;
    flow.EnterPlaying();

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
    context.Expect(reset.action == GameFlowAction::RequestMainMenu,
                   "pause menu requests a return to the main menu");
    context.Expect(!reset.screenChanged, "main-menu request defers the screen change");
    context.Expect(flow.GetScreen() == GameScreen::Paused,
                   "main-menu request keeps the pause screen active until committed");

    flow.ReturnToMainMenu();
    context.Expect(flow.GetScreen() == GameScreen::MainMenu,
                   "main-menu commit leaves the pause screen");
}

void TestExplicitScreenCommits(TestContext& context) {
    GameFlow flow;
    flow.EnterPlaying();
    context.Expect(flow.GetScreen() == GameScreen::Playing, "playing can be committed directly");

    flow.EnterPaused();
    context.Expect(flow.GetScreen() == GameScreen::Paused, "pause can be committed directly");
    context.Expect(flow.GetSelectedItem() == 0, "pause commit selects resume");

    flow.ReturnToMainMenu();
    context.Expect(flow.GetScreen() == GameScreen::MainMenu, "main menu can be committed directly");
    context.Expect(flow.GetSelectedItem() == 0, "main-menu commit selects start");
}

void TestResults(TestContext& context) {
    GameFlow flow;
    flow.EnterResults();
    context.Expect(flow.GetScreen() == GameScreen::Results, "results can be committed directly");
    context.Expect(flow.GetSelectedItem() == 0, "results selects its main-menu item");

    const GameFlowResult idle = flow.Update({});
    context.Expect(!idle.simulateGameplay, "results frames do not simulate gameplay");

    GameFlowInput navigation{};
    navigation.previousPressed = true;
    static_cast<void>(flow.Update(navigation));
    navigation.previousPressed = false;
    navigation.nextPressed = true;
    static_cast<void>(flow.Update(navigation));
    context.Expect(flow.GetSelectedItem() == 0, "results navigation keeps the only item selected");

    GameFlowInput escape{};
    escape.escapePressed = true;
    const GameFlowResult ignoredEscape = flow.Update(escape);
    context.Expect(ignoredEscape.action == GameFlowAction::None, "results ignores escape");
    context.Expect(!ignoredEscape.screenChanged, "escape does not leave results");
    context.Expect(flow.GetScreen() == GameScreen::Results, "results remains active after escape");

    GameFlowInput focusLost{};
    focusLost.focusLost = true;
    const GameFlowResult ignoredFocusLoss = flow.Update(focusLost);
    context.Expect(ignoredFocusLoss.action == GameFlowAction::None, "results ignores focus loss");
    context.Expect(!ignoredFocusLoss.screenChanged, "focus loss does not leave results");
    context.Expect(flow.GetScreen() == GameScreen::Results,
                   "results remains active after focus loss");

    GameFlowInput confirm{};
    confirm.confirmPressed = true;
    const GameFlowResult confirmed = flow.Update(confirm);
    context.Expect(confirmed.action == GameFlowAction::RequestMainMenu,
                   "results enter requests the main menu");
    context.Expect(!confirmed.screenChanged, "results enter defers the screen change");
    context.Expect(flow.GetScreen() == GameScreen::Results,
                   "results remains active until the main menu is committed");

    GameFlow clickedFlow;
    clickedFlow.EnterResults();
    GameFlowInput click{};
    click.hoveredItem = 0;
    click.mousePrimaryPressed = true;
    const GameFlowResult clicked = clickedFlow.Update(click);
    context.Expect(clicked.action == GameFlowAction::RequestMainMenu,
                   "results main-menu button requests the main menu");
    context.Expect(!clicked.screenChanged, "results click defers the screen change");
    context.Expect(clickedFlow.GetScreen() == GameScreen::Results,
                   "results click keeps the screen active until committed");
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

} // namespace

void RunGameFlowTests(TestContext& context) {
    TestMainMenu(context);
    TestPauseAndFocus(context);
    TestExplicitScreenCommits(context);
    TestResults(context);
    TestSelectionAndQuit(context);
}

} // namespace fps::tests
