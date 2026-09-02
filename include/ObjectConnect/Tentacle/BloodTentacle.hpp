#pragma once

#include "ObjectConnect/Math/Vec2.hpp"

#include <cstddef>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace object_connect {

enum class TentacleTipMode {
    Free,
    FollowingTarget,
    Attached,
};

struct BloodTentacleSettings final {
    std::size_t pointCount = 10;
    float damping = 0.985f;
    Vec2 acceleration{0.0f, 650.0f};
    int constraintIterations = 6;
    float followDelaySeconds = 0.0f;
    float maxRootPullPerStep = 8.0f;
};

struct TentaclePullOutput final {
    Vec2 desiredRootDisplacement{};
    float tension01 = 0.0f;
    bool active = false;
};

class BloodTentacle final {
public:
    [[nodiscard]] bool Initialize(Vec2 root, Vec2 initialDirection, float maxLength,
                                  const BloodTentacleSettings& settings,
                                  std::string& error);

    void SetRootAnchor(Vec2 position) noexcept;
    void FollowTip(Vec2 target) noexcept;
    void AttachTip(Vec2 anchor) noexcept;
    void DetachTip() noexcept;
    void SetDeployedLength(float length) noexcept;
    void Update(float frameDeltaSeconds) noexcept;

    [[nodiscard]] std::span<const Vec2> GetPoints() const noexcept { return points_; }
    [[nodiscard]] TentaclePullOutput GetRootPull() const noexcept;
    [[nodiscard]] Vec2 GetTipPosition() const noexcept;
    [[nodiscard]] float GetDeployedLength() const noexcept { return deployedLength_; }
    [[nodiscard]] float GetMaxLength() const noexcept { return maxLength_; }
    [[nodiscard]] TentacleTipMode GetTipMode() const noexcept { return tipMode_; }
    [[nodiscard]] bool IsInitialized() const noexcept { return !points_.empty(); }

private:
    void Step(float fixedDeltaSeconds) noexcept;
    void SolveConstraints() noexcept;
    [[nodiscard]] Vec2 GetDelayedTipTarget() noexcept;
    void PinAnchors() noexcept;

    BloodTentacleSettings settings_{};
    std::vector<Vec2> points_;
    std::vector<Vec2> previousPoints_;
    std::deque<Vec2> tipTargets_;
    Vec2 rootAnchor_{};
    Vec2 tipTarget_{};
    Vec2 attachedAnchor_{};
    Vec2 initialDirection_{1.0f, 0.0f};
    TentacleTipMode tipMode_ = TentacleTipMode::Free;
    float maxLength_ = 0.0f;
    float deployedLength_ = 0.0f;
    float accumulator_ = 0.0f;
};

} // namespace object_connect
