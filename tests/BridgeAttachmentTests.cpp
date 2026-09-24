#include "game/BridgeAttachmentRuntime.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

bool near(float left, float right, float epsilon = 1.0e-4F) {
    return std::abs(left - right) <= epsilon;
}

} // namespace

int main() {
    try {
        using usm::assets::Quaternion;
        using usm::assets::Vector3;
        using usm::game::bridgeAttachmentInFootprint;
        using usm::game::bridgeAttachmentPose;

        const Vector3 bridge{100.0F, 200.0F, 300.0F};
        require(bridgeAttachmentInFootprint(
                    8, 8, bridge, 50.0F, 25.0F,
                    {50.0F, 175.0F, -999.0F}),
                "native bridge footprint must include its minimum XY edge");
        require(bridgeAttachmentInFootprint(
                    8, 8, bridge, 50.0F, 25.0F,
                    {150.0F, 225.0F, 999.0F}),
                "native bridge footprint must include its maximum XY edge");
        require(!bridgeAttachmentInFootprint(
                    8, 9, bridge, 50.0F, 25.0F,
                    {100.0F, 200.0F, 300.0F}),
                "GetSlideCarList must not attach an object from another room");
        require(!bridgeAttachmentInFootprint(
                    8, 8, bridge, 50.0F, 25.0F,
                    {150.01F, 200.0F, 300.0F}),
                "GetSlideCarList must reject an object outside bridge X");
        require(!bridgeAttachmentInFootprint(
                    8, 8, bridge, 50.0F, 25.0F,
                    {100.0F, 225.01F, 300.0F}),
                "GetSlideCarList must reject an object outside bridge Y");

        const Quaternion identity{};
        const auto flat = bridgeAttachmentPose(
            bridge, identity, 12.5F,
            {120.0F, 205.0F, 17.0F}, identity);
        require(near(flat.position.x, 120.0F) &&
                    near(flat.position.y, 205.0F) &&
                    near(flat.position.z, 312.5F),
                "flat bridge attachment must sit on the bridge top plane");
        require(near(flat.rotation.x, 0.0F) &&
                    near(flat.rotation.y, 0.0F) &&
                    near(flat.rotation.z, 0.0F) &&
                    near(flat.rotation.w, 1.0F),
                "identity bridge must preserve attachment rotation");

        constexpr float s = 0.7071067811865475F;
        const Quaternion bridgeQuarterTurnY{0.0F, s, 0.0F, s};
        const Quaternion attachmentQuarterTurnZ{0.0F, 0.0F, s, s};
        const auto tilted = bridgeAttachmentPose(
            {10.0F, 20.0F, 30.0F}, bridgeQuarterTurnY, 5.0F,
            {12.0F, 24.0F, -100.0F}, attachmentQuarterTurnZ);
        // Native state 4 projects the attachment base onto the live bridge
        // top using the rotated +X axis. +90 degrees around Y maps +X to -Z,
        // placing this point two centimeters below the bridge center top.
        require(near(tilted.position.x, 12.0F) &&
                    near(tilted.position.y, 24.0F) &&
                    near(tilted.position.z, 33.0F),
                "tilted bridge attachment must follow native top-plane math");
        require(near(tilted.rotation.x, 0.5F) &&
                    near(tilted.rotation.y, 0.5F) &&
                    near(tilted.rotation.z, 0.5F) &&
                    near(tilted.rotation.w, 0.5F),
                "bridge rotation must premultiply authored attachment rotation");

        std::cout << "PASS native broken-bridge attachment math\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL bridge attachment parity: " << error.what() << '\n';
        return 1;
    }
}
