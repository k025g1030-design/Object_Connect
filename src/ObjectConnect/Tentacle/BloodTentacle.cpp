#include "ObjectConnect/Tentacle/BloodTentacle.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <vector>

namespace object_connect {
namespace {

constexpr float kFixedStepSeconds = 1.0f / 120.0f;
constexpr int kMaximumSubsteps = 8;
constexpr float kLengthEpsilon = 0.0001f;
constexpr std::size_t kMinimumPointCount = 8;
constexpr std::size_t kMaximumPointCount = 12;

[[nodiscard]] bool ValidateSettings(const BloodTentacleSettings& settings,
                                    std::string& error) {
    if (settings.pointCount < kMinimumPointCount ||
        settings.pointCount > kMaximumPointCount) {
        error = "Blood tentacles require between 8 and 12 points.";
        return false;
    }
    if (!std::isfinite(settings.damping) || settings.damping < 0.0f ||
        settings.damping > 1.0f) {
        error = "Blood tentacle damping must be finite and between 0 and 1.";
        return false;
    }
    if (!IsFinite(settings.acceleration)) {
        error = "Blood tentacle acceleration must be finite.";
        return false;
    }
    if (settings.constraintIterations <= 0) {
        error = "Blood tentacle constraint iterations must be greater than zero.";
        return false;
    }
    if (!std::isfinite(settings.followDelaySeconds) ||
        settings.followDelaySeconds < 0.0f) {
        error = "Blood tentacle follow delay must be finite and non-negative.";
        return false;
    }
    if (!std::isfinite(settings.maxRootPullPerStep) ||
        settings.maxRootPullPerStep < 0.0f) {
        error = "Blood tentacle root pull limit must be finite and non-negative.";
        return false;
    }
    return true;
}

[[nodiscard]] std::size_t DelayStepCount(const float delaySeconds) noexcept {
    if (delaySeconds <= 0.0f) {
        return 0;
    }

    const double requestedSteps =
        static_cast<double>(delaySeconds) / static_cast<double>(kFixedStepSeconds);
    const double roundedUp = std::ceil(requestedSteps - 0.000001);
    const double maximum =
        static_cast<double>((std::numeric_limits<std::size_t>::max)());
    if (!std::isfinite(roundedUp) || roundedUp >= maximum) {
        return (std::numeric_limits<std::size_t>::max)();
    }
    return static_cast<std::size_t>((std::max)(1.0, roundedUp));
}

[[nodiscard]] bool IsPinnedPoint(const std::size_t index,
                                 const std::size_t pointCount,
                                 const TentacleTipMode tipMode) noexcept {
    return index == 0 ||
           (index + 1 == pointCount && tipMode != TentacleTipMode::Free);
}

} // namespace

bool BloodTentacle::Initialize(const Vec2 root, const Vec2 initialDirection,
                               const float maxLength,
                               const BloodTentacleSettings& settings,
                               std::string& error) {
    error.clear();
    if (!IsFinite(root)) {
        error = "Blood tentacle root position must be finite.";
        return false;
    }
    if (!IsFinite(initialDirection) ||
        LengthSquared(initialDirection) <= kLengthEpsilon * kLengthEpsilon) {
        error = "Blood tentacle initial direction must be finite and non-zero.";
        return false;
    }
    if (!std::isfinite(maxLength) || maxLength <= 0.0f) {
        error = "Blood tentacle maximum length must be finite and greater than zero.";
        return false;
    }
    if (!ValidateSettings(settings, error)) {
        return false;
    }

    try {
        const Vec2 direction = NormalizeOr(initialDirection, {1.0f, 0.0f});
        const float segmentLength =
            maxLength / static_cast<float>(settings.pointCount - 1);
        std::vector<Vec2> nextPoints(settings.pointCount);
        for (std::size_t index = 0; index < nextPoints.size(); ++index) {
            nextPoints[index] =
                root + direction * (segmentLength * static_cast<float>(index));
        }
        std::vector<Vec2> nextPreviousPoints = nextPoints;

        settings_ = settings;
        points_ = std::move(nextPoints);
        previousPoints_ = std::move(nextPreviousPoints);
        tipTargets_.clear();
        rootAnchor_ = root;
        tipTarget_ = points_.back();
        attachedAnchor_ = points_.back();
        initialDirection_ = direction;
        tipMode_ = TentacleTipMode::Free;
        maxLength_ = maxLength;
        deployedLength_ = maxLength;
        accumulator_ = 0.0f;
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize blood tentacle: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize blood tentacle because of an unknown error.";
    }
    return false;
}

void BloodTentacle::SetRootAnchor(const Vec2 position) noexcept {
    if (!IsFinite(position)) {
        return;
    }
    rootAnchor_ = position;
    if (!points_.empty()) {
        points_.front() = position;
        previousPoints_.front() = position;
    }
}

void BloodTentacle::FollowTip(const Vec2 target) noexcept {
    if (!IsFinite(target) || points_.empty()) {
        return;
    }
    if (tipMode_ != TentacleTipMode::FollowingTarget) {
        tipTargets_.clear();
        previousPoints_.back() = points_.back();
    }
    tipTarget_ = target;
    tipMode_ = TentacleTipMode::FollowingTarget;
}

void BloodTentacle::AttachTip(const Vec2 anchor) noexcept {
    if (!IsFinite(anchor) || points_.empty()) {
        return;
    }
    tipTargets_.clear();
    attachedAnchor_ = anchor;
    tipMode_ = TentacleTipMode::Attached;
    points_.back() = anchor;
    previousPoints_.back() = anchor;
}

void BloodTentacle::DetachTip() noexcept {
    tipTargets_.clear();
    if (!points_.empty()) {
        previousPoints_.back() = points_.back();
    }
    tipMode_ = TentacleTipMode::Free;
}

void BloodTentacle::SetDeployedLength(const float length) noexcept {
    if (!std::isfinite(length)) {
        return;
    }
    deployedLength_ = std::clamp(length, 0.0f, maxLength_);
    if (!points_.empty() && deployedLength_ <= kLengthEpsilon) {
        std::ranges::fill(points_, rootAnchor_);
        std::ranges::fill(previousPoints_, rootAnchor_);
        PinAnchors();
        return;
    }
    // Deployment is presentation-visible state, so reshape immediately instead
    // of leaving a newly-created zero-length preview stretched to maxLength
    // until the next fixed simulation tick.
    SolveConstraints();
}

void BloodTentacle::Update(const float frameDeltaSeconds) noexcept {
    if (points_.empty() || !std::isfinite(frameDeltaSeconds) ||
        frameDeltaSeconds <= 0.0f) {
        return;
    }

    constexpr float maximumAccumulatedTime =
        kFixedStepSeconds * static_cast<float>(kMaximumSubsteps);
    const float acceptedTime =
        (std::min)(frameDeltaSeconds, maximumAccumulatedTime);
    accumulator_ =
        (std::min)(accumulator_ + acceptedTime, maximumAccumulatedTime);

    int completedSteps = 0;
    while (completedSteps < kMaximumSubsteps &&
           accumulator_ + 0.0000001f >= kFixedStepSeconds) {
        Step(kFixedStepSeconds);
        accumulator_ -= kFixedStepSeconds;
        if (accumulator_ < 0.0f) {
            accumulator_ = 0.0f;
        }
        ++completedSteps;
    }
}

void BloodTentacle::Step(const float fixedDeltaSeconds) noexcept {
    const std::size_t lastIndex = points_.size() - 1;
    Vec2 activeTipTarget{};
    const bool tipPinned = tipMode_ != TentacleTipMode::Free;
    if (tipMode_ == TentacleTipMode::FollowingTarget) {
        activeTipTarget = GetDelayedTipTarget();
        const Vec2 rootToTarget = activeTipTarget - rootAnchor_;
        const float targetDistance = Length(rootToTarget);
        if (targetDistance > deployedLength_ && targetDistance > kLengthEpsilon) {
            activeTipTarget = rootAnchor_ +
                              rootToTarget * (deployedLength_ / targetDistance);
        } else if (deployedLength_ <= kLengthEpsilon) {
            activeTipTarget = rootAnchor_;
        }
    } else if (tipMode_ == TentacleTipMode::Attached) {
        activeTipTarget = attachedAnchor_;
    }

    points_.front() = rootAnchor_;
    previousPoints_.front() = rootAnchor_;
    const float accelerationScale = fixedDeltaSeconds * fixedDeltaSeconds;
    for (std::size_t index = 1; index < points_.size(); ++index) {
        if (tipPinned && index == lastIndex) {
            continue;
        }

        const Vec2 current = points_[index];
        const Vec2 velocity =
            (current - previousPoints_[index]) * settings_.damping;
        previousPoints_[index] = current;
        points_[index] =
            current + velocity + settings_.acceleration * accelerationScale;
    }

    if (tipPinned) {
        points_.back() = activeTipTarget;
        // A pinned point stores its active target in previousPoints_. PinAnchors
        // reuses it while each distance-constraint iteration moves neighbours.
        previousPoints_.back() = activeTipTarget;
    }
    SolveConstraints();
}

void BloodTentacle::SolveConstraints() noexcept {
    if (points_.size() < 2) {
        return;
    }

    const float segmentLength =
        deployedLength_ / static_cast<float>(points_.size() - 1);
    PinAnchors();
    for (int iteration = 0; iteration < settings_.constraintIterations; ++iteration) {
        for (std::size_t firstIndex = 0; firstIndex + 1 < points_.size();
             ++firstIndex) {
            const std::size_t secondIndex = firstIndex + 1;
            const bool firstPinned =
                IsPinnedPoint(firstIndex, points_.size(), tipMode_);
            const bool secondPinned =
                IsPinnedPoint(secondIndex, points_.size(), tipMode_);
            if (firstPinned && secondPinned) {
                continue;
            }

            Vec2 difference = points_[secondIndex] - points_[firstIndex];
            const float distance = Length(difference);
            if (!std::isfinite(distance) || distance <= kLengthEpsilon) {
                if (segmentLength <= kLengthEpsilon) {
                    if (firstPinned) {
                        points_[secondIndex] = points_[firstIndex];
                    } else if (secondPinned) {
                        points_[firstIndex] = points_[secondIndex];
                    } else {
                        const Vec2 midpoint =
                            (points_[firstIndex] + points_[secondIndex]) * 0.5f;
                        points_[firstIndex] = midpoint;
                        points_[secondIndex] = midpoint;
                    }
                    continue;
                }

                const Vec2 direction = initialDirection_;
                if (firstPinned) {
                    points_[secondIndex] =
                        points_[firstIndex] + direction * segmentLength;
                } else if (secondPinned) {
                    points_[firstIndex] =
                        points_[secondIndex] - direction * segmentLength;
                } else {
                    const Vec2 midpoint =
                        (points_[firstIndex] + points_[secondIndex]) * 0.5f;
                    const Vec2 halfSegment = direction * (segmentLength * 0.5f);
                    points_[firstIndex] = midpoint - halfSegment;
                    points_[secondIndex] = midpoint + halfSegment;
                }
                continue;
            }

            const float relativeError = (distance - segmentLength) / distance;
            const Vec2 correction = difference * relativeError;
            if (firstPinned) {
                points_[secondIndex] -= correction;
            } else if (secondPinned) {
                points_[firstIndex] += correction;
            } else {
                const Vec2 halfCorrection = correction * 0.5f;
                points_[firstIndex] += halfCorrection;
                points_[secondIndex] -= halfCorrection;
            }
        }
        PinAnchors();
    }
}

Vec2 BloodTentacle::GetDelayedTipTarget() noexcept {
    const std::size_t delaySteps = DelayStepCount(settings_.followDelaySeconds);
    if (delaySteps == 0) {
        tipTargets_.clear();
        return tipTarget_;
    }

    try {
        tipTargets_.push_back(tipTarget_);
    } catch (...) {
        tipTargets_.clear();
        return tipTarget_;
    }

    if (tipTargets_.size() <= delaySteps) {
        return points_.back();
    }
    const Vec2 delayed = tipTargets_.front();
    tipTargets_.pop_front();
    return delayed;
}

void BloodTentacle::PinAnchors() noexcept {
    if (points_.empty()) {
        return;
    }
    points_.front() = rootAnchor_;
    previousPoints_.front() = rootAnchor_;
    if (tipMode_ == TentacleTipMode::Attached) {
        points_.back() = attachedAnchor_;
        previousPoints_.back() = attachedAnchor_;
    } else if (tipMode_ == TentacleTipMode::FollowingTarget) {
        points_.back() = previousPoints_.back();
    }
}

TentaclePullOutput BloodTentacle::GetRootPull() const noexcept {
    if (points_.empty() || tipMode_ != TentacleTipMode::Attached) {
        return {};
    }

    const Vec2 towardAnchor = attachedAnchor_ - rootAnchor_;
    const float anchorDistance = Length(towardAnchor);
    const float excessLength = anchorDistance - deployedLength_;
    if (!std::isfinite(anchorDistance) || excessLength <= kLengthEpsilon) {
        return {};
    }

    const float correctionLength =
        (std::min)(excessLength, settings_.maxRootPullPerStep);
    const float tensionDenominator =
        (std::max)(deployedLength_, kLengthEpsilon);
    return {
        NormalizeOr(towardAnchor, {}) * correctionLength,
        std::clamp(excessLength / tensionDenominator, 0.0f, 1.0f),
        true,
    };
}

Vec2 BloodTentacle::GetTipPosition() const noexcept {
    return points_.empty() ? Vec2{} : points_.back();
}

} // namespace object_connect
