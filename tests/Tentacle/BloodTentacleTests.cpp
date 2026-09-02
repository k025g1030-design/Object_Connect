#include "ObjectConnect/Tentacle/BloodTentacle.hpp"

#include "TestSupport.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace object_connect::tests {
namespace {

constexpr float kFixedStepSeconds = 1.0f / 120.0f;

[[nodiscard]] bool VecNearlyEqual(const object_connect::Vec2 left,
                                  const object_connect::Vec2 right,
                                  const float tolerance = 0.0001f) noexcept {
    return NearlyEqual(left.x, right.x, tolerance) &&
           NearlyEqual(left.y, right.y, tolerance);
}

void TestInitialization(TestContext& context) {
    object_connect::BloodTentacle tentacle;
    object_connect::BloodTentacleSettings settings;
    std::string error;
    context.Expect(
        tentacle.Initialize({10.0f, 20.0f}, {2.0f, 0.0f}, 90.0f, settings, error),
        "BloodTentacle initializes a valid ten-point chain");
    context.Expect(error.empty(), "BloodTentacle clears the initialization error");
    context.Expect(tentacle.GetPoints().size() == 10,
                   "BloodTentacle exposes the configured point count");
    context.Expect(VecNearlyEqual(tentacle.GetPoints().front(), {10.0f, 20.0f}),
                   "BloodTentacle pins its first point to the root");
    context.Expect(VecNearlyEqual(tentacle.GetPoints().back(), {100.0f, 20.0f}),
                   "BloodTentacle lays points out along the normalized direction");
    context.Expect(NearlyEqual(tentacle.GetDeployedLength(), 90.0f),
                   "BloodTentacle starts fully deployed");
}

void TestValidation(TestContext& context) {
    object_connect::BloodTentacle tentacle;
    object_connect::BloodTentacleSettings settings;
    settings.pointCount = 7;
    std::string error;
    context.Expect(
        !tentacle.Initialize({}, {1.0f, 0.0f}, 100.0f, settings, error),
        "BloodTentacle rejects fewer than eight points");
    context.Expect(!error.empty(), "BloodTentacle reports invalid point counts");

    settings.pointCount = 10;
    context.Expect(
        !tentacle.Initialize({}, {}, 100.0f, settings, error),
        "BloodTentacle rejects a zero initial direction");
    context.Expect(
        !tentacle.Initialize({}, {1.0f, 0.0f}, 0.0f, settings, error),
        "BloodTentacle rejects a zero maximum length");
}

void TestAnchorsAndFollowing(TestContext& context) {
    object_connect::BloodTentacle tentacle;
    object_connect::BloodTentacleSettings settings;
    settings.acceleration = {};
    std::string error;
    context.Expect(tentacle.Initialize({}, {1.0f, 0.0f}, 120.0f, settings, error),
                   "Anchor test tentacle initializes");

    tentacle.SetRootAnchor({12.0f, 18.0f});
    context.Expect(VecNearlyEqual(tentacle.GetPoints().front(), {12.0f, 18.0f}),
                   "SetRootAnchor pins the root immediately");

    tentacle.FollowTip({90.0f, 30.0f});
    tentacle.Update(kFixedStepSeconds);
    context.Expect(
        tentacle.GetTipMode() == object_connect::TentacleTipMode::FollowingTarget,
        "FollowTip selects following mode");
    context.Expect(VecNearlyEqual(tentacle.GetTipPosition(), {90.0f, 30.0f}),
                   "A zero-delay following tip reaches its target");

    tentacle.SetDeployedLength(40.0f);
    tentacle.FollowTip({212.0f, 18.0f});
    tentacle.Update(kFixedStepSeconds);
    context.Expect(VecNearlyEqual(tentacle.GetTipPosition(), {52.0f, 18.0f}, 0.001f),
                   "A following tip is clamped to its deployed reach");

    tentacle.AttachTip({80.0f, 40.0f});
    context.Expect(
        tentacle.GetTipMode() == object_connect::TentacleTipMode::Attached,
        "AttachTip selects attached mode");
    context.Expect(VecNearlyEqual(tentacle.GetTipPosition(), {80.0f, 40.0f}),
                   "AttachTip hard-pins the tip immediately");

    tentacle.DetachTip();
    context.Expect(tentacle.GetTipMode() == object_connect::TentacleTipMode::Free,
                   "DetachTip restores free mode");
}

void TestDelayedFollowing(TestContext& context) {
    object_connect::BloodTentacle tentacle;
    object_connect::BloodTentacleSettings settings;
    settings.acceleration = {};
    settings.followDelaySeconds = kFixedStepSeconds * 2.0f;
    std::string error;
    context.Expect(tentacle.Initialize({}, {1.0f, 0.0f}, 100.0f, settings, error),
                   "Delayed-follow tentacle initializes");

    const object_connect::Vec2 initialTip = tentacle.GetTipPosition();
    const object_connect::Vec2 target{70.0f, 20.0f};
    tentacle.FollowTip(target);
    tentacle.Update(kFixedStepSeconds);
    context.Expect(VecNearlyEqual(tentacle.GetTipPosition(), initialTip),
                   "Following history holds the tip for the first delayed step");
    tentacle.Update(kFixedStepSeconds);
    context.Expect(VecNearlyEqual(tentacle.GetTipPosition(), initialTip),
                   "Following history holds the tip for the full delay");
    tentacle.Update(kFixedStepSeconds);
    context.Expect(VecNearlyEqual(tentacle.GetTipPosition(), target),
                   "Following history releases the oldest target after the delay");
}

void TestDeployedLengthAndPull(TestContext& context) {
    object_connect::BloodTentacle tentacle;
    object_connect::BloodTentacleSettings settings;
    settings.acceleration = {};
    settings.maxRootPullPerStep = 8.0f;
    std::string error;
    context.Expect(tentacle.Initialize({}, {1.0f, 0.0f}, 100.0f, settings, error),
                   "Pull test tentacle initializes");

    tentacle.SetDeployedLength(-10.0f);
    context.Expect(NearlyEqual(tentacle.GetDeployedLength(), 0.0f),
                   "SetDeployedLength clamps below zero");
    context.Expect(std::ranges::all_of(
                       tentacle.GetPoints(), [](const object_connect::Vec2 point) {
                           return VecNearlyEqual(point, {});
                       }),
                   "A zero deployed length collapses a free chain to its root immediately");
    tentacle.SetDeployedLength(150.0f);
    context.Expect(NearlyEqual(tentacle.GetDeployedLength(), 100.0f),
                   "SetDeployedLength clamps above maximum length");

    tentacle.SetDeployedLength(40.0f);
    tentacle.AttachTip({100.0f, 0.0f});
    const object_connect::TentaclePullOutput pull = tentacle.GetRootPull();
    context.Expect(pull.active, "An overstretched attached tip produces root pull");
    context.Expect(VecNearlyEqual(pull.desiredRootDisplacement, {8.0f, 0.0f}),
                   "Root pull points toward the anchor and respects its step cap");
    context.Expect(NearlyEqual(pull.tension01, 1.0f),
                   "Large overstretch clamps normalized tension to one");

    tentacle.SetDeployedLength(100.0f);
    tentacle.AttachTip({80.0f, 0.0f});
    context.Expect(!tentacle.GetRootPull().active,
                   "An attached tip inside the deployed length has no root pull");
}

void TestFixedStepCapAndConstraints(TestContext& context) {
    object_connect::BloodTentacleSettings settings;
    settings.damping = 0.99f;
    object_connect::BloodTentacle capped;
    object_connect::BloodTentacle reference;
    std::string error;
    context.Expect(capped.Initialize({}, {1.0f, 0.0f}, 90.0f, settings, error),
                   "Capped-step tentacle initializes");
    context.Expect(reference.Initialize({}, {1.0f, 0.0f}, 90.0f, settings, error),
                   "Reference tentacle initializes");

    capped.Update(1.0f);
    reference.Update(kFixedStepSeconds * 8.0f);
    context.Expect(capped.GetPoints().size() == reference.GetPoints().size(),
                   "Fixed-step comparison uses matching point counts");
    for (std::size_t index = 0; index < capped.GetPoints().size(); ++index) {
        context.Expect(VecNearlyEqual(capped.GetPoints()[index],
                                      reference.GetPoints()[index], 0.001f),
                       "A long frame is capped to eight fixed simulation steps");
    }

    const float expectedSegmentLength =
        capped.GetDeployedLength() /
        static_cast<float>(capped.GetPoints().size() - 1);
    for (std::size_t index = 1; index < capped.GetPoints().size(); ++index) {
        const float length = object_connect::Length(
            capped.GetPoints()[index] - capped.GetPoints()[index - 1]);
        context.Expect(std::fabs(length - expectedSegmentLength) < 0.25f,
                       "Distance constraints keep every segment near its rest length");
    }
}

void TestGravitySag(TestContext& context) {
    object_connect::BloodTentacle tentacle;
    object_connect::BloodTentacleSettings settings;
    settings.damping = 0.98f;
    std::string error;
    context.Expect(tentacle.Initialize({}, {1.0f, 0.0f}, 120.0f, settings, error),
                   "Sag test tentacle initializes");
    tentacle.AttachTip({100.0f, 0.0f});
    for (int frame = 0; frame < 240; ++frame) {
        tentacle.Update(kFixedStepSeconds);
    }

    context.Expect(VecNearlyEqual(tentacle.GetPoints().front(), {}, 0.001f) &&
                       VecNearlyEqual(tentacle.GetPoints().back(), {100.0f, 0.0f},
                                      0.001f),
                   "Gravity sag keeps both anchors pinned");
    float greatestInteriorY = 0.0f;
    for (std::size_t index = 1; index + 1 < tentacle.GetPoints().size(); ++index) {
        greatestInteriorY = (std::max)(greatestInteriorY,
                                       tentacle.GetPoints()[index].y);
    }
    context.Expect(greatestInteriorY > 2.0f,
                   "Slack between horizontal anchors naturally sags under gravity");
}

} // namespace

void RunBloodTentacleTests(TestContext& context) {
    TestInitialization(context);
    TestValidation(context);
    TestAnchorsAndFollowing(context);
    TestDelayedFollowing(context);
    TestDeployedLengthAndPull(context);
    TestFixedStepCapAndConstraints(context);
    TestGravitySag(context);
}

} // namespace object_connect::tests
