#include "TestSupport.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <string>

namespace {

struct TestSuite {
    const char* name;
    void (*run)(object_connect::tests::TestContext&);
};

} // namespace

int main() {
    object_connect::tests::TestContext context;
    constexpr std::array<TestSuite, 6> suites = {{
        {"Data.PuzzleCatalog", object_connect::tests::RunPuzzleCatalogTests},
        {"Game.Flow", object_connect::tests::RunGameFlowTests},
        {"Geometry.2D", object_connect::tests::RunGeometry2DTests},
        {"Puzzle.Board", object_connect::tests::RunPuzzleBoardTests},
        {"Tentacle.Simulation", object_connect::tests::RunBloodTentacleTests},
        {"Tentacle.Ribbon", object_connect::tests::RunRibbonStripTests},
    }};

    for (const TestSuite& suite : suites) {
        try {
            suite.run(context);
        } catch (const std::exception& exception) {
            context.Fail(std::string{"Unhandled exception in "} + suite.name + ": " +
                         exception.what());
        } catch (...) {
            context.Fail(std::string{"Unhandled unknown exception in "} + suite.name);
        }
    }

    if (context.GetFailureCount() != 0) {
        std::cerr << context.GetFailureCount() << " Object_Connect core test(s) failed.\n";
        return 1;
    }

    std::cout << "All Object_Connect core tests passed.\n";
    return 0;
}
