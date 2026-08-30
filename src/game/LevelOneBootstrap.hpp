#pragma once

#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/DdsAtcTexture.hpp"
#include "assets/IrrScene.hpp"
#include "assets/SpriteAtlas.hpp"
#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/AttackConfig.hpp"
#include "game/ButtonConfig.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicCameraTrack.hpp"
#include "game/EnemyBehaviorConfig.hpp"
#include "game/EnemyAttributeConfig.hpp"
#include "game/EnemyAttackIntervalConfig.hpp"
#include "game/EnemyRangeAttackConfig.hpp"
#include "game/EnemySpecialActionConfig.hpp"
#include "game/GameplayCamera.hpp"
#include "game/LocalizedStringTable.hpp"

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
    std::string sceneFile;
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

// Authored path node consumed by Player slide/forced-traversal states. These
// names mirror the serialized editor attributes and the recovered WayPoint
// runtime instead of collapsing the graph to an anonymous position list.
struct LevelWayPointAsset {
    std::int32_t objectId{-1};
    std::string name;
    assets::Vector3 position;
    bool enabled{true};
    bool electricShock{};
    std::array<std::int32_t, 2> nextWaypointIds{{-1, -1}};
    bool useGravityWhenEnd{true};
    bool unstandable{};
    std::int32_t jumpDirection{};
    float timeToMe{};
    std::int32_t linkedCameraAreaId{-1};
};

// CWebGrabPoint fields recovered from ProcessUserAttr (0x00327320) and Init
// (0x00327470). The linked CamCtrlPoint supplies the swing-plane direction;
// an optional WayPoint supplies the forced exit destination.
struct LevelWebGrabPointAsset {
    std::int32_t objectId{-1};
    assets::Vector3 position;
    std::int32_t directionControlPointId{-1};
    assets::Vector3 direction;
    float length{};
    float visibleLength{-1.0F};
    float verticalAngleDegrees{};
    float horizontalAngleDegrees{};
    float exitSpeed{};
    bool cannotControl{};
    std::int32_t targetWaypointId{-1};
    bool hasTargetWaypoint{};
    assets::Vector3 targetWaypointPosition;
    std::int32_t targetSlideId{-1};
};

// CSlider::ProcessUserAttr (0x0031ce6c) stores the authored entry WayPoint,
// Enabled flag, and electric-shock flag. Init (0x0031e7cc) follows the first
// WayPoint link to materialize the ordered segment graph.
struct LevelSlideAsset {
    std::int32_t objectId{-1};
    std::string name;
    assets::Vector3 position;
    std::int32_t linkedWaypointId{-1};
    bool enabled{true};
    bool electricShock{};
    std::vector<std::int32_t> waypointIds;
};

struct LevelCinematicAsset {
    std::int32_t objectId{-1};
    std::string name;
    std::string scriptFile;
    bool scriptAvailable{};
    CinematicScript script;
    CinematicCameraTrack cameraTrack;
    std::string cameraAnimationFile;
    assets::ColladaAnimationFile cameraAnimation;
    CinematicCamera animatedCamera;
    std::vector<CinematicActorAsset> actors;
    std::uint32_t colladaDurationMilliseconds{};
    std::int32_t nextCinematicId{-1};
    bool levelEndAfterPlayback{};
    bool gameEndAfterPlayback{};

    [[nodiscard]] bool hasColladaPlayback() const noexcept {
        return animatedCamera.valid();
    }
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

struct LevelHudAsset {
    assets::SpriteAtlas interfaceAtlas;
    assets::DdsAtcTexture interfaceTexture;
};

class LevelOneBootstrap final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);

    [[nodiscard]] const assets::IrrScene& mainScene() const noexcept {
        return mainScene_;
    }
    [[nodiscard]] const assets::IrrScene& firstRoom() const noexcept {
        return rooms_.front().scene;
    }
    [[nodiscard]] const assets::ColladaGeometry& previewGeometry() const noexcept {
        return rooms_.front().geometry.geometries().front();
    }
    [[nodiscard]] const assets::BtexTexture& previewTexture() const noexcept {
        return rooms_.front().textures.front();
    }
    [[nodiscard]] const assets::ColladaMeshFile& roomGeometry() const noexcept {
        return rooms_.front().geometry;
    }
    [[nodiscard]] const std::vector<assets::BtexTexture>& roomTextures() const
        noexcept {
        return rooms_.front().textures;
    }
    [[nodiscard]] const std::vector<LevelRoomAsset>& rooms() const noexcept {
        return rooms_;
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
    [[nodiscard]] const std::vector<LevelWayPointAsset>& waypoints() const
        noexcept {
        return waypoints_;
    }
    [[nodiscard]] const std::vector<LevelWebGrabPointAsset>& webGrabPoints()
        const noexcept {
        return webGrabPoints_;
    }
    [[nodiscard]] const std::vector<LevelSlideAsset>& slides() const noexcept {
        return slides_;
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
    [[nodiscard]] const ButtonConfigDatabase& buttonConfigs() const noexcept {
        return buttonConfigs_;
    }
    [[nodiscard]] const EnemySpecialActionConfigDatabase&
    enemySpecialActions() const noexcept {
        return enemySpecialActions_;
    }
    [[nodiscard]] const EnemyBehaviorConfigDatabase& enemyBehaviorConfigs()
        const noexcept {
        return enemyBehaviorConfigs_;
    }
    [[nodiscard]] const EnemyRangeAttackConfigDatabase&
    enemyRangeAttackConfigs() const noexcept {
        return enemyRangeAttackConfigs_;
    }
    [[nodiscard]] const EnemyAttributeConfigDatabase& enemyAttributeConfigs()
        const noexcept {
        return enemyAttributeConfigs_;
    }
    [[nodiscard]] const EnemyAttackIntervalConfigDatabase&
    enemyAttackIntervalConfigs() const noexcept {
        return enemyAttackIntervalConfigs_;
    }
    [[nodiscard]] const LevelHudAsset& hud() const noexcept { return hud_; }
    [[nodiscard]] const LevelTextCatalog& textCatalog() const noexcept {
        return textCatalog_;
    }

private:
    assets::IrrScene mainScene_;
    std::vector<LevelRoomAsset> rooms_;
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
    std::vector<LevelWayPointAsset> waypoints_;
    std::vector<LevelWebGrabPointAsset> webGrabPoints_;
    std::vector<LevelSlideAsset> slides_;
    std::vector<LevelCinematicAsset> cinematics_;
    std::vector<EnemyArchetypeAsset> enemyArchetypes_;
    std::vector<LevelEnemyAsset> enemies_;
    AttackConfigDatabase attackConfigs_;
    ButtonConfigDatabase buttonConfigs_;
    EnemySpecialActionConfigDatabase enemySpecialActions_;
    EnemyBehaviorConfigDatabase enemyBehaviorConfigs_;
    EnemyAttributeConfigDatabase enemyAttributeConfigs_;
    EnemyAttackIntervalConfigDatabase enemyAttackIntervalConfigs_;
    EnemyRangeAttackConfigDatabase enemyRangeAttackConfigs_;
    LevelHudAsset hud_;
    LevelTextCatalog textCatalog_;
};

} // namespace usm::game
