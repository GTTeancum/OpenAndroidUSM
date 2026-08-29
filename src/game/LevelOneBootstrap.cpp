#include "game/LevelOneBootstrap.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <vector>

namespace usm::game {

Result LevelOneBootstrap::load(const std::filesystem::path& gameDataRoot) {
    roomTextures_.clear();
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
    return Result::success();
}

} // namespace usm::game
