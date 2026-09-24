#include "game/BridgeAttachmentRuntime.hpp"

#include <cmath>
#include <limits>

namespace usm::game {
namespace {

assets::Quaternion multiply(assets::Quaternion left,
                            assets::Quaternion right) noexcept {
    return {
        left.w * right.x + left.x * right.w + left.y * right.z -
            left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w +
            left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x +
            left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y -
            left.z * right.z};
}

assets::Vector3 rotate(assets::Quaternion rotation,
                       assets::Vector3 value) noexcept {
    const float lengthSquared = rotation.x * rotation.x +
                                rotation.y * rotation.y +
                                rotation.z * rotation.z +
                                rotation.w * rotation.w;
    if (lengthSquared <= std::numeric_limits<float>::epsilon()) {
        return value;
    }
    const assets::Quaternion inverse{-rotation.x / lengthSquared,
                                     -rotation.y / lengthSquared,
                                     -rotation.z / lengthSquared,
                                     rotation.w / lengthSquared};
    const auto rotated =
        multiply(multiply(rotation, {value.x, value.y, value.z, 0.0F}),
                 inverse);
    return {rotated.x, rotated.y, rotated.z};
}

} // namespace

bool bridgeAttachmentInFootprint(
    std::int32_t bridgeRoomId, std::int32_t attachmentRoomId,
    const assets::Vector3& bridgePosition, float halfWidth, float halfDepth,
    const assets::Vector3& attachmentPosition) noexcept {
    if (bridgeRoomId != attachmentRoomId) {
        return false;
    }
    return attachmentPosition.x >= bridgePosition.x - halfWidth &&
           attachmentPosition.x <= bridgePosition.x + halfWidth &&
           attachmentPosition.y >= bridgePosition.y - halfDepth &&
           attachmentPosition.y <= bridgePosition.y + halfDepth;
}

BridgeAttachmentPose bridgeAttachmentPose(
    const assets::Vector3& bridgePosition,
    const assets::Quaternion& bridgeRotation, float bridgeHalfHeight,
    const assets::Vector3& attachmentPosition,
    const assets::Quaternion& attachmentInitialRotation) noexcept {
    BridgeAttachmentPose pose;
    pose.position = attachmentPosition;
    const assets::Vector3 positiveX =
        rotate(bridgeRotation, {1.0F, 0.0F, 0.0F});
    pose.position.z =
        bridgePosition.z + bridgeHalfHeight -
        (bridgePosition.x - attachmentPosition.x) * positiveX.z;
    pose.rotation = multiply(bridgeRotation, attachmentInitialRotation);
    return pose;
}

} // namespace usm::game
