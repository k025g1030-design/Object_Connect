#include "../TestSupport.hpp"

#include "RetroFPS/World/GridMapLoader.hpp"
#include "RetroFPS/World/World.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseValidMap(
    TestContext& context, const std::string_view text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "valid map text should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error("valid test map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

void ExpectParseError(
    TestContext& context,
    const std::string_view text,
    const std::size_t expectedLine,
    const std::size_t expectedColumn) {
    const MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(!result.Succeeded(), "invalid map should fail through GridMapLoader");
    context.Expect(!result.map.has_value(), "invalid map result should not contain map data");

    const std::string prefix = "line " + std::to_string(expectedLine) + ", column " +
                               std::to_string(expectedColumn) + ":";
    context.Expect(
        result.error.starts_with(prefix),
        "parse error line and column should be one-based and exact");
}

void TestGridMapParsing(TestContext& context) {
    const GridMap map = ParseValidMap(context, "#######\n#PE.ED#\n#######");
    context.Expect(map.GetWidth() == 7, "map width");
    context.Expect(map.GetHeight() == 3, "map height");
    context.Expect(
        map.GetPlayerSpawnCell() == GridCoordinate{1, 1},
        "player spawn coordinate");
    context.Expect(
        map.GetSpawnCell() == map.GetPlayerSpawnCell(),
        "legacy spawn-cell accessor uses the player spawn");
    context.Expect(
        map.GetTile(1, 1) == TileType::PlayerSpawn,
        "player spawn remains typed map data");
    context.Expect(map.GetTile(1, 2) == TileType::MonsterSpawn, "first monster tile");
    context.Expect(map.GetTile(1, 3) == TileType::Floor, "floor tile");
    context.Expect(map.GetTile(1, 5) == TileType::NextMapExit, "next-map exit tile");
    context.Expect(
        map.GetMonsterSpawnCells() ==
            std::vector<GridCoordinate>{{1, 2}, {1, 4}},
        "all monster spawns preserve map order");
    context.Expect(
        map.GetNextMapExitCell() == GridCoordinate{1, 5},
        "next-map exit coordinate");
    context.Expect(map.IsWalkable(1, 1), "spawn is walkable");
    context.Expect(map.IsWalkable(1, 2), "monster spawn is walkable");
    context.Expect(map.IsWalkable(1, 3), "floor is walkable");
    context.Expect(map.IsWalkable(1, 5), "next-map exit is walkable");
    context.Expect(map.IsSolid(0, 0), "hash is solid");
    context.Expect(map.IsSolid(-1, 1), "negative row is solid");
    context.Expect(map.IsSolid(1, 7), "out-of-bounds column is solid");

    const Float2 spawnPosition = map.GetSpawnPosition(2.0f);
    context.Expect(NearlyEqual(spawnPosition.x, 3.0f), "columns map to +X cell centers");
    context.Expect(NearlyEqual(spawnPosition.z, 3.0f), "rows map to +Z cell centers");
    const Float2 exitPosition = map.GetCellCenter(map.GetNextMapExitCell(), 2.0f);
    context.Expect(NearlyEqual(exitPosition.x, 11.0f), "exit column maps to +X center");
    context.Expect(NearlyEqual(exitPosition.z, 3.0f), "exit row maps to +Z center");

    const std::optional<GridCoordinate> spawnCoordinate =
        map.TryGetCoordinateAtPosition(spawnPosition, 2.0f);
    context.Expect(
        spawnCoordinate == GridCoordinate{1, 1},
        "world position resolves to its containing grid coordinate");
    context.Expect(
        map.TryGetCoordinateAtPosition({4.0f, 2.0f}, 2.0f) == GridCoordinate{1, 2},
        "cell-boundary positions resolve to the cell on the positive side");
    context.Expect(
        !map.TryGetCoordinateAtPosition({-0.01f, 1.0f}).has_value(),
        "negative world position is outside the map");
    context.Expect(
        !map.TryGetCoordinateAtPosition({14.0f, 1.0f}, 2.0f).has_value(),
        "position on the outer maximum boundary is outside the map");
    context.Expect(
        !map.TryGetCoordinateAtPosition(
                {(std::numeric_limits<float>::quiet_NaN)(), 1.0f})
             .has_value(),
        "non-finite world position does not resolve to a cell");

    const GridMap crlfMap = ParseValidMap(context, "####\r\n#PD#\r\n####\r\n");
    context.Expect(
        crlfMap.GetWidth() == 4 && crlfMap.GetHeight() == 3,
        "CRLF map parsing");

    context.ExpectThrows<std::out_of_range>(
        [&map] { static_cast<void>(map.GetTile(3, 0)); },
        "GetTile should reject an out-of-range row");
    context.ExpectThrows<std::out_of_range>(
        [&map] { static_cast<void>(map.GetCellCenter({3, 0})); },
        "cell-center conversion should reject an out-of-range coordinate");
    context.ExpectThrows<std::invalid_argument>(
        [&map] { static_cast<void>(map.GetSpawnPosition(0.0f)); },
        "spawn conversion should reject a non-positive cell size");
    context.ExpectThrows<std::invalid_argument>(
        [&map] {
            static_cast<void>(map.TryGetCoordinateAtPosition({1.0f, 1.0f}, 0.0f));
        },
        "position conversion should reject a non-positive cell size");

    ExpectParseError(context, "", 1, 1);
    ExpectParseError(context, "P.\n#", 2, 2);
    ExpectParseError(context, "P@", 1, 2);
    ExpectParseError(context, "..", 1, 1);
    ExpectParseError(context, "PDP", 1, 3);
    ExpectParseError(context, "PDD", 1, 3);
    ExpectParseError(context, "P.", 1, 1);
    ExpectParseError(context, "P\r", 1, 2);
}

void TestGridMapLoading(TestContext& context) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "ObjectFPS_CoreTests_map.txt";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        context.Expect(file.is_open(), "temporary map file should open");
        file << "####\r\n#PD#\r\n####\r\n";
    }

    const MapLoadResult loaded = GridMapLoader::Load(path);
    context.Expect(loaded.Succeeded(), "GridMapLoader reports successful file load");
    context.Expect(static_cast<bool>(loaded), "MapLoadResult boolean conversion reports success");
    context.Expect(loaded.map.has_value(), "successful MapLoadResult contains a map");
    context.Expect(loaded.error.empty(), "successful MapLoadResult has no error");
    if (loaded.map.has_value()) {
        context.Expect(
            loaded.map->GetSpawnCell().row == 1 && loaded.map->GetSpawnCell().column == 1,
            "GridMapLoader::Load parses the file contents");
    }

    const MapLoadResult invalid = GridMapLoader::Parse("..\n..");
    context.Expect(!invalid.Succeeded(), "GridMapLoader catches parse errors");
    context.Expect(!invalid.map.has_value(), "failed MapLoadResult has no map");
    context.Expect(!invalid.error.empty(), "failed MapLoadResult contains an error message");

    std::error_code removeError;
    static_cast<void>(std::filesystem::remove(path, removeError));
    context.Expect(!removeError, "temporary map cleanup");

    const MapLoadResult missing = GridMapLoader::Load(path);
    context.Expect(!missing.Succeeded(), "GridMapLoader catches missing-file errors");
    context.Expect(!missing.error.empty(), "missing-file result contains an error message");
}

void TestWorldOwnership(TestContext& context) {
    World world;
    context.Expect(!world.IsInitialized(), "default world is not initialized");
    context.ExpectThrows<std::logic_error>(
        [&world] { static_cast<void>(world.GetMap()); },
        "uninitialized world has no map");
    context.ExpectThrows<std::logic_error>(
        [&world] { static_cast<void>(world.GetSettings()); },
        "uninitialized world has no settings");

    const WorldSettings settings{2.0f, 3.0f};
    {
        GridMap map = ParseValidMap(context, "PD");
        world.Initialize(std::move(map), settings);
    }

    context.Expect(world.IsInitialized(), "Initialize gives World owned state");
    context.Expect(world.GetMap().GetWidth() == 2, "World owns its GridMap after source lifetime");
    context.Expect(
        NearlyEqual(world.GetSettings().cellSize, 2.0f),
        "World owns the configured cell size");
    context.Expect(
        NearlyEqual(world.GetSettings().wallHeight, 3.0f),
        "World owns the configured wall height");

    const WorldSettings invalidSettings{0.0f, 3.0f};
    context.ExpectThrows<std::invalid_argument>(
        [&context, &world, invalidSettings] {
            world.Initialize(ParseValidMap(context, "PD"), invalidSettings);
        },
        "World rejects an invalid replacement cell size");
    context.Expect(
        world.IsInitialized() && world.GetMap().GetWidth() == 2,
        "failed World initialization preserves the previous owned state");

    world.Reset();
    context.Expect(!world.IsInitialized(), "Reset clears World ownership");
    context.ExpectThrows<std::logic_error>(
        [&world] { static_cast<void>(world.GetMap()); },
        "reset world no longer exposes map data");
}

void TestInvalidWorldSettings(TestContext& context) {
    const auto expectInvalid = [&context](
                                   const WorldSettings settings,
                                   const std::string_view description) {
        context.ExpectThrows<std::invalid_argument>(
            [&context, settings] {
                World world(ParseValidMap(context, "PD"), settings);
                static_cast<void>(world);
            },
            description);
    };

    expectInvalid({0.0f, 2.5f}, "World rejects zero cell size");
    expectInvalid({-1.0f, 2.5f}, "World rejects negative cell size");
    expectInvalid(
        {(std::numeric_limits<float>::infinity)(), 2.5f},
        "World rejects non-finite cell size");
    expectInvalid({1.0f, 0.0f}, "World rejects zero wall height");
    expectInvalid(
        {1.0f, (std::numeric_limits<float>::quiet_NaN)()},
        "World rejects non-finite wall height");
}

} // namespace

void RunWorldTests(TestContext& context) {
    TestGridMapParsing(context);
    TestGridMapLoading(context);
    TestWorldOwnership(context);
    TestInvalidWorldSettings(context);
}

} // namespace fps::tests
