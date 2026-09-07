#pragma once

#include "assets/AnimationDisplacement.hpp"
#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/DdsAtcTexture.hpp"
#include "assets/IrrScene.hpp"
#include "assets/SpriteAtlas.hpp"
#include "assets/TgaTexture.hpp"
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
#include "game/EffectPreset.hpp"
#include "game/GameplayCamera.hpp"
#include "game/LocalizedStringTable.hpp"
#include "game/PlayerHitEffectConfig.hpp"
#include "game/QuickTimeActionConfig.hpp"

#include <algorithm>
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
    std::int32_t animationClipId{-1};
    std::uint32_t animationClipStartMilliseconds{};
    std::uint32_t animationClipEndMilliseconds{};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    // Serialized Irrlicht absolute transform. Keeping the complete matrix is
    // required for actors parented to the level root, not just its translation.
    std::array<float, 16> worldTransform{};
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animation;

    [[nodiscard]] std::uint32_t animationClipDurationMilliseconds() const
        noexcept {
        return animationClipEndMilliseconds -
               animationClipStartMilliseconds;
    }
    [[nodiscard]] std::uint32_t animationTimestamp(
        std::uint32_t cinematicTimestampMilliseconds) const noexcept {
        const std::uint32_t localTimestamp =
            cinematicTimestampMilliseconds <= animationStartMilliseconds
                ? 0
                : cinematicTimestampMilliseconds -
                      animationStartMilliseconds;
        return animationClipStartMilliseconds +
               std::min(localTimestamp,
                        animationClipDurationMilliseconds());
    }
};

struct LevelRoomAsset {
    // CRoom::ProcessMovingAttributes (0x0036d49c) reads these fields from the
    // room's authored Geometry node. The geometry ID, rather than the
    // one-based room index, is what cinematic room commands address.
    std::int32_t objectId{-1};
    std::string name;
    std::string sceneFile;
    assets::Vector3 position;
    bool motionInitiallyActive{};
    float lineSpeedCentimetersPerMillisecond{};
    std::int32_t linkedWaypointId{-1};
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
    assets::AnimationDisplacement animationDisplacement;
};

struct LevelTriggerAsset {
    std::int32_t objectId{-1};
    std::string name;
    // CTrigger is a CRoom child. Preserve its one-based room ownership so
    // automatic activation can follow CRoom::SetVisible like the original.
    std::int32_t roomId{-1};
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
    std::int32_t roomId{};
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
    std::int32_t roomId{};
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
    std::int32_t roomId{};
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
    std::uint32_t cameraAnimationStartMilliseconds{};
    std::vector<CinematicActorAsset> actors;
    std::uint32_t colladaDurationMilliseconds{};
    std::int32_t nextCinematicId{-1};
    bool levelEndAfterPlayback{};
    bool gameEndAfterPlayback{};

    [[nodiscard]] bool hasColladaPlayback() const noexcept {
        return animatedCamera.valid();
    }
    [[nodiscard]] std::uint32_t cameraAnimationTimestamp(
        std::uint32_t cinematicTimestampMilliseconds) const noexcept {
        return cinematicTimestampMilliseconds <=
                       cameraAnimationStartMilliseconds
                   ? 0
                   : cinematicTimestampMilliseconds -
                         cameraAnimationStartMilliseconds;
    }
};

struct EnemyArchetypeAsset {
    std::string gameType;
    std::string meshFile;
    std::string animationFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
    // DisplacementAnimation::createAnimationControl (0x003902f8) binds the
    // exported Dummy/Pelvis streams beside every character animation bank.
    // Unit consumes Dummy as physical root motion and pelvis-minus-Dummy as
    // the scene-node offset; keeping these with the shared archetype mirrors
    // the native ownership and prevents hurt poses from separating visually
    // from their damage capsule.
    assets::AnimationDisplacement animationDisplacement;
};

// CThrowObject::LoadDefaultMesh (0x00358e78) loads this exact BDAE for the
// subtype-1 object allocated by weapon type 5. The file carries both the
// rendered bottle mesh and its impact animation bank.
struct MolotovProjectileAsset {
    std::string meshFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
};

// CBullet::setType(0) (0x0035c444) loads this exact BDAE for Spider-Man's
// ordinary motion-123 web shot. Unlike the tentacle variant it does not
// select an animation, so the authored scene geometry is rendered directly.
struct WebPelletProjectileAsset {
    std::string meshFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
};

// CLevel::InitAllWebLines (0x0037faa4) assigns this standalone texture to all
// four pooled CobWeb/CTexLineSceneNode instances.
struct WebLineAsset {
    std::string textureFile;
    assets::BtexTexture texture;
};

// CBoomerang's constructors at 0x0035b3fc/0x0035b604 load this exact BDAE.
// The instruction sequence at 0x0035b560/0x0035b720 then calls SetAnim with
// the native string at 0x004e538a: `weapons`, loop=true, mode=0.
struct BoomerangProjectileAsset {
    std::string meshFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
};

// Shared animated models constructed by Electro's native behaviors. The
// addresses are retained at the use sites because the same three BDAEs are
// combined differently by Thunderclap, Rotate, Weak, and Electro Dash.
struct ElectroEffectModelAsset {
    std::string meshFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
};

struct ElectroEffectAsset {
    ElectroEffectModelAsset wave;
    ElectroEffectModelAsset waveBillboard;
    ElectroEffectModelAsset beam;
};

// CBehaviorHurt state 0x45 throws these two packaged animated-object effects
// at the ground manifold point when an air-kickdown victim lands.
struct EnemyLandingEffectAsset {
    ElectroEffectModelAsset shockwave;
    ElectroEffectModelAsset crashWall;
};

struct LevelEnemyAsset {
    std::int32_t objectId{-1};
    std::string name;
    std::string gameType;
    std::string initialAnimation;
    std::int16_t enemyTypeId{-1};
    std::size_t archetypeIndex{};
    // CRoom::SetVisible addresses rooms through a one-based offset. Preserve
    // that ownership so render visibility applies to child enemies too.
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    float health{};
    bool visible{true};
    bool aiEnabled{};
    bool waitSpawn{};
    // CEnemy::ProcessUserAttr (0x00332870) selects wall/air state families
    // independently of enemy type and preserves the authored movement gate.
    bool onWall{};
    bool inAir{};
    bool immobile{};
    float lineSpeedCentimetersPerMillisecond{};
    float awarenessRadius{};
    float awarenessAngleDegrees{};
};

enum class LevelObjectKind {
    Animated,
    Destroyable,
    Comic,
    Car,
    DropObject,
    SpiderWebWall,
    StaticObject,
    Hostage,
    StreamPiping,
    SlideCar,
    Platform,
    ElectricPlatform,
    Train,
    BrokenBridge,
    AreaDamage,
};

// Shared render data for mesh-bearing room objects. Object instances retain
// their authored IDs and transforms separately so cinematic commands can
// address them without coupling portable gameplay state to D3D resources.
struct LevelObjectArchetypeAsset {
    std::string meshFile;
    std::string animationFile;
    assets::ColladaMeshFile mesh;
    std::vector<assets::BtexTexture> textures;
    assets::ColladaAnimationFile animationBank;
    // CLevel::LoadNextObject (0x003853fc) applies selected runtime material
    // overrides after constructing an object's Collada scene. The electric
    // platform branch passes "electric_wr" to
    // SetMaterialAdditiveByTexName (0x00373838), which changes only scene
    // nodes whose layer-zero texture name contains that fragment.
    std::string additiveTextureNameFragment;
    // CPlatForm::Init (0x00318964) constructs this independent transmission
    // mesh for collision even when the authored IRR node has Collision=false.
    std::string physicsMeshFile;
    assets::ColladaMeshFile physicsMesh;
};

struct LevelObjectAsset {
    std::int32_t objectId{-1};
    std::string name;
    std::string gameType;
    LevelObjectKind kind{LevelObjectKind::StaticObject};
    std::string initialAnimation;
    bool initialAnimationLoops{true};
    std::size_t archetypeIndex{};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    bool visible{true};
    bool hasCollision{};
    // CAnimatedObject::ProcessUserAttr (0x002fd560) maps AddColor to native
    // material type 0x0d (GL_SRC_ALPHA, GL_ONE) for the whole scene node.
    bool additiveBlend{};
    float collisionRadius{};
    float health{};
    float damageRadius{};
    float damage{};
    std::string destructionEffectType;
    std::int32_t deadSpawnObjectId{-1};
    std::int32_t deadCinematicId{-1};
    std::int32_t hitVoxSoundId{-1};
    bool attackable{};
    bool collisionAfterDestruction{};
    std::int32_t comicIndex{-1};
    std::string comicLevelStringId;
    assets::Vector3 comicCollectionMinimum;
    assets::Vector3 comicCollectionMaximum;
    // CHostage::ProcessUserAttr (0x00338724) owns four named animation
    // phases plus the rescue-control radii/rewards. They are retained on the
    // authored object instead of being inferred from clip order at runtime.
    std::array<std::string, 4> hostageAnimations;
    float hostageEnableRadius{};
    std::int32_t hostageHealthOrbCount{};
    std::int32_t hostageSkillPointOrbCount{};
    float hostageHintHeight{180.0F};
    float hostageButtonHeight{85.0F};
    bool hostageIsWoman{};
    // CElectricPlatForm (type 0x29) embeds CElectriferous. These fields are
    // read verbatim by CElectriferous::ProcessUserAttr (0x0030d6f8) and
    // CPlatForm/CWayPointMover at 0x0031874c/0x003269f0.
    float electricOffDurationMilliseconds{};
    float electricOnDurationMilliseconds{};
    float electricReadyDurationMilliseconds{};
    float electricDelayMilliseconds{};
    float electricDamage{};
    bool electricInitiallyActive{};
    std::int32_t electricInitialState{1};
    float platformParkDurationMilliseconds{};
    float platformLineSpeedCentimetersPerMillisecond{};
    bool platformInitiallyActive{};
    bool platformActiveForever{};
    std::int32_t platformLinkedWaypointId{-1};
    // CTrain derives from CWayPointMover. ProcessUserAttr/InitLinker at
    // 0x00321174/0x0032146c retain these authored movement and carriage links.
    float trainLineSpeedCentimetersPerMillisecond{};
    bool trainInitiallyActive{};
    std::int32_t trainLinkedWaypointId{-1};
    std::int32_t trainPreviousObjectId{-1};
    std::int32_t trainNextObjectId{-1};
    float trainLifeDurationMilliseconds{};
    bool trainCanTransport{};
    bool trainKillsPlayer{};
    // CBrokenBridge::ProcessUserAttr (0x0030081c), times in seconds.
    std::int32_t bridgeType{};
    float bridgeIdleShakeSeconds{1.0F};
    float bridgeDropShakeSeconds{1.0F};
    float bridgeDropDistance{};
    float bridgeDropAngleDegrees{};
    float bridgeDropSeconds{};
    float bridgeSecondShakeSeconds{1.0F};
    float bridgeSecondDropDistance{};
    float bridgeSecondAngleDegrees{};
    float bridgeSecondDropSeconds{};
    float bridgeActivationDistance{10.0F};
    float bridgeCarRunSpeed{500.0F};
    // CAreaDamage::ProcessUserAttr (0x003028d4). These are distinct from
    // CEffectDamage's invisible volume assets: AreaDamage owns a rendered
    // animated scene object and alternates its active animation with an
    // optional randomized wait.
    float areaDamageBeginDelayMilliseconds{};
    float areaDamageRandomLowMilliseconds{};
    float areaDamageRandomHighMilliseconds{};
    std::int32_t areaDamageType{};
    bool areaDamageIgnorePhysics{};
    bool areaDamageActiveForever{};
    bool areaDamageAutomaticDetection{true};
    assets::Vector3 collisionLocalMinimum;
    assets::Vector3 collisionLocalMaximum;
    bool hasCollisionBounds{};
};

struct LevelHudAsset {
    assets::SpriteAtlas interfaceAtlas;
    assets::DdsAtcTexture interfaceTexture;
    assets::SpriteAtlas tutorialAtlas;
    assets::DdsAtcTexture tutorialTexture;
    assets::SpriteAtlas transportAtlas;
    assets::DdsAtcTexture transportTexture;
    assets::SpriteAtlas mainMenuAtlas;
    assets::DdsAtcTexture mainMenuTexture;
    assets::SpriteAtlas backgroundSuitAtlas;
    assets::TgaTexture backgroundSuitTexture;
    assets::SpriteAtlas normalWhiteFontAtlas;
    assets::DdsAtcTexture normalWhiteFontTexture;
    assets::SpriteAtlas outlineSmallFontAtlas;
    assets::DdsAtcTexture outlineSmallFontTexture;
    assets::SpriteAtlas outlineBigFontAtlas;
    assets::DdsAtcTexture outlineBigFontTexture;
};

struct LevelEffectAsset {
    EffectPresetDatabase presets;
    assets::SpriteAtlas atlas;
    assets::DdsAtcTexture texture;
};

struct PlayerHitEffectAsset {
    PlayerHitEffectDefinition definition;
    assets::ColladaMeshFile mesh;
    assets::ColladaAnimationFile animation;
    std::vector<assets::BtexTexture> textures;
};

// Room-owned CEffect nodes reconstructed from their $EffectType preset and
// absolute Irrlicht transform.
struct LevelEnvironmentEffectAsset {
    std::int32_t objectId{-1};
    std::string effectType;
    std::int32_t roomId{-1};
    assets::Vector3 position;
    bool visible{true};
};

enum class LevelBonusType : std::int32_t {
    Health = 0,
    WebPower = 1,
    SkillPoint = 2,
};

// CBonus is serialized as an empty room node. ProcessUserAttr
// (0x003937a8) creates the visible effect from these flags at runtime.
struct LevelBonusAsset {
    std::int32_t objectId{-1};
    LevelBonusType type{LevelBonusType::Health};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    bool visible{true};
};

// Authored world-space Hint billboard. Hint::ProcessUserAttr
// (0x0033da84) loads the sprite/animation and links it to another scene
// object; the only linked first-level instance is the spider-sense tutorial
// cue attached to Spider-Man.
struct LevelHintAsset {
    std::int32_t objectId{-1};
    std::int32_t linkedObjectId{-1};
    std::int32_t roomId{-1};
    std::int32_t animationIndex{};
    std::string spriteFile;
    assets::Vector3 position;
    bool visible{};
    assets::SpriteAtlas atlas;
    assets::DdsAtcTexture texture;
};

struct LevelDamageAsset {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    assets::Vector3 sizes;
    float damage{30.0F};
    std::int32_t damageType{};
    bool enabled{true};
};

struct LevelRestorePointAsset {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
};

// Authored CCheckPoint node. CCheckPoint::ProcessUserAttr (0x0036919c)
// retains the trigger dimensions and the three-way restart placement policy:
// a saved live player transform, this scene-node transform, or a linked
// waypoint. The absolute matrix is also the source for the optional OBB.
struct LevelCheckPointAsset {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    assets::Vector3 sizes;
    bool savePosition{true};
    bool enabled{true};
    bool orientedBox{};
    std::int32_t linkedWaypointId{-1};
};

struct LevelRestoreTriggerAsset {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    std::int32_t restorePointId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    assets::Vector3 sizes;
    float damage{};
    std::int32_t cinematicId{-1};
    bool fallAfterRestore{};
    bool useLastCheckpoint{};
    // CTriggerRestore::ProcessAttr (0x0036c170) inverts the serialized
    // AbsoluteTransformation and passes that complete matrix to obbox.
    // Retain it: Irrlicht's transposed quaternion convention cannot be
    // reproduced by treating Rotation as a conventional world quaternion.
    std::array<float, 16> worldTransform{};
};

struct LevelDropAreaAsset {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Vector3 sizes;
    std::string effectType;
};

struct LevelDropObjectAsset {
    std::int32_t objectId{-1};
    std::int32_t ownerAreaId{-1};
    std::int32_t roomId{-1};
    std::int32_t delayMilliseconds{};
    float damage{};
    assets::Vector3 position;
    assets::Vector3 halfExtents{50.0F, 50.0F, 50.0F};
    std::string effectType;
};

// Authored CTriggerSound volume. The native object starts a looping 2D Vox
// emitter while the player's collision box intersects this volume.
struct LevelTriggerSoundAsset {
    std::int32_t objectId{-1};
    std::string eventName;
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 16> worldTransform{};
    assets::Vector3 sizes;
    bool axisAlignedBox{};
};

class LevelOneBootstrap final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot,
                              std::uint32_t levelNumber = 1);

    [[nodiscard]] std::uint32_t levelNumber() const noexcept {
        return levelNumber_;
    }
    [[nodiscard]] bool hasIntroCinematic() const noexcept {
        return introCamera_.valid();
    }

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
    [[nodiscard]] std::uint32_t introColladaDurationMilliseconds() const
        noexcept {
        return introColladaDurationMilliseconds_;
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
    [[nodiscard]] const MolotovProjectileAsset& molotovProjectile() const
        noexcept {
        return molotovProjectile_;
    }
    [[nodiscard]] const WebPelletProjectileAsset& webPelletProjectile() const
        noexcept {
        return webPelletProjectile_;
    }
    [[nodiscard]] const WebLineAsset& webLine() const noexcept {
        return webLine_;
    }
    [[nodiscard]] const BoomerangProjectileAsset& boomerangProjectile() const
        noexcept {
        return boomerangProjectile_;
    }
    [[nodiscard]] const ElectroEffectAsset& electroEffects() const noexcept {
        return electroEffects_;
    }
    [[nodiscard]] const EnemyLandingEffectAsset& enemyLandingEffects() const
        noexcept {
        return enemyLandingEffects_;
    }
    [[nodiscard]] const std::vector<LevelObjectArchetypeAsset>&
    objectArchetypes() const noexcept {
        return objectArchetypes_;
    }
    [[nodiscard]] const std::vector<LevelObjectAsset>& objects() const noexcept {
        return objects_;
    }
    [[nodiscard]] const AttackConfigDatabase& attackConfigs() const noexcept {
        return attackConfigs_;
    }
    [[nodiscard]] const ButtonConfigDatabase& buttonConfigs() const noexcept {
        return buttonConfigs_;
    }
    [[nodiscard]] const QuickTimeActionConfigDatabase& quickTimeActionConfigs()
        const noexcept {
        return quickTimeActionConfigs_;
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
    [[nodiscard]] const LevelEffectAsset& effects() const noexcept {
        return effects_;
    }
    [[nodiscard]] const PlayerHitEffectConfigDatabase& playerHitEffectConfigs()
        const noexcept {
        return playerHitEffectConfigs_;
    }
    [[nodiscard]] const std::vector<PlayerHitEffectAsset>& playerHitEffects()
        const noexcept {
        return playerHitEffects_;
    }
    [[nodiscard]] const std::vector<LevelEnvironmentEffectAsset>&
    environmentEffects() const noexcept {
        return environmentEffects_;
    }
    [[nodiscard]] const std::vector<LevelBonusAsset>& bonuses() const noexcept {
        return bonuses_;
    }
    [[nodiscard]] const std::vector<LevelHintAsset>& hints() const noexcept {
        return hints_;
    }
    [[nodiscard]] const std::vector<LevelDamageAsset>& damageVolumes() const
        noexcept {
        return damageVolumes_;
    }
    [[nodiscard]] const std::vector<LevelRestoreTriggerAsset>&
    restoreTriggers() const noexcept {
        return restoreTriggers_;
    }
    [[nodiscard]] const std::vector<LevelRestorePointAsset>& restorePoints()
        const noexcept {
        return restorePoints_;
    }
    [[nodiscard]] const std::vector<LevelCheckPointAsset>& checkPoints()
        const noexcept {
        return checkPoints_;
    }
    [[nodiscard]] const std::vector<LevelDropAreaAsset>& dropAreas() const
        noexcept {
        return dropAreas_;
    }
    [[nodiscard]] const std::vector<LevelDropObjectAsset>& dropObjects() const
        noexcept {
        return dropObjects_;
    }
    [[nodiscard]] const std::vector<LevelTriggerSoundAsset>& triggerSounds()
        const noexcept {
        return triggerSounds_;
    }
    [[nodiscard]] const LevelTextCatalog& textCatalog() const noexcept {
        return textCatalog_;
    }

private:
    std::uint32_t levelNumber_{1};
    assets::IrrScene mainScene_;
    std::vector<LevelRoomAsset> rooms_;
    LevelStaticMeshAsset introSky_;
    CinematicScript introStartScript_;
    CinematicScript introScript_;
    CinematicScript introEndScript_;
    assets::ColladaAnimationFile introCameraAnimation_;
    CinematicCamera introCamera_;
    std::uint32_t introColladaDurationMilliseconds_{};
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
    WebPelletProjectileAsset webPelletProjectile_;
    WebLineAsset webLine_;
    MolotovProjectileAsset molotovProjectile_;
    BoomerangProjectileAsset boomerangProjectile_;
    ElectroEffectAsset electroEffects_;
    EnemyLandingEffectAsset enemyLandingEffects_;
    std::vector<LevelObjectArchetypeAsset> objectArchetypes_;
    std::vector<LevelObjectAsset> objects_;
    AttackConfigDatabase attackConfigs_;
    ButtonConfigDatabase buttonConfigs_;
    QuickTimeActionConfigDatabase quickTimeActionConfigs_;
    EnemySpecialActionConfigDatabase enemySpecialActions_;
    EnemyBehaviorConfigDatabase enemyBehaviorConfigs_;
    EnemyAttributeConfigDatabase enemyAttributeConfigs_;
    EnemyAttackIntervalConfigDatabase enemyAttackIntervalConfigs_;
    EnemyRangeAttackConfigDatabase enemyRangeAttackConfigs_;
    LevelHudAsset hud_;
    LevelEffectAsset effects_;
    PlayerHitEffectConfigDatabase playerHitEffectConfigs_;
    std::vector<PlayerHitEffectAsset> playerHitEffects_;
    std::vector<LevelEnvironmentEffectAsset> environmentEffects_;
    std::vector<LevelBonusAsset> bonuses_;
    std::vector<LevelHintAsset> hints_;
    std::vector<LevelDamageAsset> damageVolumes_;
    std::vector<LevelRestoreTriggerAsset> restoreTriggers_;
    std::vector<LevelRestorePointAsset> restorePoints_;
    std::vector<LevelCheckPointAsset> checkPoints_;
    std::vector<LevelDropAreaAsset> dropAreas_;
    std::vector<LevelDropObjectAsset> dropObjects_;
    std::vector<LevelTriggerSoundAsset> triggerSounds_;
    LevelTextCatalog textCatalog_;
};

} // namespace usm::game
