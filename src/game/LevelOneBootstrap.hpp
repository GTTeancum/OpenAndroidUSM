#pragma once

#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/CinematicCamera.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace usm::game {

struct CinematicActorAsset {
    std::int32_t objectId{-1};
    std::string sceneNodeName;
    std::uint32_t animationStartMilliseconds{};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    // Serialized Irrlicht absolute transform. Keeping the complete matrix is
    // required for actors parented to the level root, not just its translation.
    std::array<float, 16> worldTransform{};
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animation;
};

class LevelOneBootstrap final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);

    [[nodiscard]] const assets::IrrScene& mainScene() const noexcept {
        return mainScene_;
    }
    [[nodiscard]] const assets::IrrScene& firstRoom() const noexcept {
        return firstRoom_;
    }
    [[nodiscard]] const assets::ColladaGeometry& previewGeometry() const noexcept {
        return roomGeometry_.geometries().front();
    }
    [[nodiscard]] const assets::BtexTexture& previewTexture() const noexcept {
        return roomTextures_.front();
    }
    [[nodiscard]] const assets::ColladaMeshFile& roomGeometry() const noexcept {
        return roomGeometry_;
    }
    [[nodiscard]] const std::vector<assets::BtexTexture>& roomTextures() const
        noexcept {
        return roomTextures_;
    }
    [[nodiscard]] const CinematicScript& introStartScript() const noexcept {
        return introStartScript_;
    }
    [[nodiscard]] const CinematicScript& introScript() const noexcept {
        return introScript_;
    }
    [[nodiscard]] const CinematicScript& introEndScript() const noexcept {
        return introEndScript_;
    }
    [[nodiscard]] const assets::ColladaAnimationFile& introCameraAnimation()
        const noexcept {
        return introCameraAnimation_;
    }
    [[nodiscard]] const CinematicCamera& introCamera() const noexcept {
        return introCamera_;
    }
    [[nodiscard]] const std::vector<CinematicActorAsset>& introActors() const
        noexcept {
        return introActors_;
    }

private:
    assets::IrrScene mainScene_;
    assets::IrrScene firstRoom_;
    assets::ColladaMeshFile roomGeometry_;
    std::vector<assets::BtexTexture> roomTextures_;
    CinematicScript introStartScript_;
    CinematicScript introScript_;
    CinematicScript introEndScript_;
    assets::ColladaAnimationFile introCameraAnimation_;
    CinematicCamera introCamera_;
    std::vector<CinematicActorAsset> introActors_;
};

} // namespace usm::game
