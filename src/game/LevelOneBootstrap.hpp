#pragma once

#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/AttackConfig.hpp"
#include "game/CinematicCamera.hpp"
#include "game/EnemySpecialActionConfig.hpp"
#include "game/GameplayCamera.hpp"

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
    assets::ColladaMeshFile collision;
    assets::ColladaMeshFile navigationMesh;
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
    float health{1000.0F};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
};

struct LevelTriggerAsset {
    std::int32_t objectId{-1};
    std::string name;
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    assets::Vector3 sizes;
    bool orientedBox{};
    bool enabled{};
    bool autoDisabled{};
    std::int32_t outToInCinematicId{-1};
    std::int32_t inToOutCinematicId{-1};
    std::int32_t whileInsideCinematicId{-1};
    std::int32_t whileOutsideCinematicId{-1};
};

struct LevelCinematicAsset {
    std::int32_t objectId{-1};
    std::string name;
    std::string scriptFile;
    CinematicScript script;
};

struct EnemyArchetypeAsset {
    std::string gameType;
    std::string meshFile;
    std::string animationFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
};

struct LevelEnemyAsset {
    std::int32_t objectId{-1};
    std::string name;
    std::string gameType;
    std::string initialAnimation;
    std::int16_t enemyTypeId{-1};
    std::size_t archetypeIndex{};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    float health{};
    bool visible{true};
    bool aiEnabled{};
    bool waitSpawn{};
    float lineSpeedCentimetersPerMillisecond{};
    float awarenessRadius{};
    float awarenessAngleDegrees{};
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
    [[nodiscard]] const std::vector<CameraArea>& cameraAreas() const noexcept {
        return cameraAreas_;
    }
    [[nodiscard]] const std::vector<LevelTriggerAsset>& triggers() const noexcept {
        return triggers_;
    }
    [[nodiscard]] const std::vector<LevelCinematicAsset>& cinematics() const
        noexcept {
        return cinematics_;
    }
    [[nodiscard]] const std::vector<EnemyArchetypeAsset>& enemyArchetypes() const
        noexcept {
        return enemyArchetypes_;
    }
    [[nodiscard]] const std::vector<LevelEnemyAsset>& enemies() const noexcept {
        return enemies_;
    }
    [[nodiscard]] const AttackConfigDatabase& attackConfigs() const noexcept {
        return attackConfigs_;
    }
    [[nodiscard]] const EnemySpecialActionConfigDatabase&
    enemySpecialActions() const noexcept {
        return enemySpecialActions_;
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
    std::vector<CameraArea> cameraAreas_;
    std::vector<LevelTriggerAsset> triggers_;
    std::vector<LevelCinematicAsset> cinematics_;
    std::vector<EnemyArchetypeAsset> enemyArchetypes_;
    std::vector<LevelEnemyAsset> enemies_;
    AttackConfigDatabase attackConfigs_;
    EnemySpecialActionConfigDatabase enemySpecialActions_;
};

} // namespace usm::game
