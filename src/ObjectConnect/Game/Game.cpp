#include "ObjectConnect/Game/Game.hpp"

#include "ObjectConnect/Data/PuzzleCatalogLoader.hpp"
#include "ObjectConnect/Game/GameFlow.hpp"
#include "ObjectConnect/Input/InputState.hpp"
#include "ObjectConnect/Input/InputSystem.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"
#include "ObjectConnect/Rendering/GameUiRenderer.hpp"
#include "ObjectConnect/Rendering/PuzzleRenderer.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace object_connect {
namespace {

constexpr float kSolvedMenuDelaySeconds = 0.6f;
constexpr std::size_t kRendererVertexCapacity = 65536;
constexpr char kRuntimeResourceRoot[] = "Resources";

[[nodiscard]] float NormalizeDelta(const float deltaSeconds) noexcept {
    return std::isfinite(deltaSeconds) && deltaSeconds > 0.0f ? deltaSeconds : 0.0f;
}

} // namespace

struct Game::Impl final {
    PuzzleCatalog catalog;
    InputSystem input;
    PuzzleRenderer puzzleRenderer;
    GameUiRenderer ui;
    GameFlow flow;
    std::unique_ptr<PuzzleBoard> board;
    std::optional<std::size_t> currentPuzzleIndex;
    bool shouldQuit = false;
    float elapsedSeconds = 0.0f;
    float solvedElapsedSeconds = 0.0f;

    [[nodiscard]] bool IsCurrentPuzzleLast() const noexcept {
        return currentPuzzleIndex.has_value() &&
               *currentPuzzleIndex + 1 >= catalog.GetPuzzles().size();
    }

    [[nodiscard]] bool StartPuzzle(const std::size_t index, std::string& error) {
        error.clear();
        if (index >= catalog.GetPuzzles().size()) {
            error = "The selected puzzle index is out of range.";
            return false;
        }
        auto nextBoard = std::make_unique<PuzzleBoard>();
        if (!nextBoard->Initialize(catalog.GetPuzzles()[index], error)) {
            return false;
        }
        board = std::move(nextBoard);
        currentPuzzleIndex = index;
        solvedElapsedSeconds = 0.0f;
        flow.EnterPlaying();
        return true;
    }

    void RequireStartPuzzle(const std::size_t index) {
        std::string error;
        if (!StartPuzzle(index, error)) {
            throw std::runtime_error(error);
        }
    }

    void LeavePuzzleForLevelSelect() noexcept {
        board.reset();
        currentPuzzleIndex.reset();
        solvedElapsedSeconds = 0.0f;
        flow.EnterLevelSelect();
    }

    void LeavePuzzleForMainMenu() noexcept {
        board.reset();
        currentPuzzleIndex.reset();
        solvedElapsedSeconds = 0.0f;
        flow.ReturnToMainMenu();
    }

    void ApplyCommand(const GameFlowResult& result) {
        switch (result.command) {
        case GameCommand::None:
            break;
        case GameCommand::OpenLevelSelect:
            LeavePuzzleForLevelSelect();
            break;
        case GameCommand::StartPuzzle:
            if (!result.puzzleIndex.has_value()) {
                throw std::runtime_error("GameFlow omitted the selected puzzle index.");
            }
            RequireStartPuzzle(*result.puzzleIndex);
            break;
        case GameCommand::RetryPuzzle:
            if (!currentPuzzleIndex.has_value()) {
                throw std::runtime_error("Retry was requested without an active puzzle.");
            }
            RequireStartPuzzle(*currentPuzzleIndex);
            break;
        case GameCommand::NextPuzzle:
            if (!currentPuzzleIndex.has_value()) {
                throw std::runtime_error("Next puzzle was requested without an active puzzle.");
            }
            if (*currentPuzzleIndex + 1 < catalog.GetPuzzles().size()) {
                RequireStartPuzzle(*currentPuzzleIndex + 1);
            } else {
                LeavePuzzleForLevelSelect();
            }
            break;
        case GameCommand::ReturnToMainMenu:
            LeavePuzzleForMainMenu();
            break;
        case GameCommand::QuitGame:
            shouldQuit = true;
            break;
        }
    }

    void Update(const float unnormalizedDeltaSeconds) {
        const float deltaSeconds = NormalizeDelta(unnormalizedDeltaSeconds);
        elapsedSeconds += deltaSeconds;
        const InputState state = input.Sample();
        const GameScreen screenBeforeInput = flow.GetScreen();

        if (board && (screenBeforeInput == GameScreen::Playing ||
                      screenBeforeInput == GameScreen::Paused) &&
            state.keyboard.retryPressed) {
            RequireStartPuzzle(*currentPuzzleIndex);
            return;
        }

        if (board && screenBeforeInput == GameScreen::Playing &&
            (state.keyboard.escapePressed || state.focusLost)) {
            board->CancelDrag(true);
        }

        const bool solvedMenuReady = solvedElapsedSeconds >= kSolvedMenuDelaySeconds;
        GameFlowInput flowInput{};
        flowInput.previousPressed = state.keyboard.previousPressed;
        flowInput.nextPressed = state.keyboard.nextPressed;
        flowInput.confirmPressed = state.keyboard.enterPressed;
        flowInput.escapePressed = state.keyboard.escapePressed;
        flowInput.focusLost = state.focusLost;
        flowInput.mouseMoved = state.mouse.moved;
        flowInput.mousePrimaryPressed = state.mouse.leftPressed;
        flowInput.hoveredItem = ui.HitTest(
            screenBeforeInput, {state.mouse.positionX, state.mouse.positionY},
            catalog.GetPuzzles().size(), IsCurrentPuzzleLast(), solvedMenuReady,
            flow.GetSelectedItem());

        if (screenBeforeInput == GameScreen::Solved && !solvedMenuReady) {
            flowInput = {};
        }

        const GameFlowResult flowResult = flow.Update(
            flowInput, catalog.GetPuzzles().size(), IsCurrentPuzzleLast());
        ApplyCommand(flowResult);

        if (flowResult.simulatePuzzle && board && flow.GetScreen() == GameScreen::Playing) {
            BoardPointerInput boardInput{};
            boardInput.position = {state.mouse.positionX, state.mouse.positionY};
            boardInput.leftPressed = state.mouse.leftPressed;
            boardInput.leftHeld = state.mouse.leftHeld;
            boardInput.leftReleased = state.mouse.leftReleased;
            board->Update(boardInput, deltaSeconds);
            if (board->IsSolved()) {
                flow.EnterSolved();
                solvedElapsedSeconds = 0.0f;
            }
        } else if (board && flow.GetScreen() == GameScreen::Solved) {
            board->Update({}, deltaSeconds);
        }

        if (flow.GetScreen() == GameScreen::Solved) {
            solvedElapsedSeconds += deltaSeconds;
        } else if (screenBeforeInput != GameScreen::Solved) {
            solvedElapsedSeconds = 0.0f;
        }
    }

    void Draw() {
        std::optional<PuzzleBoardSnapshot> snapshot;
        if (board) {
            snapshot = board->MakeSnapshot();
            puzzleRenderer.Draw(board->GetDefinition(), *snapshot, elapsedSeconds);
        }
        ui.Draw(flow.GetScreen(), flow.GetSelectedItem(), catalog,
                currentPuzzleIndex, snapshot ? &*snapshot : nullptr,
                solvedElapsedSeconds >= kSolvedMenuDelaySeconds);
    }
};

Game::Game() noexcept = default;
Game::~Game() { Finalize(); }

bool Game::Initialize(std::string& error) { return Initialize(GameConfig{}, error); }

bool Game::Initialize(const GameConfig& config, std::string& error) {
    Finalize();
    error.clear();
    auto next = std::make_unique<Impl>();
    if (!PuzzleCatalogLoader::Load(config.catalogPath, kRuntimeResourceRoot,
                                   next->catalog, error)) {
        return false;
    }
    if (next->catalog.Empty()) {
        error = "Puzzle data must contain at least one puzzle.";
        return false;
    }
    if (!next->input.Initialize(error)) {
        return false;
    }
    if (!next->puzzleRenderer.Initialize(kRendererVertexCapacity,
                                         next->catalog.GetTileset(), error)) {
        return false;
    }
    if (!next->ui.Initialize(error)) {
        return false;
    }
    impl_ = std::move(next);
    return true;
}

void Game::Update(const float deltaSeconds) {
    if (impl_) {
        impl_->Update(deltaSeconds);
    }
}

void Game::Draw() {
    if (impl_) {
        impl_->Draw();
    }
}

void Game::Finalize() noexcept {
    if (impl_) {
        impl_->ui.Finalize();
        impl_->puzzleRenderer.Finalize();
        impl_->input.Finalize();
    }
    impl_.reset();
}

bool Game::ShouldQuit() const noexcept { return impl_ && impl_->shouldQuit; }
bool Game::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace object_connect
