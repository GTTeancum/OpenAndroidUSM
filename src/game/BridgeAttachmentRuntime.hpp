#pragma once

#include "assets/IrrScene.hpp"

#include <cstdint>

namespace usm::game {

struct BridgeAttachmentPose {
    assets::Vector3 position;
    assets::Quaternion rotation;
};

// CBrokenBridge::GetSlideCarList (0x00301da0) performs one room-local,
// inclusive XY footprint test against the bridge rectangle for both CSlideCar
// and CAreaDamage bodies.
[[nodiscard]] bool bridgeAttachmentInFootprint(
    std::int32_t bridgeRoomId, std::int32_t attachmentRoomId,
    const assets::Vector3& bridgePosition, float halfWidth, float halfDepth,
    const assets::Vector3& attachmentPosition) noexcept;

// CBrokenBridge state 4 carries linked bodies on the tilting first section.
// Their base follows the bridge top plane and their authored rotation is
// premultiplied by the live bridge rotation.
[[nodiscard]] BridgeAttachmentPose bridgeAttachmentPose(
    const assets::Vector3& bridgePosition,
    const assets::Quaternion& bridgeRotation, float bridgeHalfHeight,
    const assets::Vector3& attachmentPosition,
    const assets::Quaternion& attachmentInitialRotation) noexcept;

} // namespace usm::game
