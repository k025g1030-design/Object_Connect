#include "../TestSupport.hpp"

#include "RetroFPS/Game/GameConfig.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef RETROFPS_TEST_RESOURCE_ROOT
#error RETROFPS_TEST_RESOURCE_ROOT must identify the source resource directory.
#endif

namespace fps::tests {
namespace {

[[nodiscard]] GridMap LoadCampaignMap(
    TestContext& context, const std::filesystem::path& runtimePath) {
    const std::filesystem::path sourcePath =
        std::filesystem::path{RETROFPS_TEST_RESOURCE_ROOT} / "maps" / runtimePath.filename();
    MapLoadResult result = GridMapLoader::Load(sourcePath);
    context.Expect(result.Succeeded(), "configured campaign map should load from source resources");
    if (!result.map.has_value()) {
        throw std::runtime_error(
            "campaign map failed to load: " + sourcePath.generic_string() + ": " + result.error);
    }
    return std::move(*result.map);
}

[[nodiscard]] bool IsReachable(
    const GridMap& map,
    const GridCoordinate start,
    const GridCoordinate target) {
    const std::size_t width = map.GetWidth();
    const std::size_t height = map.GetHeight();
    std::vector<bool> visited(width * height, false);
    std::queue<GridCoordinate> pending;
    visited[start.row * width + start.column] = true;
    pending.push(start);

    constexpr std::array<std::pair<std::ptrdiff_t, std::ptrdiff_t>, 4> offsets = {{
        {-1, 0},
        {1, 0},
        {0, -1},
        {0, 1},
    }};

    while (!pending.empty()) {
        const GridCoordinate current = pending.front();
        pending.pop();
        if (current == target) {
            return true;
        }

        for (const auto [rowOffset, columnOffset] : offsets) {
            const std::ptrdiff_t row =
                static_cast<std::ptrdiff_t>(current.row) + rowOffset;
            const std::ptrdiff_t column =
                static_cast<std::ptrdiff_t>(current.column) + columnOffset;
            if (!map.IsWalkable(row, column)) {
                continue;
            }

            const auto nextRow = static_cast<std::size_t>(row);
            const auto nextColumn = static_cast<std::size_t>(column);
            const std::size_t index = nextRow * width + nextColumn;
            if (visited[index]) {
                continue;
            }
            visited[index] = true;
            pending.push({nextRow, nextColumn});
        }
    }
    return false;
}

void TestCampaignResources(TestContext& context) {
    const GameConfig config{};
    context.Expect(config.mapPaths.size() == 2, "default campaign contains two maps");

    for (const std::filesystem::path& mapPath : config.mapPaths) {
        const GridMap map = LoadCampaignMap(context, mapPath);
        const GridCoordinate playerSpawn = map.GetPlayerSpawnCell();
        context.Expect(
            IsReachable(map, playerSpawn, map.GetNextMapExitCell()),
            "campaign exit should be reachable from player spawn");
        context.Expect(
            !map.GetMonsterSpawnCells().empty(),
            "sample campaign map should demonstrate monster spawn data");
        for (const GridCoordinate monsterSpawn : map.GetMonsterSpawnCells()) {
            context.Expect(
                IsReachable(map, playerSpawn, monsterSpawn),
                "monster spawn should be reachable from player spawn");
        }
    }
}

} // namespace

void RunCampaignResourceTests(TestContext& context) {
    TestCampaignResources(context);
}

} // namespace fps::tests
