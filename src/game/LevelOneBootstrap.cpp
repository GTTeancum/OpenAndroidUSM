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

Result loadTextures(filesystem::GbmpArchive& archive,
                    const assets::ColladaMeshFile& mesh,
                    std::vector<assets::BtexTexture>& output) {
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
        const auto candidate = std::find_if(
            candidates.begin(), candidates.end(),
            [&archive](const std::string& path) {
                return archive.find(path) != nullptr;
            });
        if (candidate == candidates.end()) {
            output.clear();
            return Result::failure("Could not locate actor texture " +
                                   image.sourcePath);
        }
        Result result = archive.read(*candidate, resource);
        if (!result) {
            output.clear();
            return Result::failure("Could not load actor texture " +
                                   image.sourcePath + ": " + result.message());
        }
        assets::BtexTexture texture;
        result = texture.load(resource);
        if (!result) {
            output.clear();
            return Result::failure("Could not decode actor texture " +
                                   image.sourcePath + ": " + result.message());
        }
        output.push_back(std::move(texture));
    }
    return Result::success();
}

} // namespace

Result LevelOneBootstrap::load(const std::filesystem::path& gameDataRoot) {
    roomTextures_.clear();
    introActors_.clear();
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

    result = levelArchive.read("levelnew_01_0_Room1.irr", resource);
    if (!result) {
        return result;
    }
    result = firstRoom_.load(resource);
    if (!result) {
        return result;
    }

    result = levelArchive.read("meshes_bin/geometry01.bdae", resource);
    if (!result) {
        return result;
    }
    result = roomGeometry_.load(resource);
    if (!result) {
        return result;
    }
    if (roomGeometry_.geometries().empty()) {
        return Result::failure("Room 1 geometry contains no renderable meshes");
    }

    roomTextures_.reserve(roomGeometry_.images().size());
    for (const assets::ColladaImage& image : roomGeometry_.images()) {
        result = levelArchive.read("textures_bin/" + image.sourcePath, resource);
        if (!result) {
            result = entityArchive.read("textures_bin/" + image.sourcePath,
                                        resource);
            if (!result) {
                roomTextures_.clear();
                return Result::failure("Could not load Room 1 texture " +
                                       image.sourcePath + ": " +
                                       result.message());
            }
        }
        assets::BtexTexture texture;
        result = texture.load(resource);
        if (!result) {
            roomTextures_.clear();
            return Result::failure("Could not decode Room 1 texture " +
                                   image.sourcePath + ": " + result.message());
        }
        roomTextures_.push_back(std::move(texture));
    }
    if (roomTextures_.empty()) {
        return Result::failure("Room 1 geometry has no diffuse textures");
    }

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
                findLevelNode(mainScene_, firstRoom_, thread.objectId);
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
            result = loadTextures(entityArchive, actor.mesh, actor.textures);
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
