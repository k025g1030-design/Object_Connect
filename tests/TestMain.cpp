#include "TestSupport.hpp"

#include <array>
#include <exception>
#include <iostream>
#include <string>

namespace {

struct TestSuite {
    const char* name;
    void (*run)(fps::tests::TestContext&);
};

} // namespace

int main() {
    fps::tests::TestContext context;
    constexpr std::array<TestSuite, 6> suites = {{
        {"World", fps::tests::RunWorldTests},
        {"Collision", fps::tests::RunCollisionTests},
        {"Rendering.MapGeometry", fps::tests::RunMapGeometryTests},
        {"Gameplay.PlayerController", fps::tests::RunPlayerControllerTests},
        {"Game.Flow", fps::tests::RunGameFlowTests},
        {"Game.CampaignResources", fps::tests::RunCampaignResourceTests},
    }};

    for (const TestSuite& suite : suites) {
        try {
            suite.run(context);
        } catch (const std::exception& exception) {
            context.Fail(
                std::string{"Unhandled exception in "} + suite.name + ": " + exception.what());
        } catch (...) {
            context.Fail(std::string{"Unhandled unknown exception in "} + suite.name);
        }
    }

    if (context.GetFailureCount() != 0) {
        std::cerr << context.GetFailureCount() << " core test(s) failed.\n";
        return 1;
    }

    std::cout << "All core tests passed.\n";
    return 0;
}
