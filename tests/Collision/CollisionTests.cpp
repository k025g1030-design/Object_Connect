#include "../TestSupport.hpp"

#include "RetroFPS/Collision/GridCollision.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fps::tests {
namespace {

[[nodiscard]] GridMap ParseValidMap(
    TestContext& context, const std::string_view text) {
    MapLoadResult result = GridMapLoader::Parse(text);
    context.Expect(result.Succeeded(), "valid collision map should parse");
    if (!result.map.has_value()) {
        throw std::runtime_error("valid collision test map failed to parse: " + result.error);
    }
    return std::move(*result.map);
}

void TestCircleGridQueries(TestContext& context) {
    const GridMap singleCell = ParseValidMap(context, "P");
    context.Expect(
        !GridCollision::OverlapsSolid(singleCell, {0.5f, 0.5f}, 0.49f),
        "circle inside a walkable cell is clear");
    context.Expect(
        !GridCollision::OverlapsSolid(singleCell, {0.5f, 0.5f}, 0.5f),
        "exact circle-to-wall contact is not penetration");
    context.Expect(
        GridCollision::OverlapsSolid(singleCell, {0.5f, 0.5f}, 0.51f),
        "out-of-bounds space is solid");

    const GridMap cornerMap = ParseValidMap(
        context,
        "####\n"
        "#P.#\n"
        "#.##\n"
        "####");
    context.Expect(
        GridCollision::OverlapsSolid(cornerMap, {1.85f, 1.85f}, 0.25f),
        "nearest-point test detects a circle against a solid-cell corner");
}

void TestMovementResolution(TestContext& context) {
    const GridMap singleCell = ParseValidMap(context, "P");
    const Float2 bounded =
        GridCollision::MoveCircle(singleCell, {0.5f, 0.5f}, {10.0f, 0.0f}, 0.2f);
    context.Expect(bounded.x <= 0.8001f, "substeps prevent tunneling through outer wall");
    context.Expect(
        !GridCollision::OverlapsSolid(singleCell, bounded, 0.2f),
        "bounded move result is clear");

    const GridMap wallMap = ParseValidMap(
        context,
        "#####\n"
        "#P#.#\n"
        "#.#.#\n"
        "#...#\n"
        "#####");
    const Float2 slide =
        GridCollision::MoveCircle(wallMap, {1.5f, 1.5f}, {2.0f, 1.0f}, 0.2f);
    context.Expect(slide.x <= 1.8001f, "solid cell blocks X movement");
    context.Expect(slide.z > 2.0f, "Z movement continues to slide along wall");
    context.Expect(
        !GridCollision::OverlapsSolid(wallMap, slide, 0.2f),
        "sliding result is clear");

    const GridMap sealedCornerMap = ParseValidMap(
        context,
        "#####\n"
        "#P#.#\n"
        "##..#\n"
        "#...#\n"
        "#####");
    const Float2 cornerBlocked = GridCollision::MoveCircle(
        sealedCornerMap, {1.5f, 1.5f}, {2.0f, 2.0f}, 0.2f);
    context.Expect(
        cornerBlocked.x <= 1.8001f && cornerBlocked.z <= 1.8001f,
        "substeps cannot pass diagonally through a sealed grid corner");
    context.Expect(
        !GridCollision::OverlapsSolid(sealedCornerMap, cornerBlocked, 0.2f),
        "corner-resolved movement remains outside solid cells");
}

void TestInvalidCollisionArguments(TestContext& context) {
    const GridMap map = ParseValidMap(context, "P");
    const float infinity = (std::numeric_limits<float>::infinity)();
    const float nan = (std::numeric_limits<float>::quiet_NaN)();

    context.ExpectThrows<std::invalid_argument>(
        [&map, infinity] {
            static_cast<void>(GridCollision::OverlapsSolid(map, {infinity, 0.5f}, 0.2f));
        },
        "collision rejects a non-finite center");
    context.ExpectThrows<std::invalid_argument>(
        [&map] { static_cast<void>(GridCollision::OverlapsSolid(map, {0.5f, 0.5f}, 0.0f)); },
        "collision rejects a non-positive radius");
    context.ExpectThrows<std::invalid_argument>(
        [&map] {
            static_cast<void>(
                GridCollision::OverlapsSolid(map, {0.5f, 0.5f}, 0.2f, 0.0f));
        },
        "collision rejects a non-positive cell size");
    context.ExpectThrows<std::invalid_argument>(
        [&map, nan] {
            static_cast<void>(
                GridCollision::MoveCircle(map, {0.5f, 0.5f}, {nan, 0.0f}, 0.2f));
        },
        "movement rejects a non-finite displacement");
}

void TestExtremeFiniteValues(TestContext& context) {
    const float tinyRadius = (std::numeric_limits<float>::denorm_min)();
    const GridMap map = ParseValidMap(context, "P#");

    context.Expect(
        GridCollision::OverlapsSolid(map, {1.5f, 0.5f}, tinyRadius),
        "a denormal-sized circle centered in a solid cell still overlaps");

    const Float2 unchanged =
        GridCollision::MoveCircle(map, {0.5f, 0.5f}, {0.0f, 0.0f}, tinyRadius);
    context.Expect(
        unchanged.x == 0.5f && unchanged.z == 0.5f,
        "zero displacement with a denormal-sized radius is stable");

    context.Expect(
        GridCollision::OverlapsSolid(
            map,
            {(std::numeric_limits<float>::max)(), 0.5f},
            0.2f),
        "finite coordinates outside the representable grid are treated as solid");
}

} // namespace

void RunCollisionTests(TestContext& context) {
    TestCircleGridQueries(context);
    TestMovementResolution(context);
    TestInvalidCollisionArguments(context);
    TestExtremeFiniteValues(context);
}

} // namespace fps::tests
