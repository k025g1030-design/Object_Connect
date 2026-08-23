#include "../TestSupport.hpp"

#include "RetroFPS/Collision/GridCollision.hpp"
#include "RetroFPS/World/GridMapLoader.hpp"

#include <array>
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
    const GridMap singleCell = ParseValidMap(
        context,
        "#####\n"
        "#P#D#\n"
        "#####");
    context.Expect(
        !GridCollision::OverlapsSolid(singleCell, {1.5f, 1.5f}, 0.49f),
        "circle inside a walkable cell is clear");
    context.Expect(
        !GridCollision::OverlapsSolid(singleCell, {1.5f, 1.5f}, 0.5f),
        "exact circle-to-wall contact is not penetration");
    context.Expect(
        GridCollision::OverlapsSolid(singleCell, {1.5f, 1.5f}, 0.51f),
        "solid cells surrounding a walkable cell block penetration");

    const GridMap markerMap = ParseValidMap(context, "P.MD");
    context.Expect(
        !GridCollision::OverlapsSolid(markerMap, {0.5f, 0.5f}, 0.2f),
        "player spawn is non-solid");
    context.Expect(
        !GridCollision::OverlapsSolid(markerMap, {2.5f, 0.5f}, 0.2f),
        "enemy spawn is non-solid");
    context.Expect(
        !GridCollision::OverlapsSolid(markerMap, {3.5f, 0.5f}, 0.2f),
        "next-map exit is non-solid");

    const GridMap cornerMap = ParseValidMap(
        context,
        "####\n"
        "#P.#\n"
        "#D##\n"
        "####");
    context.Expect(
        GridCollision::OverlapsSolid(cornerMap, {1.85f, 1.85f}, 0.25f),
        "nearest-point test detects a circle against a solid-cell corner");
}

void TestCircleObstacleQueries(TestContext& context) {
    const CircleObstacle obstacle{{2.0f, 1.5f}, 0.3f};
    context.Expect(
        !GridCollision::OverlapsCircle({1.5f, 1.5f}, 0.2f, obstacle),
        "exactly tangent circles do not overlap");
    context.Expect(
        GridCollision::OverlapsCircle({1.5001f, 1.5f}, 0.2f, obstacle),
        "circle penetration is detected");
    context.Expect(
        !GridCollision::OverlapsCircle({1.4f, 1.5f}, 0.2f, obstacle),
        "separated circles do not overlap");
}

void TestMovementResolution(TestContext& context) {
    const GridMap singleCell = ParseValidMap(
        context,
        "#####\n"
        "#P#D#\n"
        "#####");
    const Float2 bounded =
        GridCollision::MoveCircle(singleCell, {1.5f, 1.5f}, {10.0f, 0.0f}, 0.2f);
    context.Expect(bounded.x <= 1.8001f, "substeps prevent tunneling through a wall");
    context.Expect(
        !GridCollision::OverlapsSolid(singleCell, bounded, 0.2f),
        "bounded move result is clear");

    const GridMap wallMap = ParseValidMap(
        context,
        "#####\n"
        "#P#D#\n"
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
        "#P#D#\n"
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

void TestDynamicCircleMovement(TestContext& context) {
    const GridMap openMap = ParseValidMap(
        context,
        "###########\n"
        "#P.......D#\n"
        "#.........#\n"
        "#.........#\n"
        "###########");
    const std::array blockers{
        CircleObstacle{{4.5f, 2.5f}, 0.3f},
        CircleObstacle{{7.0f, 2.5f}, 0.4f},
    };

    const Float2 highSpeed = GridCollision::MoveCircle(
        openMap, {1.5f, 2.5f}, {8.0f, 0.0f}, 0.2f, blockers);
    context.Expect(
        NearlyEqual(highSpeed.x, 4.0f, 0.0002f),
        "swept circle stops at the nearest blocker without tunneling");
    context.Expect(
        !GridCollision::OverlapsCircle(highSpeed, 0.2f, blockers.front()),
        "swept circle result does not penetrate the blocker");

    const std::array tangentBlocker{
        CircleObstacle{{4.5f, 2.5f}, 0.3f},
    };
    const Float2 inward = GridCollision::MoveCircle(
        openMap, {4.0f, 2.5f}, {1.0f, 0.0f}, 0.2f, tangentBlocker);
    context.Expect(
        NearlyEqual(inward.x, 4.0f) && NearlyEqual(inward.z, 2.5f),
        "movement into a tangent blocker is rejected");

    const Float2 outward = GridCollision::MoveCircle(
        openMap, {4.0f, 2.5f}, {-1.0f, 0.0f}, 0.2f, tangentBlocker);
    context.Expect(
        NearlyEqual(outward.x, 3.0f) && NearlyEqual(outward.z, 2.5f),
        "movement away from a tangent blocker is allowed");

    const Float2 tangential = GridCollision::MoveCircle(
        openMap, {4.0f, 2.5f}, {0.0f, 1.0f}, 0.2f, tangentBlocker);
    context.Expect(
        NearlyEqual(tangential.x, 4.0f) && NearlyEqual(tangential.z, 3.5f),
        "movement tangent to a circle is allowed");

    const Float2 slide = GridCollision::MoveCircle(
        openMap, {3.5f, 2.5f}, {2.0f, 1.0f}, 0.2f, tangentBlocker);
    context.Expect(
        slide.x > 4.0f && slide.z > 3.0f,
        "axis resolution slides around a circular blocker");
    context.Expect(
        !GridCollision::OverlapsCircle(slide, 0.2f, tangentBlocker.front()),
        "circular sliding remains outside the blocker");

    context.ExpectThrows<std::invalid_argument>(
        [&openMap, &tangentBlocker] {
            static_cast<void>(GridCollision::MoveCircle(
                openMap,
                {4.25f, 2.5f},
                {-1.0f, 0.0f},
                0.2f,
                tangentBlocker));
        },
        "movement rejects a start that overlaps a dynamic blocker");
}

void TestWallsAndDynamicCircles(TestContext& context) {
    const GridMap map = ParseValidMap(
        context,
        "#######\n"
        "#P#..D#\n"
        "#.#...#\n"
        "#.....#\n"
        "#######");
    const std::array blocker{
        CircleObstacle{{1.5f, 2.5f}, 0.5f},
    };
    const Float2 blocked = GridCollision::MoveCircle(
        map, {1.5f, 1.5f}, {2.0f, 2.0f}, 0.2f, blocker);
    context.Expect(
        blocked.x <= 1.8001f && blocked.z < 2.0f,
        "a wall and a circle can jointly block both movement axes");
    context.Expect(
        !GridCollision::OverlapsSolid(map, blocked, 0.2f),
        "joint wall and circle resolution remains outside the wall");
    context.Expect(
        !GridCollision::OverlapsCircle(blocked, 0.2f, blocker.front()),
        "joint wall and circle resolution remains outside the circle");
}

void TestInvalidCollisionArguments(TestContext& context) {
    const GridMap map = ParseValidMap(context, "PD");
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

    context.ExpectThrows<std::invalid_argument>(
        [nan] {
            static_cast<void>(GridCollision::OverlapsCircle(
                {0.5f, 0.5f}, 0.2f, {{nan, 0.5f}, 0.2f}));
        },
        "circle query rejects a non-finite obstacle center");
    context.ExpectThrows<std::invalid_argument>(
        [] {
            static_cast<void>(GridCollision::OverlapsCircle(
                {0.5f, 0.5f}, 0.2f, {{1.0f, 0.5f}, 0.0f}));
        },
        "circle query rejects a non-positive obstacle radius");
    context.ExpectThrows<std::invalid_argument>(
        [&map, infinity] {
            const std::array invalidBlocker{
                CircleObstacle{{1.0f, 0.5f}, infinity},
            };
            static_cast<void>(GridCollision::MoveCircle(
                map,
                {0.5f, 0.5f},
                {0.0f, 0.0f},
                0.2f,
                invalidBlocker));
        },
        "movement validates dynamic blockers even without displacement");
}

void TestExtremeFiniteValues(TestContext& context) {
    const float tinyRadius = (std::numeric_limits<float>::denorm_min)();
    const GridMap map = ParseValidMap(context, "P#D");

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
    TestCircleObstacleQueries(context);
    TestMovementResolution(context);
    TestDynamicCircleMovement(context);
    TestWallsAndDynamicCircles(context);
    TestInvalidCollisionArguments(context);
    TestExtremeFiniteValues(context);
}

} // namespace fps::tests
