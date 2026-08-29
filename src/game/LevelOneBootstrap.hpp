#pragma once

#include "assets/BtexTexture.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"

#include <filesystem>

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
        return roomTexture_;
    }

private:
    assets::IrrScene mainScene_;
    assets::IrrScene firstRoom_;
    assets::ColladaMeshFile roomGeometry_;
    assets::BtexTexture roomTexture_;
};

} // namespace usm::game
