#pragma once

#include <cmath>
#include <iostream>
#include <string_view>
#include <utility>

namespace fps::tests {

class TestContext final {
public:
    void Expect(const bool condition, const std::string_view description) {
        if (condition) {
            return;
        }

        std::cerr << "FAILED: " << description << '\n';
        ++failureCount_;
    }

    void Fail(const std::string_view description) { Expect(false, description); }

    template <typename Exception, typename Callable>
    void ExpectThrows(Callable&& callable, const std::string_view description) {
        try {
            std::forward<Callable>(callable)();
        } catch (const Exception&) {
            return;
        } catch (...) {
            Fail(description);
            return;
        }

        Fail(description);
    }

    [[nodiscard]] int GetFailureCount() const noexcept { return failureCount_; }

private:
    int failureCount_ = 0;
};

[[nodiscard]] inline bool NearlyEqual(const float left, const float right,
                                      const float tolerance = 0.0001f) noexcept {
    return std::fabs(left - right) <= tolerance;
}

void RunWorldTests(TestContext& context);
void RunCollisionTests(TestContext& context);
void RunMapGeometryTests(TestContext& context);
void RunPlayerControllerTests(TestContext& context);
void RunGameFlowTests(TestContext& context);
void RunMapSceneManagerTests(TestContext& context);
void RunCampaignResourceTests(TestContext& context);

} // namespace fps::tests
