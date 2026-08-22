#include "../TestSupport.hpp"

#include "RetroFPS/Rendering/MapGeometryGenerator.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseValidMap(
    TestContext& context, const std::string_view text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "valid rendering map should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error("valid rendering test map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

void TestSingleCellGeometry(TestContext& context) {
    const GridMap map = ParseValidMap(context, "P#D");
    const MapGeometry geometry = MapGeometryGenerator::Generate(map);

    std::size_t cellSurfaceCount = 0;
    std::size_t floorCount = 0;
    std::size_t wallCount = 0;
    for (const SurfaceInstance& surface : geometry.surfaces) {
        if (surface.row != 0 || surface.column != 0) {
            continue;
        }

        ++cellSurfaceCount;
        if (surface.type == SurfaceType::Floor) {
            ++floorCount;
            context.Expect(
                NearlyEqual(surface.transform.translation.x, 0.5f),
                "floor center X");
            context.Expect(
                NearlyEqual(surface.transform.translation.y, 0.0f),
                "floor is at ground Y");
            context.Expect(
                NearlyEqual(surface.transform.translation.z, 0.5f),
                "floor center Z");
            context.Expect(NearlyEqual(surface.normal.y, 1.0f), "floor normal points up");
            continue;
        }
        if (surface.type != SurfaceType::Wall) {
            continue;
        }

        ++wallCount;
        const float toCellX = 0.5f - surface.transform.translation.x;
        const float toCellZ = 0.5f - surface.transform.translation.z;
        const float inwardDot = surface.normal.x * toCellX + surface.normal.z * toCellZ;
        context.Expect(inwardDot > 0.49f, "wall normal points inward to walkable cell");
        context.Expect(
            NearlyEqual(surface.transform.translation.y, 1.25f),
            "wall is vertically centered");
        context.Expect(
            NearlyEqual(surface.transform.scale.y, 2.5f),
            "default wall height");

        const float yaw = surface.transform.rotationRadians.y;
        context.Expect(
            NearlyEqual(std::sin(yaw), surface.normal.x),
            "wall yaw rotates local +Z normal X");
        context.Expect(
            NearlyEqual(std::cos(yaw), surface.normal.z),
            "wall yaw rotates local +Z normal Z");
    }

    context.Expect(
        cellSurfaceCount == 5,
        "one isolated walkable cell emits one floor and four walls");
    context.Expect(floorCount == 1, "single-cell floor count");
    context.Expect(wallCount == 4, "single-cell wall count");
}

void TestSharedEdgesAndScaling(TestContext& context) {
    const MapGeometry adjacent =
        MapGeometryGenerator::Generate(ParseValidMap(context, "PD"));
    std::size_t floorCount = 0;
    std::size_t wallCount = 0;
    for (const SurfaceInstance& surface : adjacent.surfaces) {
        if (surface.type == SurfaceType::Floor) {
            ++floorCount;
        } else if (surface.type == SurfaceType::Wall) {
            ++wallCount;
        }
    }

    context.Expect(floorCount == 2, "each walkable cell emits a floor");
    context.Expect(wallCount == 6, "walkable-to-walkable edge emits no wall");

    const GridMap map = ParseValidMap(context, "P#D");
    const WorldSettings settings{2.0f, 3.0f};
    const MapGeometry scaled = MapGeometryGenerator::Generate(map, settings);
    context.Expect(
        NearlyEqual(scaled.surfaces.front().transform.translation.x, 1.0f),
        "custom cell size changes the floor center");
    context.Expect(
        NearlyEqual(scaled.surfaces.front().transform.scale.x, 2.0f),
        "custom cell size changes the floor scale");

    for (const SurfaceInstance& surface : scaled.surfaces) {
        if (surface.type == SurfaceType::Wall) {
            context.Expect(
                NearlyEqual(surface.transform.translation.y, 1.5f),
                "custom wall height changes wall center Y");
            context.Expect(
                NearlyEqual(surface.transform.scale.y, 3.0f),
                "custom wall height changes wall scale Y");
        }
    }
}

void TestDoorGeometry(TestContext& context) {
    const GridMap map = ParseValidMap(context, "PD");
    const WorldSettings settings{2.0f, 3.0f};
    const MapGeometry geometry = MapGeometryGenerator::Generate(map, settings);

    const SurfaceInstance* door = nullptr;
    bool hasExitFloor = false;
    std::size_t doorCount = 0;
    for (const SurfaceInstance& surface : geometry.surfaces) {
        if (surface.row == 0 && surface.column == 1 &&
            surface.type == SurfaceType::Floor) {
            hasExitFloor = true;
        }
        if (surface.type == SurfaceType::Door) {
            ++doorCount;
            door = &surface;
        }
    }

    context.Expect(hasExitFloor, "the next-map exit remains a walkable floor cell");
    context.Expect(doorCount == 1, "one next-map exit emits one door instance");
    if (door == nullptr) {
        return;
    }

    context.Expect(
        door->row == 0 && door->column == 1,
        "door identifies its next-map exit cell");
    context.Expect(
        NearlyEqual(door->transform.translation.x, 3.0f) &&
            NearlyEqual(door->transform.translation.z, 1.0f),
        "door is centered in its grid cell");
    context.Expect(
        NearlyEqual(door->transform.translation.y, 1.2f),
        "door rests on the floor at eighty percent wall height");
    context.Expect(
        NearlyEqual(door->transform.scale.x, 0.7f),
        "cube scale produces a door seventy percent of the cell width");
    context.Expect(
        NearlyEqual(door->transform.scale.y, 1.2f),
        "cube scale produces a door eighty percent of the wall height");
    context.Expect(
        NearlyEqual(door->transform.scale.z, 0.12f),
        "cube scale produces a door twelve percent of the cell depth");
}

void TestInvalidGeometrySettings(TestContext& context) {
    const GridMap map = ParseValidMap(context, "PD");
    const auto expectInvalid = [&context, &map](
                                   const WorldSettings settings,
                                   const std::string_view description) {
        context.ExpectThrows<std::invalid_argument>(
            [&map, settings] {
                static_cast<void>(MapGeometryGenerator::Generate(map, settings));
            },
            description);
    };

    expectInvalid({0.0f, 2.5f}, "geometry rejects zero cell size");
    expectInvalid(
        {(std::numeric_limits<float>::quiet_NaN)(), 2.5f},
        "geometry rejects non-finite cell size");
    expectInvalid({1.0f, -1.0f}, "geometry rejects negative wall height");
    expectInvalid(
        {1.0f, (std::numeric_limits<float>::infinity)()},
        "geometry rejects non-finite wall height");
}

} // namespace

void RunMapGeometryTests(TestContext& context) {
    TestSingleCellGeometry(context);
    TestSharedEdgesAndScaling(context);
    TestDoorGeometry(context);
    TestInvalidGeometrySettings(context);
}

} // namespace fps::tests
