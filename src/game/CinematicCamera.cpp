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

Result CinematicCamera::bind(
    const assets::ColladaAnimationFile& animation, float farPlaneOverride) {
    positionTrack_ = nullptr;
    targetTrack_ = nullptr;
    defaults_ = {};
    if (!animation.camera()) {
        return Result::failure("Cinematic BDAE has no camera definition");
    }
    const assets::ColladaCamera& camera = *animation.camera();
    if (camera.orthographic) {
        return Result::failure("Orthographic cinematic cameras are unsupported");
    }

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
    return Result::success();
}

CameraPose CinematicCamera::sample(
    std::uint32_t timestampMilliseconds) const noexcept {
    CameraPose result = defaults_;
    if (!valid()) {
        return result;
    }
    result.position = vectorFrom(positionTrack_->sample(timestampMilliseconds));
    result.target = vectorFrom(targetTrack_->sample(timestampMilliseconds));
    return result;
}

} // namespace usm::game
