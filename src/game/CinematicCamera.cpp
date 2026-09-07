#include "game/CinematicCamera.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace usm::game {
namespace {

std::string removeLeadingHash(std::string value) {
    if (value.starts_with('#')) {
        value.erase(value.begin());
    }
    return value;
}

assets::Vector3 vectorFrom(
    const assets::ColladaAnimationSample& sample) noexcept {
    return {sample.value[0], sample.value[1], sample.value[2]};
}

} // namespace

bool isPointInScreen(const CameraPose& camera, float aspectRatio,
                     const assets::Vector3& point) noexcept {
    const auto normalized = [](assets::Vector3 value) {
        const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        return length > 0.0F ? assets::Vector3{value.x / length, value.y / length, value.z / length}
                             : assets::Vector3{};
    };
    const auto cross = [](const assets::Vector3& a, const assets::Vector3& b) {
        return assets::Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                               a.x * b.y - a.y * b.x};
    };
    const auto dot = [](const assets::Vector3& a, const assets::Vector3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    };
    const auto forward = normalized({camera.target.x - camera.position.x,
        camera.target.y - camera.position.y, camera.target.z - camera.position.z});
    const auto right = normalized(cross(camera.up, forward));
    const auto up = cross(forward, right);
    const assets::Vector3 relative{point.x - camera.position.x,
        point.y - camera.position.y, point.z - camera.position.z};
    const float depth = dot(relative, forward);
    const float halfHeight = depth * std::tan(camera.verticalFieldOfViewDegrees * 0.00872664626F);
    return aspectRatio > 0.0F && dot(right, right) > 0.5F &&
        depth >= camera.nearPlane && depth <= camera.farPlane &&
        std::abs(dot(relative, up)) <= halfHeight &&
        std::abs(dot(relative, right)) <= halfHeight * aspectRatio;
}

Result CinematicCamera::bind(
    const assets::ColladaAnimationFile& animation, float farPlaneOverride,
    std::int32_t clipId) {
    positionTrack_ = nullptr;
    targetTrack_ = nullptr;
    defaults_ = {};
    clipId_ = -1;
    clipStartMilliseconds_ = 0;
    clipEndMilliseconds_ = 0;
    if (!animation.camera()) {
        return Result::failure("Cinematic BDAE has no camera definition");
    }
    const assets::ColladaCamera& camera = *animation.camera();
    if (camera.orthographic) {
        return Result::failure("Orthographic cinematic cameras are unsupported");
    }
    if (clipId < 0 ||
        static_cast<std::size_t>(clipId) >= animation.clips().size()) {
        return Result::failure("Cinematic camera clip ID is invalid");
    }
    const assets::ColladaAnimationClip& clip =
        animation.clips()[static_cast<std::size_t>(clipId)];

    std::string cameraNode = camera.id;
    if (cameraNode.ends_with("-camera")) {
        cameraNode.resize(cameraNode.size() - std::string("-camera").size());
    }
    cameraNode += "-node";
    const std::string targetNode = removeLeadingHash(camera.targetNode);
    for (const assets::ColladaAnimationTrack& track : animation.tracks()) {
        if (track.property != assets::ColladaAnimationProperty::Translation ||
            track.componentCount != 3) {
            continue;
        }
        if (track.id == cameraNode + "-translation") {
            positionTrack_ = &track;
        }
        if (track.id == targetNode + "-translation") {
            targetTrack_ = &track;
        }
    }
    if (!positionTrack_ || !targetTrack_) {
        return Result::failure(
            "Cinematic camera position or target channel is missing");
    }
    defaults_.verticalFieldOfViewDegrees =
        camera.verticalFieldOfViewDegrees;
    defaults_.nearPlane = camera.nearPlane;
    defaults_.farPlane = farPlaneOverride > camera.nearPlane
                             ? farPlaneOverride
                             : camera.farPlane;
    // AnimCamera::SetClip (0x002f198c) forwards clipID to
    // CTimelineController::setClip (0x0042928c). The controller sets its
    // current time to the authored clip start and completion duration to
    // clipEnd - clipStart.
    clipId_ = clipId;
    clipStartMilliseconds_ = clip.startMilliseconds;
    clipEndMilliseconds_ = clip.endMilliseconds;
    return Result::success();
}

CameraPose CinematicCamera::sample(
    std::uint32_t timestampMilliseconds) const noexcept {
    CameraPose result = defaults_;
    if (!valid()) {
        return result;
    }
    const std::uint32_t localTimestamp =
        std::min(timestampMilliseconds, clipDurationMilliseconds());
    const std::uint32_t animationTimestamp =
        clipStartMilliseconds_ + localTimestamp;
    result.position = vectorFrom(positionTrack_->sample(animationTimestamp));
    result.target = vectorFrom(targetTrack_->sample(animationTimestamp));
    return result;
}

} // namespace usm::game
