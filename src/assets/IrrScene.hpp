#pragma once

#include "core/Result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace usm::assets {

struct Vector3 {
    float x{};
    float y{};
    float z{};
};

struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{1.0F};
};

struct IrrSceneNode {
    std::int32_t id{-1};
    std::int32_t parentId{-1};
    Vector3 position;
    Quaternion rotation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> absoluteTransform{};
    bool visible{true};
    std::string sceneType;
    std::string name;
    std::string gameType;
    std::string meshFile;
    std::string animationFile;
    std::string initialAnimation;
    bool hasCollision{};
    std::int32_t initialCameraAreaId{-1};
    std::int32_t linkedCinematicId{-1};
    std::int32_t endGameCinematicId{-1};
    std::array<std::int32_t, 4> cameraControlPointIds{{-1, -1, -1, -1}};
    std::array<std::int32_t, 4> nextCameraAreaIds{{-1, -1, -1, -1}};
    std::array<std::uint32_t, 4> cameraAreaSwitchTimeUnits{};
    bool cameraAreaInverseNormal{};
    float cameraAreaHeight{};
    float cameraAreaZFollowRate{};
    bool cameraAreaDisabled{};
    Vector3 cameraDirection;
    float cameraDistance{};
    Vector3 cameraTargetOffset;
    float cameraTargetHeightOffset{};
    float cameraFarPlaneOffset{};
};

// Parser for the UTF-16 Irrlicht scene files stored in the level archives.
// Room files are XML fragments with multiple top-level <node> elements.
class IrrScene final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const std::vector<IrrSceneNode>& nodes() const noexcept {
        return nodes_;
    }
    [[nodiscard]] const IrrSceneNode* findNode(std::int32_t id) const noexcept;

private:
    std::vector<IrrSceneNode> nodes_;
};

} // namespace usm::assets
