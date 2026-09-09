#include "TestSupport.hpp"

#include "ObjectConnect/Rendering/MouseCursorRenderer.hpp"

#include <array>

namespace object_connect::tests {
namespace {

void TestScreenPriority(TestContext& context) {
    constexpr std::array kNonPlayingScreens{
        GameScreen::MainMenu,
        GameScreen::LevelSelect,
        GameScreen::Paused,
        GameScreen::Solved,
        GameScreen::FinalResults,
    };
    for (const GameScreen screen : kNonPlayingScreens) {
        context.Expect(
            ResolveMouseCursorIcon(screen, false, true) ==
                MouseCursorIcon::Interactive,
            "an actionable non-playing screen uses the interactive cursor");
        context.Expect(
            ResolveMouseCursorIcon(screen, false, false) ==
                MouseCursorIcon::Normal,
            "blank non-playing space uses the normal cursor");
    }

    context.Expect(
        ResolveMouseCursorIcon(GameScreen::Playing, false, true) ==
            MouseCursorIcon::Normal,
        "playing ignores UI hover when no line is held");
    context.Expect(
        ResolveMouseCursorIcon(GameScreen::Playing, true, true) ==
            MouseCursorIcon::Holding,
        "holding has priority over every hover state while playing");

    context.Expect(
        ResolveMouseCursorIcon(GameScreen::LevelSelect, false, true) ==
            MouseCursorIcon::Interactive,
        "an enabled level-page arrow is an interactive action");
    context.Expect(
        ResolveMouseCursorIcon(GameScreen::LevelSelect, false, false) ==
            MouseCursorIcon::Normal,
        "a disabled level-page arrow remains the normal cursor");
    context.Expect(
        ResolveMouseCursorIcon(GameScreen::Solved, false, false) ==
            MouseCursorIcon::Normal,
        "the solved menu remains non-interactive during its delay");
    context.Expect(
        ResolveMouseCursorIcon(GameScreen::Solved, false, true) ==
            MouseCursorIcon::Interactive,
        "the solved menu becomes interactive after its delay");
    context.Expect(
        ResolveMouseCursorIcon(GameScreen::FinalResults, false, true) ==
            MouseCursorIcon::Interactive,
        "a final-results button uses the interactive cursor");
}

void TestVisibilityGate(TestContext& context) {
    context.Expect(ShouldUseCustomMouseCursor(true, true),
                   "focused client-local pointers use the custom cursor");
    context.Expect(!ShouldUseCustomMouseCursor(false, true),
                   "focus loss restores the system cursor");
    context.Expect(!ShouldUseCustomMouseCursor(true, false),
                   "leaving the client restores the system cursor");
    context.Expect(!ShouldUseCustomMouseCursor(false, false),
                   "an unfocused external pointer never hides the system cursor");
}

} // namespace

void RunMouseCursorStateTests(TestContext& context) {
    TestScreenPriority(context);
    TestVisibilityGate(context);
}

} // namespace object_connect::tests
