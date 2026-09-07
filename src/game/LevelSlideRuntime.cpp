#include "game/LevelSlideRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

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

bool parseInteger(const CinematicCommand& command, std::string_view name,
                  std::int32_t& output) noexcept {
    const CinematicAttribute* attribute = command.findAttribute(name);
    if (attribute == nullptr) {
        return false;
    }
    const char* begin = attribute->value.data();
    const char* end = begin + attribute->value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool attributeAsBoolean(const CinematicCommand& command,
                        std::string_view name) noexcept {
    const CinematicAttribute* attribute = command.findAttribute(name);
    return attribute != nullptr &&
           (attribute->value == "true" || attribute->value == "1");
}

} // namespace

void LevelSlideRuntime::bind(
    std::span<const LevelSlideAsset> slides,
    std::span<const LevelWayPointAsset> waypoints) {
    slides_ = slides;
    waypoints_ = waypoints;
    enabled_.clear();
    enabled_.reserve(slides.size());
    for (const LevelSlideAsset& slide : slides) {
        enabled_.push_back(slide.enabled);
    }
    activeSlide_ = nullptr;
    cooldownSlide_ = nullptr;
    cooldownRemainingMilliseconds_ = 0;
    active_ = false;
}

Result LevelSlideRuntime::applyCinematicCommand(
    const CinematicCommand& command) {
    if (command.name != "Enable_Slide") {
        return Result::success();
    }

    // CCinematicThread::SetSlideEnable (0x0036fe98) reads ^ID^Slide,
    // reads Enable through IAttributes::getAttributeAsBool, resolves the
    // object with CLevel::FindObjectInRooms, and calls the object's virtual
    // enable setter at vtable offset 0x9c. Unlike EnableTriggerRestore, a
    // missing ID or object is a command failure.
    std::int32_t objectId = -1;
    if (!parseInteger(command, "^ID^Slide", objectId)) {
        return Result::failure("Enable_Slide has no valid slide ID");
    }
    const auto slide = std::find_if(
        slides_.begin(), slides_.end(), [objectId](const auto& candidate) {
            return candidate.objectId == objectId;
        });
    if (slide == slides_.end()) {
        return Result::failure("Enable_Slide references a missing slide");
    }
    enabled_[static_cast<std::size_t>(slide - slides_.begin())] =
        attributeAsBoolean(command, "Enable");
    return Result::success();
}

SlideCatch LevelSlideRuntime::findCatch(
    const assets::Vector3& playerPosition, bool playerIsDownFalling,
    bool allowTerminalCatch) const noexcept {
    SlideCatch best;
    best.distanceSquared = kCatchDistanceSquared;
    for (const LevelSlideAsset& slide : slides_) {
        if (!enabled(&slide) ||
            (&slide == cooldownSlide_ &&
             cooldownRemainingMilliseconds_ != 0) ||
            slide.waypointIds.size() < 2) {
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
    if (best.slide == nullptr) {
        return best;
    }

    const LevelWayPointAsset* segmentEnd =
        waypoint(best.slide->waypointIds[best.segmentIndex + 1]);
    if (segmentEnd == nullptr) {
        return {};
    }

    // CSlider::Update rejects an ordinary catch within the final 200 cm of a
    // terminal segment. Native states 19 (web-swing release) and 21 (slider
    // jump fall) bypass that endpoint gate; an ordinary jump does not. This
    // prevents a player leaving waypoint 446 from being pulled back onto
    // Slide 1039 while following the descending bonus trail.
    if (best.segmentIndex + 2 >= best.slide->waypointIds.size() &&
        length(subtract(best.projectedPosition, segmentEnd->position)) <=
            200.0F &&
        !allowTerminalCatch) {
        return {};
    }

    const assets::Vector3 difference =
        subtract(best.projectedPosition, playerPosition);
    const float horizontalDistanceSquared =
        difference.x * difference.x + difference.y * difference.y;
    if (horizontalDistanceSquared >= 2500.0F) {
        return {};
    }

    const bool fallingBelowSlide =
        difference.z > 0.0F && playerIsDownFalling;
    if (fallingBelowSlide) {
        if (difference.z >= 800.0F) {
            return {};
        }
    } else if (difference.z <= -30.0F) {
        return {};
    }
    return best;
}

Result LevelSlideRuntime::start(const SlideCatch& caught,
                                float speedCentimetersPerSecond) noexcept {
    if (caught.slide == nullptr || !enabled(caught.slide) ||
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
    if (slide == slides_.end() || !enabled(&*slide) ||
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
    // CSlider::Update (0x0031d920) calls the virtual enabled predicate at
    // vtable offset 0x98 before doing any segment/player work.
    if (!active_ || activeSlide_ == nullptr || !enabled(activeSlide_) ||
        elapsedMilliseconds == 0) {
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

SlideExit LevelSlideRuntime::jumpFinish(
    std::uint32_t cooldownMilliseconds) noexcept {
    // Player::UpdateSlide (0x0034c498) clears CSlider+0x1d8 and stores the
    // selected slide_to_jump animation length plus 2000 ms at CSlider+0x21c.
    // CSlider::Update (0x0031d920) decrements that timer and skips all catch
    // work until it expires, preventing state 21 from re-catching the rope
    // that was just released.
    const LevelSlideAsset* releasedSlide = activeSlide_;
    SlideExit result = finish();
    cooldownSlide_ = releasedSlide;
    cooldownRemainingMilliseconds_ = cooldownMilliseconds;
    return result;
}

void LevelSlideRuntime::advanceCooldown(
    std::uint32_t elapsedMilliseconds) noexcept {
    if (cooldownRemainingMilliseconds_ == 0 || elapsedMilliseconds == 0) {
        return;
    }
    if (elapsedMilliseconds >= cooldownRemainingMilliseconds_) {
        cooldownRemainingMilliseconds_ = 0;
        cooldownSlide_ = nullptr;
    } else {
        cooldownRemainingMilliseconds_ -= elapsedMilliseconds;
    }
}

std::optional<bool> LevelSlideRuntime::enabled(
    std::int32_t objectId) const noexcept {
    const auto slide = std::find_if(
        slides_.begin(), slides_.end(), [objectId](const auto& candidate) {
            return candidate.objectId == objectId;
        });
    if (slide == slides_.end()) {
        return std::nullopt;
    }
    return enabled_[static_cast<std::size_t>(slide - slides_.begin())];
}

bool LevelSlideRuntime::enabled(
    const LevelSlideAsset* slide) const noexcept {
    for (std::size_t index = 0; index < slides_.size(); ++index) {
        if (&slides_[index] == slide) {
            return index < enabled_.size() && enabled_[index];
        }
    }
    return false;
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
