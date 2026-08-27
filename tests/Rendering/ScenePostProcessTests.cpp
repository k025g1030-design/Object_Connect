#include "TestSupport.hpp"

#include "RetroFPS/Rendering/ScenePostProcessSettings.hpp"

#include <limits>

namespace fps::tests {
namespace {

void TestValidation(TestContext& context) {
    context.Expect(
        IsValidScenePostProcessSettings(ScenePostProcessSettings{}),
        "default scene post-process settings are valid");
    context.Expect(
        !IsValidScenePostProcessSettings({0.0f, 2.2f}),
        "zero brightness is rejected");
    context.Expect(
        !IsValidScenePostProcessSettings({1.0f, 0.0f}),
        "zero gamma is rejected");
    context.Expect(
        !IsValidScenePostProcessSettings(
            {(std::numeric_limits<float>::infinity)(), 2.2f}),
        "infinite brightness is rejected");
    context.Expect(
        !IsValidScenePostProcessSettings(
            {1.0f, (std::numeric_limits<float>::quiet_NaN)()}),
        "non-finite gamma is rejected");
}

void TestCurve(TestContext& context) {
    const ScenePostProcessSettings neutral{1.0f, 2.2f};
    context.Expect(
        NearlyEqual(SceneGammaExponent(neutral), 1.0f),
        "gamma 2.2 is the neutral curve exponent");
    context.Expect(
        NearlyEqual(ApplyScenePostProcessChannel(0.0f, neutral), 0.0f),
        "post-process keeps black black");
    context.Expect(
        NearlyEqual(ApplyScenePostProcessChannel(0.5f, neutral), 0.5f),
        "neutral post-process preserves a linear middle tone");

    const ScenePostProcessSettings brighter{1.25f, 2.2f};
    context.Expect(
        ApplyScenePostProcessChannel(0.5f, brighter) >
            ApplyScenePostProcessChannel(0.5f, neutral),
        "brightness raises a middle tone");
    context.Expect(
        NearlyEqual(ApplyScenePostProcessChannel(2.0f, brighter), 1.0f),
        "post-process output is clamped to the valid range");

    const ScenePostProcessSettings liftedShadows{1.0f, 3.0f};
    context.Expect(
        ApplyScenePostProcessChannel(0.2f, liftedShadows) > 0.2f,
        "gamma above 2.2 lifts dark tones");
}

} // namespace

void RunScenePostProcessTests(TestContext& context) {
    TestValidation(context);
    TestCurve(context);
}

} // namespace fps::tests
