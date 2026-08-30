#include "game/LevelOneBootstrap.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iterator>
#include <string_view>
#include <vector>

namespace usm::game {
namespace {

std::string normalizeArchivePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (path.starts_with("./")) {
        path.erase(0, 2);
    }
    constexpr std::string_view entityPrefix = "../entities/";
    if (path.starts_with(entityPrefix)) {
        path.erase(0, entityPrefix.size());
    }
    return path;
}

std::string_view userAttribute(const assets::IrrSceneNode& node,
                               std::string_view name) noexcept {
    const auto match = node.userAttributes.find(std::string(name));
    return match == node.userAttributes.end() ? std::string_view{}
                                               : match->second;
}

std::int32_t integerAttribute(const assets::IrrSceneNode& node,
                              std::string_view name,
                              std::int32_t fallback = -1) noexcept {
    const std::string_view value = userAttribute(node, name);
    std::int32_t parsed = fallback;
    const auto result = std::from_chars(value.data(), value.data() + value.size(),
                                        parsed);
    return result.ec == std::errc{} ? parsed : fallback;
}

float floatAttribute(const assets::IrrSceneNode& node, std::string_view name,
                     float fallback = 0.0F) noexcept {
    const std::string_view value = userAttribute(node, name);
    if (value.empty()) {
        return fallback;
    }
    std::string storage(value);
    char* end = nullptr;
    const float parsed = std::strtof(storage.c_str(), &end);
    return end == storage.c_str() ? fallback : parsed;
}

bool booleanAttribute(const assets::IrrSceneNode& node, std::string_view name,
                      bool fallback = false) noexcept {
    const std::string_view value = userAttribute(node, name);
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    return fallback;
}

assets::Vector3 vectorAttribute(const assets::IrrSceneNode& node,
                                std::string_view name) noexcept {
    std::string storage(userAttribute(node, name));
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Vector3 result;
    for (float* component : {&result.x, &result.y, &result.z}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return {};
        }
        cursor = end;
    }
    return result;
}

const assets::IrrSceneNode* findLevelNode(const assets::IrrScene& mainScene,
                                          const assets::IrrScene& firstRoom,
                                          std::int32_t id) noexcept {
    const assets::IrrSceneNode* node = mainScene.findNode(id);
    return node != nullptr ? node : firstRoom.findNode(id);
}

Result loadTextures(filesystem::GbmpArchive& primaryArchive,
                    filesystem::GbmpArchive* fallbackArchive,
                    const assets::ColladaMeshFile& mesh,
                    std::vector<assets::BtexTexture>& output,
                    std::string_view assetName) {
    output.clear();
    output.reserve(mesh.images().size());
    std::vector<std::byte> resource;
    for (const assets::ColladaImage& image : mesh.images()) {
        std::string source = image.sourcePath;
        std::replace(source.begin(), source.end(), '\\', '/');
        const std::string filename =
            std::filesystem::path(source).filename().string();
        while (source.starts_with("../")) {
            source.erase(0, 3);
        }
        const std::vector<std::string> candidates{
            "textures_bin/" + image.sourcePath,
            "textures_bin/" + filename,
            "textures/" + filename,
            source,
        };
        const auto primaryCandidate = std::find_if(
            candidates.begin(), candidates.end(),
            [&primaryArchive](const std::string& path) {
                return primaryArchive.find(path) != nullptr;
            });
        const auto fallbackCandidate =
            fallbackArchive == nullptr
                ? candidates.end()
                : std::find_if(
                      candidates.begin(), candidates.end(),
                      [fallbackArchive](const std::string& path) {
                          return fallbackArchive->find(path) != nullptr;
                      });
        if (primaryCandidate == candidates.end() &&
            fallbackCandidate == candidates.end()) {
            output.clear();
            return Result::failure("Could not locate " +
                                   std::string(assetName) + " texture " +
                                   image.sourcePath);
        }
        Result result = primaryCandidate != candidates.end()
                            ? primaryArchive.read(*primaryCandidate, resource)
                            : fallbackArchive->read(*fallbackCandidate,
                                                    resource);
        if (!result) {
            output.clear();
            return Result::failure("Could not load " +
                                   std::string(assetName) + " texture " +
                                   image.sourcePath + ": " + result.message());
        }
        assets::BtexTexture texture;
        result = texture.load(resource);
        if (!result) {
            output.clear();
            return Result::failure("Could not decode " +
                                   std::string(assetName) + " texture " +
                                   image.sourcePath + ": " + result.message());
        }
        output.push_back(std::move(texture));
    }
    return Result::success();
}

} // namespace

Result LevelOneBootstrap::load(const std::filesystem::path& gameDataRoot) {
    introRooms_.clear();
    introSky_ = {};
    introActors_.clear();
    player_ = {};
    cameraAreas_.clear();
    triggers_.clear();
    cinematics_.clear();
    enemyArchetypes_.clear();
    enemies_.clear();
    filesystem::GbmpArchive levelArchive;
    Result result = levelArchive.open(gameDataRoot / "levelnew_01.pack");
    if (!result) {
        return result;
    }
    filesystem::GbmpArchive entityArchive;
    result = entityArchive.open(gameDataRoot / "entities.pack");
    if (!result) {
        return result;
    }

    std::vector<std::byte> resource;
    result = levelArchive.read("levelnew_01.irr", resource);
    if (!result) {
        return result;
    }
    result = mainScene_.load(resource);
    if (!result) {
        return result;
    }

    const auto playerNode = std::find_if(
        mainScene_.nodes().begin(), mainScene_.nodes().end(),
        [](const assets::IrrSceneNode& node) {
            return node.gameType == "SpiderMan";
        });
    if (playerNode == mainScene_.nodes().end() ||
        playerNode->meshFile.empty() || playerNode->animationFile.empty()) {
        return Result::failure(
            "Level 1 scene has no complete Spider-Man player node");
    }
    player_.objectId = playerNode->id;
    player_.sceneNodeName = playerNode->name;
    player_.initialAnimation = playerNode->initialAnimation;
    player_.initialCameraAreaId = playerNode->initialCameraAreaId;
    player_.linkedCinematicId = playerNode->linkedCinematicId;
    player_.endGameCinematicId = playerNode->endGameCinematicId;
    player_.hasCollision = playerNode->hasCollision;
    player_.position = playerNode->position;
    player_.rotation = playerNode->rotation;
    player_.scale = playerNode->scale;
    player_.worldTransform = playerNode->absoluteTransform;

    result = entityArchive.read(normalizeArchivePath(playerNode->meshFile),
                                resource);
    if (!result) {
        return Result::failure("Could not load player mesh: " +
                               result.message());
    }
    result = player_.mesh.load(resource);
    if (!result) {
        return Result::failure("Could not parse player mesh: " +
                               result.message());
    }
    result = loadTextures(entityArchive, nullptr, player_.mesh,
                          player_.textures, player_.sceneNodeName);
    if (!result) {
        return result;
    }
    result = entityArchive.read(
        normalizeArchivePath(playerNode->animationFile), resource);
    if (!result) {
        return Result::failure("Could not load player animation bank: " +
                               result.message());
    }
    result = player_.animationBank.load(resource);
    if (!result) {
        return Result::failure("Could not parse player animation bank: " +
                               result.message());
    }
    if (player_.animationBank.findClip(player_.initialAnimation) == nullptr) {
        return Result::failure("Player initial animation is not in its bank");
    }

    for (const assets::IrrSceneNode& areaNode : mainScene_.nodes()) {
        if (areaNode.gameType != "CameraArea") {
            continue;
        }
        CameraArea area;
        area.objectId = areaNode.id;
        area.nextAreaIds = areaNode.nextCameraAreaIds;
        area.switchTimeUnits = areaNode.cameraAreaSwitchTimeUnits;
        area.inverseNormal = areaNode.cameraAreaInverseNormal;
        area.height = areaNode.cameraAreaHeight;
        area.zFollowRate = areaNode.cameraAreaZFollowRate;
        area.disabled = areaNode.cameraAreaDisabled;
        area.farPlaneOffset = areaNode.cameraFarPlaneOffset;
        for (std::size_t index = 0; index < area.controlPoints.size();
             ++index) {
            const assets::IrrSceneNode* controlNode =
                mainScene_.findNode(areaNode.cameraControlPointIds[index]);
            if (controlNode == nullptr ||
                controlNode->gameType != "CamCtrlPoint" ||
                controlNode->cameraDistance <= 0.0F) {
                cameraAreas_.clear();
                return Result::failure("Camera area " + areaNode.name +
                                       " has an invalid control point");
            }
            area.controlPoints[index] = {
                controlNode->id,
                controlNode->position,
                controlNode->cameraDirection,
                controlNode->cameraDistance,
                controlNode->cameraTargetOffset,
                controlNode->cameraTargetHeightOffset,
            };
        }
        cameraAreas_.push_back(std::move(area));
    }
    GameplayCamera gameplayCamera;
    result = gameplayCamera.bind(cameraAreas_, player_.initialCameraAreaId);
    if (!result) {
        cameraAreas_.clear();
        return result;
    }

    introRooms_.reserve(5);
    for (std::uint32_t roomNumber = 1; roomNumber <= 5; ++roomNumber) {
        LevelRoomAsset room;
        room.name = "Room" + std::to_string(roomNumber);
        const std::string roomFile =
            "levelnew_01_" + std::to_string(roomNumber - 1) + "_" +
            room.name + ".irr";
        result = levelArchive.read(roomFile, resource);
        if (!result) {
            return result;
        }
        result = room.scene.load(resource);
        if (!result) {
            return Result::failure("Could not parse " + room.name + ": " +
                                   result.message());
        }
        const auto geometryNode = std::find_if(
            room.scene.nodes().begin(), room.scene.nodes().end(),
            [](const assets::IrrSceneNode& node) {
                return node.gameType == "Geometry" && !node.meshFile.empty();
            });
        if (geometryNode == room.scene.nodes().end()) {
            return Result::failure(room.name +
                                   " has no Geometry scene node");
        }
        result = levelArchive.read(
            normalizeArchivePath(geometryNode->meshFile), resource);
        if (!result) {
            return result;
        }
        result = room.geometry.load(resource);
        if (!result || room.geometry.geometries().empty()) {
            return Result::failure("Could not parse geometry for " +
                                   room.name + ": " + result.message());
        }
        result = loadTextures(levelArchive, &entityArchive, room.geometry,
                              room.textures, room.name);
        if (!result || room.textures.empty()) {
            return !result ? result
                           : Result::failure(room.name +
                                             " has no diffuse textures");
        }
        const auto collisionNode = std::find_if(
            room.scene.nodes().begin(), room.scene.nodes().end(),
            [](const assets::IrrSceneNode& node) {
                return node.gameType == "Collisions" &&
                       !node.meshFile.empty();
            });
        const auto navigationNode = std::find_if(
            room.scene.nodes().begin(), room.scene.nodes().end(),
            [](const assets::IrrSceneNode& node) {
                return node.gameType == "NavMesh" &&
                       !node.meshFile.empty();
            });
        if (collisionNode == room.scene.nodes().end() ||
            navigationNode == room.scene.nodes().end()) {
            return Result::failure(room.name +
                                   " has no collision or navigation mesh");
        }
        result = levelArchive.read(
            normalizeArchivePath(collisionNode->meshFile), resource);
        if (!result || !(result = room.collision.load(resource)) ||
            room.collision.geometries().empty()) {
            return Result::failure("Could not load collision mesh for " +
                                   room.name + ": " + result.message());
        }
        result = levelArchive.read(
            normalizeArchivePath(navigationNode->meshFile), resource);
        if (!result || !(result = room.navigationMesh.load(resource)) ||
            room.navigationMesh.geometries().empty()) {
            return Result::failure("Could not load navigation mesh for " +
                                   room.name + ": " + result.message());
        }
        introRooms_.push_back(std::move(room));
    }

    for (const LevelRoomAsset& room : introRooms_) {
        for (const assets::IrrSceneNode& node : room.scene.nodes()) {
            if (node.gameType == "Trigger") {
                LevelTriggerAsset trigger;
                trigger.objectId = node.id;
                trigger.name = node.name;
                trigger.position = node.position;
                trigger.rotation = node.rotation;
                trigger.scale = node.scale;
                trigger.worldTransform = node.absoluteTransform;
                trigger.sizes = vectorAttribute(node, "Sizes");
                trigger.orientedBox =
                    booleanAttribute(node, "IsOBBox", false);
                trigger.enabled = booleanAttribute(node, "Enabled", true);
                trigger.autoDisabled =
                    booleanAttribute(node, "AutoDisabled", false);
                trigger.outToInCinematicId =
                    integerAttribute(node, "^OutToIn^Cinematic");
                trigger.inToOutCinematicId =
                    integerAttribute(node, "^InToOut^Cinematic");
                trigger.whileInsideCinematicId =
                    integerAttribute(node, "^WhileIn^Cinematic");
                trigger.whileOutsideCinematicId =
                    integerAttribute(node, "^WhileOut^Cinematic");
                triggers_.push_back(std::move(trigger));
                continue;
            }
            if (node.gameType == "Cinematic") {
                const std::string_view scriptFile =
                    userAttribute(node, "!ScriptFile");
                if (scriptFile.empty()) {
                    return Result::failure("Cinematic " + node.name +
                                           " has no script file");
                }
                LevelCinematicAsset cinematic;
                cinematic.objectId = node.id;
                cinematic.name = node.name;
                cinematic.scriptFile = normalizeArchivePath(
                    std::string(scriptFile));
                result = levelArchive.read(cinematic.scriptFile, resource);
                if (!result || !(result = cinematic.script.load(resource))) {
                    return Result::failure("Could not load cinematic " +
                                           cinematic.name + ": " +
                                           result.message());
                }
                cinematics_.push_back(std::move(cinematic));
                continue;
            }
            if (!node.gameType.starts_with("MeleeThugEnemy_")) {
                continue;
            }

            std::string resolvedAnimationFile = node.animationFile;
            if (entityArchive.find(
                    normalizeArchivePath(resolvedAnimationFile)) == nullptr &&
                node.gameType == "MeleeThugEnemy_knife") {
                // Level 1 authors knife enemies with a nonexistent
                // thug_knife_anim.bdae. The shipped archive and cinematic
                // knife actor both use the shared bat/knife animation bank.
                resolvedAnimationFile =
                    "../entities/meshes_bin/thug_bat_anim.bdae";
            }
            const auto archetype = std::find_if(
                enemyArchetypes_.begin(), enemyArchetypes_.end(),
                [&node, &resolvedAnimationFile](
                    const EnemyArchetypeAsset& candidate) {
                    return candidate.meshFile == node.meshFile &&
                           candidate.animationFile == resolvedAnimationFile;
                });
            std::size_t archetypeIndex = 0;
            if (archetype == enemyArchetypes_.end()) {
                EnemyArchetypeAsset asset;
                asset.gameType = node.gameType;
                asset.meshFile = node.meshFile;
                asset.animationFile = resolvedAnimationFile;
                result = entityArchive.read(
                    normalizeArchivePath(asset.meshFile), resource);
                if (!result || !(result = asset.mesh.load(resource))) {
                    return Result::failure("Could not load enemy mesh " +
                                           asset.meshFile + ": " +
                                           result.message());
                }
                result = loadTextures(entityArchive, nullptr, asset.mesh,
                                      asset.textures, asset.gameType);
                if (!result) {
                    return result;
                }
                result = entityArchive.read(
                    normalizeArchivePath(asset.animationFile), resource);
                if (!result || !(result = asset.animationBank.load(resource))) {
                    return Result::failure("Could not load enemy animation " +
                                           asset.animationFile + ": " +
                                           result.message());
                }
                enemyArchetypes_.push_back(std::move(asset));
                archetypeIndex = enemyArchetypes_.size() - 1;
            } else {
                archetypeIndex = static_cast<std::size_t>(
                    std::distance(enemyArchetypes_.begin(), archetype));
            }

            LevelEnemyAsset enemy;
            enemy.objectId = node.id;
            enemy.name = node.name;
            enemy.gameType = node.gameType;
            enemy.initialAnimation = node.initialAnimation;
            if (enemy.initialAnimation.empty()) {
                enemy.initialAnimation =
                    node.gameType == "MeleeThugEnemy_knife"
                        ? "idle_knife_at_idle"
                        : "idle_at1_idle";
            }
            enemy.archetypeIndex = archetypeIndex;
            enemy.position = node.position;
            enemy.rotation = node.rotation;
            enemy.scale = node.scale;
            enemy.worldTransform = node.absoluteTransform;
            enemy.health = floatAttribute(node, "Health");
            enemy.visible = node.visible;
            enemy.aiEnabled = booleanAttribute(node, "AI_Enable", true);
            enemy.waitSpawn = booleanAttribute(node, "WaitSpawn", false);
            enemy.lineSpeedCentimetersPerMillisecond =
                floatAttribute(node, "Line_Speed", 0.3F);
            enemy.awarenessRadius = floatAttribute(node, "Aware_Radius");
            enemy.awarenessAngleDegrees =
                floatAttribute(node, "Aware_Angle");
            if (enemyArchetypes_[archetypeIndex].animationBank.findClip(
                    enemy.initialAnimation) == nullptr) {
                return Result::failure("Enemy " + std::to_string(enemy.objectId) +
                                       " initial animation " +
                                       enemy.initialAnimation + " is missing");
            }
            enemies_.push_back(std::move(enemy));
        }
    }

    introSky_.name = "Level 1 sky";
    result = levelArchive.read("meshes_bin/lvl01_sky.bdae", resource);
    if (!result) {
        return result;
    }
    result = introSky_.geometry.load(resource);
    if (!result || introSky_.geometry.geometries().empty()) {
        return Result::failure("Could not parse Level 1 sky: " +
                               result.message());
    }
    result = loadTextures(levelArchive, &entityArchive, introSky_.geometry,
                          introSky_.textures, introSky_.name);
    if (!result) {
        return result;
    }
    const auto skyNode = std::find_if(
        mainScene_.nodes().begin(), mainScene_.nodes().end(),
        [](const assets::IrrSceneNode& node) {
            return node.meshFile.find("lvl01_sky.bdae") != std::string::npos;
        });
    if (skyNode == mainScene_.nodes().end()) {
        return Result::failure("Level 1 scene has no sky mesh node");
    }
    // CSkyBoxObject::Update (0x0031b6f4) replaces the serialized node
    // position with the active camera position every frame.
    introSky_.cameraRelative = true;

    result = levelArchive.read(
        "cinematics/levelnew_01_1264_cinematic.cff", resource);
    if (!result) {
        return result;
    }
    result = introStartScript_.load(resource);
    if (!result) {
        return result;
    }
    result = levelArchive.read(
        "cinematics/levelnew_01_1265_cinematic.cff", resource);
    if (!result) {
        return result;
    }
    result = introScript_.load(resource);
    if (!result) {
        return result;
    }
    result = levelArchive.read(
        "cinematics/levelnew_01_1266_cinematic.cff", resource);
    if (!result) {
        return result;
    }
    result = introEndScript_.load(resource);
    if (!result) {
        return result;
    }
    result = levelArchive.read("meshes_bin/camera_lv1_start.bdae", resource);
    if (!result) {
        return result;
    }
    result = introCameraAnimation_.load(resource);
    if (!result) {
        return result;
    }
    // PlayDAECamera in levelnew_01_1265_cinematic.cff overrides the BDAE's
    // 1000-unit far plane with 10000 units.
    result = introCamera_.bind(introCameraAnimation_, 10000.0F);
    if (!result) {
        return result;
    }

    for (const CinematicThread& thread : introScript_.threads()) {
        for (const CinematicCommand& command : thread.commands) {
            if (command.name != "PlayDAEAnim") {
                continue;
            }
            const CinematicAttribute* animationFile =
                command.findAttribute("AnimFile");
            const assets::IrrSceneNode* sceneNode =
                findLevelNode(mainScene_, introRooms_.front().scene,
                              thread.objectId);
            if (animationFile == nullptr || sceneNode == nullptr ||
                sceneNode->meshFile.empty()) {
                introActors_.clear();
                return Result::failure(
                    "Cinematic actor command has no matching scene node");
            }

            CinematicActorAsset actor;
            actor.objectId = thread.objectId;
            actor.sceneNodeName = sceneNode->name;
            actor.animationStartMilliseconds = command.timestampMilliseconds;
            actor.position = sceneNode->position;
            actor.rotation = sceneNode->rotation;
            actor.scale = sceneNode->scale;
            actor.worldTransform = sceneNode->absoluteTransform;

            result = entityArchive.read(
                normalizeArchivePath(sceneNode->meshFile), resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not load cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = actor.mesh.load(resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not parse cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = loadTextures(entityArchive, nullptr, actor.mesh,
                                  actor.textures, actor.sceneNodeName);
            if (!result) {
                introActors_.clear();
                return result;
            }
            result = levelArchive.read(
                normalizeArchivePath(animationFile->value), resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not load animation for " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = actor.animation.load(resource);
            if (!result) {
                introActors_.clear();
                return Result::failure("Could not parse animation for " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            introActors_.push_back(std::move(actor));
        }
    }
    if (introActors_.empty()) {
        return Result::failure("Level-one intro has no cinematic actors");
    }
    return Result::success();
}

} // namespace usm::game
