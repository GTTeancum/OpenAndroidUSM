#include "game/LevelSlideRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

constexpr float kCatchDistanceSquared = 640000.0F;
constexpr float kFallbackSlideSpeedCentimetersPerSecond = 1100.0F;

assets::Vector3 subtract(const assets::Vector3& left,
                         const assets::Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

float dot(const assets::Vector3& left,
          const assets::Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

float length(const assets::Vector3& value) noexcept {
    return std::sqrt(dot(value, value));
}

} // namespace

void LevelSlideRuntime::bind(
    std::span<const LevelSlideAsset> slides,
    std::span<const LevelWayPointAsset> waypoints) noexcept {
    slides_ = slides;
    waypoints_ = waypoints;
    activeSlide_ = nullptr;
    active_ = false;
}

SlideCatch LevelSlideRuntime::findCatch(
    const assets::Vector3& playerPosition) const noexcept {
    SlideCatch best;
    best.distanceSquared = kCatchDistanceSquared;
    for (const LevelSlideAsset& slide : slides_) {
        if (!slide.enabled || slide.waypointIds.size() < 2) {
            continue;
        }
        for (std::size_t index = 0; index + 1 < slide.waypointIds.size();
             ++index) {
            const LevelWayPointAsset* start = waypoint(slide.waypointIds[index]);
            const LevelWayPointAsset* end =
                waypoint(slide.waypointIds[index + 1]);
            if (start == nullptr || end == nullptr) {
                continue;
            }
            const assets::Vector3 edge = subtract(end->position,
                                                  start->position);
            const float edgeLengthSquared = dot(edge, edge);
            if (edgeLengthSquared <= std::numeric_limits<float>::epsilon()) {
                continue;
            }
            const float factor = std::clamp(
                dot(subtract(playerPosition, start->position), edge) /
                    edgeLengthSquared,
                0.0F, 1.0F);
            const assets::Vector3 projected{
                start->position.x + edge.x * factor,
                start->position.y + edge.y * factor,
                start->position.z + edge.z * factor,
            };
            const assets::Vector3 difference = subtract(playerPosition,
                                                         projected);
            const float candidateDistanceSquared = dot(difference, difference);
            if (candidateDistanceSquared >= best.distanceSquared) {
                continue;
            }
            best = {&slide, index, projected, candidateDistanceSquared};
        }
    }
    return best;
}

Result LevelSlideRuntime::start(const SlideCatch& caught,
                                float speedCentimetersPerSecond) noexcept {
    if (caught.slide == nullptr || !caught.slide->enabled ||
        caught.segmentIndex + 1 >= caught.slide->waypointIds.size()) {
        return Result::failure("Slide catch has no valid segment");
    }
    activeSlide_ = caught.slide;
    active_ = true;
    speedCentimetersPerSecond_ =
        std::isfinite(speedCentimetersPerSecond) &&
                speedCentimetersPerSecond > 0.0F
            ? speedCentimetersPerSecond
            : kFallbackSlideSpeedCentimetersPerSecond;
    const LevelWayPointAsset* startPoint =
        waypoint(activeSlide_->waypointIds[caught.segmentIndex]);
    if (startPoint == nullptr) {
        activeSlide_ = nullptr;
        active_ = false;
        return Result::failure("Slide catch start waypoint is missing");
    }
    const float offset = length(subtract(caught.projectedPosition,
                                         startPoint->position));
    Result result = enterSegment(caught.segmentIndex, offset);
    if (!result) {
        activeSlide_ = nullptr;
        active_ = false;
    }
    return result;
}

Result LevelSlideRuntime::start(std::int32_t slideId,
                                float speedCentimetersPerSecond) noexcept {
    const auto slide = std::find_if(
        slides_.begin(), slides_.end(), [slideId](const auto& candidate) {
            return candidate.objectId == slideId;
        });
    if (slide == slides_.end() || !slide->enabled ||
        slide->waypointIds.size() < 2) {
        return Result::failure("Slide ID has no enabled segment graph");
    }
    const LevelWayPointAsset* startPoint = waypoint(slide->waypointIds[0]);
    if (startPoint == nullptr) {
        return Result::failure("Slide start waypoint is missing");
    }
    activeSlide_ = &*slide;
    active_ = true;
    speedCentimetersPerSecond_ =
        std::isfinite(speedCentimetersPerSecond) &&
                speedCentimetersPerSecond > 0.0F
            ? speedCentimetersPerSecond
            : kFallbackSlideSpeedCentimetersPerSecond;
    position_ = startPoint->position;
    Result result = enterSegment(0, 0.0F);
    if (!result) {
        activeSlide_ = nullptr;
        active_ = false;
    }
    return result;
}

void LevelSlideRuntime::update(std::uint32_t elapsedMilliseconds) noexcept {
    if (!active_ || activeSlide_ == nullptr || elapsedMilliseconds == 0) {
        return;
    }
    float distance = speedCentimetersPerSecond_ *
                     (static_cast<float>(elapsedMilliseconds) / 1000.0F);
    while (active_ && distance > 0.0F) {
        const float remaining = segmentLength_ - distanceAlongSegment_;
        if (distance < remaining) {
            distanceAlongSegment_ += distance;
            distance = 0.0F;
        } else {
            distance -= std::max(remaining, 0.0F);
            ++segmentIndex_;
            if (segmentIndex_ + 1 >= activeSlide_->waypointIds.size()) {
                const LevelWayPointAsset* end =
                    waypoint(activeSlide_->waypointIds.back());
                if (end != nullptr) {
                    position_ = end->position;
                }
                active_ = false;
                break;
            }
            if (!enterSegment(segmentIndex_, 0.0F)) {
                active_ = false;
                break;
            }
        }
    }
    if (active_) {
        const LevelWayPointAsset* startPoint =
            waypoint(activeSlide_->waypointIds[segmentIndex_]);
        position_ = {
            startPoint->position.x + direction_.x * distanceAlongSegment_,
            startPoint->position.y + direction_.y * distanceAlongSegment_,
            startPoint->position.z + direction_.z * distanceAlongSegment_,
        };
    }
}

SlideExit LevelSlideRuntime::finish() noexcept {
    SlideExit result;
    result.velocityCentimetersPerSecond = {
        direction_.x * speedCentimetersPerSecond_,
        direction_.y * speedCentimetersPerSecond_,
        direction_.z * speedCentimetersPerSecond_,
    };
    if (activeSlide_ != nullptr) {
        result.electricShock = activeSlide_->electricShock;
        const LevelWayPointAsset* end =
            waypoint(activeSlide_->waypointIds.back());
        if (end != nullptr) {
            result.useGravity = end->useGravityWhenEnd;
            result.electricShock = result.electricShock || end->electricShock;
        }
    }
    active_ = false;
    activeSlide_ = nullptr;
    return result;
}

const LevelWayPointAsset* LevelSlideRuntime::waypoint(
    std::int32_t id) const noexcept {
    const auto match = std::find_if(
        waypoints_.begin(), waypoints_.end(), [id](const auto& candidate) {
            return candidate.objectId == id;
        });
    return match == waypoints_.end() ? nullptr : &*match;
}

Result LevelSlideRuntime::enterSegment(std::size_t index,
                                       float offset) noexcept {
    if (activeSlide_ == nullptr ||
        index + 1 >= activeSlide_->waypointIds.size()) {
        return Result::failure("Slide segment index is invalid");
    }
    const LevelWayPointAsset* startPoint =
        waypoint(activeSlide_->waypointIds[index]);
    const LevelWayPointAsset* endPoint =
        waypoint(activeSlide_->waypointIds[index + 1]);
    if (startPoint == nullptr || endPoint == nullptr) {
        return Result::failure("Slide segment waypoint is missing");
    }
    direction_ = subtract(endPoint->position, startPoint->position);
    segmentLength_ = length(direction_);
    if (segmentLength_ <= std::numeric_limits<float>::epsilon()) {
        return Result::failure("Slide segment has zero length");
    }
    direction_.x /= segmentLength_;
    direction_.y /= segmentLength_;
    direction_.z /= segmentLength_;
    segmentIndex_ = index;
    distanceAlongSegment_ = std::clamp(offset, 0.0F, segmentLength_);
    position_ = {
        startPoint->position.x + direction_.x * distanceAlongSegment_,
        startPoint->position.y + direction_.y * distanceAlongSegment_,
        startPoint->position.z + direction_.z * distanceAlongSegment_,
    };
    return Result::success();
}

} // namespace usm::game
