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
    const GridMap map = ParseValidMap(context, "P");
    const MapGeometry geometry = MapGeometryGenerator::Generate(map);
    context.Expect(
        geometry.surfaces.size() == 5,
        "one walkable cell emits one floor and four walls");

    std::size_t floorCount = 0;
    std::size_t wallCount = 0;
    for (const SurfaceInstance& surface : geometry.surfaces) {
        context.Expect(
            surface.row == 0 && surface.column == 0,
            "surface identifies its walkable cell");
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

    context.Expect(floorCount == 1, "single-cell floor count");
    context.Expect(wallCount == 4, "single-cell wall count");
}

void TestSharedEdgesAndScaling(TestContext& context) {
    const MapGeometry adjacent =
        MapGeometryGenerator::Generate(ParseValidMap(context, "P."));
    std::size_t floorCount = 0;
    std::size_t wallCount = 0;
    for (const SurfaceInstance& surface : adjacent.surfaces) {
        if (surface.type == SurfaceType::Floor) {
            ++floorCount;
        } else {
            ++wallCount;
        }
    }

    context.Expect(floorCount == 2, "each walkable cell emits a floor");
    context.Expect(wallCount == 6, "walkable-to-walkable edge emits no wall");

    const GridMap map = ParseValidMap(context, "P");
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

void TestInvalidGeometrySettings(TestContext& context) {
    const GridMap map = ParseValidMap(context, "P");
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
    TestInvalidGeometrySettings(context);
}

} // namespace fps::tests
