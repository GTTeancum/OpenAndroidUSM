#pragma once

#include "assets/BtexTexture.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicScript.hpp"

#include <filesystem>
#include <vector>

namespace usm::game {

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

private:
    assets::IrrScene mainScene_;
    assets::IrrScene firstRoom_;
    assets::ColladaMeshFile roomGeometry_;
    std::vector<assets::BtexTexture> roomTextures_;
    CinematicScript introStartScript_;
    CinematicScript introScript_;
    CinematicScript introEndScript_;
};

} // namespace usm::game
