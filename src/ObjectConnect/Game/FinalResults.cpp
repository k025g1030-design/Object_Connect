#include "ObjectConnect/Game/FinalResults.hpp"

#include <utility>

namespace object_connect {
namespace {

[[nodiscard]] bool IsCountedOrganType(const NodeType type) noexcept {
    return type == NodeType::Root || type == NodeType::Follow ||
           type == NodeType::End;
}

} // namespace

bool IsFinalResultsTarget(const PuzzleDefinition& puzzle) noexcept {
    return puzzle.nextLevelId.has_value() &&
           std::string_view{*puzzle.nextLevelId} == kFinalResultsTargetId;
}

std::size_t GetFinalResultsPageCount(const std::size_t stageCount) noexcept {
    if (stageCount == 0) {
        return 1;
    }
    return 1 + (stageCount - 1) / kFinalResultsStagesPerPage;
}

std::size_t ClampFinalResultsPage(const std::size_t requestedPage,
                                  const std::size_t stageCount) noexcept {
    const std::size_t lastPage = GetFinalResultsPageCount(stageCount) - 1;
    return requestedPage < lastPage ? requestedPage : lastPage;
}

std::size_t MoveFinalResultsPage(const std::size_t currentPage,
                                 const std::ptrdiff_t signedPageDelta,
                                 const std::size_t stageCount) noexcept {
    const std::size_t lastPage = GetFinalResultsPageCount(stageCount) - 1;
    const std::size_t clampedCurrent =
        currentPage < lastPage ? currentPage : lastPage;
    if (signedPageDelta >= 0) {
        const std::size_t forward =
            static_cast<std::size_t>(signedPageDelta);
        const std::size_t remaining = lastPage - clampedCurrent;
        return forward < remaining ? clampedCurrent + forward : lastPage;
    }

    const std::size_t backward =
        static_cast<std::size_t>(-(signedPageDelta + 1)) + 1;
    return backward < clampedCurrent ? clampedCurrent - backward : 0;
}

void RunCompletionTracker::Reset() noexcept { completedStages_.clear(); }

void RunCompletionTracker::RecordCompletedStage(
    const PuzzleDefinition& puzzle, const PuzzleBoardSnapshot& snapshot) {
    StageResultEntry stage{};
    stage.puzzleId = puzzle.id;
    stage.stageTitle = puzzle.title;

    for (std::size_t index = 0; index < puzzle.nodes.size(); ++index) {
        const NodeDefinition& node = puzzle.nodes[index];
        if (!node.HasPlacement() || !IsCountedOrganType(node.type)) {
            continue;
        }

        ++stage.totalOrganCount;
        if (index < snapshot.nodeStates.size() &&
            snapshot.nodeStates[index].active) {
            ++stage.connectedOrganCount;
        }
    }

    completedStages_.push_back(std::move(stage));
}

bool RunCompletionTracker::RemoveLastCompletedStage(
    const std::string_view puzzleId) noexcept {
    if (completedStages_.empty() || completedStages_.back().puzzleId != puzzleId) {
        return false;
    }
    completedStages_.pop_back();
    return true;
}

FinalResultsSummary RunCompletionTracker::BuildSummary() const {
    FinalResultsSummary summary{};
    summary.stages = completedStages_;
    return summary;
}

} // namespace object_connect
