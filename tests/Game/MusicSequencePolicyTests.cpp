#include "TestSupport.hpp"

#include "ObjectConnect/Game/MusicSequencePolicy.hpp"

namespace object_connect::tests {

void RunMusicSequencePolicyTests(TestContext& context) {
    MusicSequencePolicy policy;
    context.Expect(!policy.OnScreenEntered(GameScreen::MainMenu),
                   "initial main-menu entry does not restart the intro");
    context.Expect(!policy.OnScreenEntered(GameScreen::LevelSelect),
                   "main menu to level select leaves music untouched");
    context.Expect(!policy.OnScreenEntered(GameScreen::MainMenu),
                   "level select back to main does not restart without play");

    policy.RecordPuzzleEntered();
    context.Expect(!policy.OnScreenEntered(GameScreen::Paused) &&
                       !policy.OnScreenEntered(GameScreen::Solved) &&
                       !policy.OnScreenEntered(GameScreen::LevelSelect),
                   "pause, solved, and level-select transitions keep music scene-independent");
    context.Expect(policy.OnScreenEntered(GameScreen::MainMenu),
                   "returning to main after play restarts the intro once");
    context.Expect(!policy.OnScreenEntered(GameScreen::MainMenu),
                   "the consumed main-menu restart is not repeated");
}

} // namespace object_connect::tests
