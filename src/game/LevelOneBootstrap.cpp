#include "game/LevelOneBootstrap.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
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
        introRooms_.push_back(std::move(room));
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
