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

struct LevelRoomAsset {
    std::string name;
    assets::IrrScene scene;
    assets::ColladaMeshFile geometry;
    std::vector<assets::BtexTexture> textures;
};

struct LevelStaticMeshAsset {
    std::string name;
    assets::ColladaMeshFile geometry;
    std::vector<assets::BtexTexture> textures;
    bool cameraRelative{};
};

struct LevelPlayerAsset {
    std::int32_t objectId{-1};
    std::string sceneNodeName;
    std::string initialAnimation;
    std::int32_t initialCameraAreaId{-1};
    std::int32_t linkedCinematicId{-1};
    std::int32_t endGameCinematicId{-1};
    bool hasCollision{};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
};

class LevelOneBootstrap final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);

    [[nodiscard]] const assets::IrrScene& mainScene() const noexcept {
        return mainScene_;
    }
    [[nodiscard]] const assets::IrrScene& firstRoom() const noexcept {
        return introRooms_.front().scene;
    }
    [[nodiscard]] const assets::ColladaGeometry& previewGeometry() const noexcept {
        return introRooms_.front().geometry.geometries().front();
    }
    [[nodiscard]] const assets::BtexTexture& previewTexture() const noexcept {
        return introRooms_.front().textures.front();
    }
    [[nodiscard]] const assets::ColladaMeshFile& roomGeometry() const noexcept {
        return introRooms_.front().geometry;
    }
    [[nodiscard]] const std::vector<assets::BtexTexture>& roomTextures() const
        noexcept {
        return introRooms_.front().textures;
    }
    [[nodiscard]] const std::vector<LevelRoomAsset>& introRooms() const
        noexcept {
        return introRooms_;
    }
    [[nodiscard]] const LevelStaticMeshAsset& introSky() const noexcept {
        return introSky_;
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
    [[nodiscard]] const LevelPlayerAsset& player() const noexcept {
        return player_;
    }

private:
    assets::IrrScene mainScene_;
    std::vector<LevelRoomAsset> introRooms_;
    LevelStaticMeshAsset introSky_;
    CinematicScript introStartScript_;
    CinematicScript introScript_;
    CinematicScript introEndScript_;
    assets::ColladaAnimationFile introCameraAnimation_;
    CinematicCamera introCamera_;
    std::vector<CinematicActorAsset> introActors_;
    LevelPlayerAsset player_;
};

} // namespace usm::game
