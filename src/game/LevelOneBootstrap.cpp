#include "game/LevelOneBootstrap.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <vector>

namespace usm::game {

Result LevelOneBootstrap::load(const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive levelArchive;
    Result result = levelArchive.open(gameDataRoot / "levelnew_01.pack");
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

    result = levelArchive.read("textures_bin/level01_alphatest.tga", resource);
    if (!result) {
        return result;
    }
    return roomTexture_.load(resource);
}

} // namespace usm::game
