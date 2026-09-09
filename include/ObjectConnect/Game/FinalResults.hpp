#pragma once

#include "ObjectConnect/Data/PuzzleData.hpp"
#include "ObjectConnect/Puzzle/PuzzleBoard.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace object_connect {

[[nodiscard]] bool IsFinalResultsTarget(
    const PuzzleDefinition& puzzle) noexcept;

inline constexpr std::size_t kFinalResultsStagesPerPage = 10;

[[nodiscard]] std::size_t GetFinalResultsPageCount(
    std::size_t stageCount) noexcept;
[[nodiscard]] std::size_t ClampFinalResultsPage(
    std::size_t requestedPage, std::size_t stageCount) noexcept;
// Negative deltas move toward the first page; positive deltas move toward the
// last page. Movement saturates at either end.
[[nodiscard]] std::size_t MoveFinalResultsPage(
    std::size_t currentPage, std::ptrdiff_t signedPageDelta,
    std::size_t stageCount) noexcept;

struct StageResultEntry final {
    std::string puzzleId;
    std::string stageTitle;
    std::size_t connectedOrganCount = 0;
    std::size_t totalOrganCount = 0;
};

struct FinalResultsSummary final {
    std::vector<StageResultEntry> stages;
};

class RunCompletionTracker final {
public:
    void Reset() noexcept;
    void RecordCompletedStage(const PuzzleDefinition& puzzle,
                              const PuzzleBoardSnapshot& snapshot);
    [[nodiscard]] bool RemoveLastCompletedStage(
        std::string_view puzzleId) noexcept;
    [[nodiscard]] FinalResultsSummary BuildSummary() const;

private:
    std::vector<StageResultEntry> completedStages_;
};

} // namespace object_connect
