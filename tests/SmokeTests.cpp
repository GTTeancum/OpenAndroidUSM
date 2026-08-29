#include "assets/BresFile.hpp"
#include "assets/BtexTexture.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "audio/OggAudio.hpp"
#include "core/Result.hpp"
#include "filesystem/GbmpArchive.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <string_view>
#include <vector>

int main() {
    const auto success = usm::Result::success();
    assert(static_cast<bool>(success));

    const auto failure = usm::Result::failure("expected failure");
    assert(!static_cast<bool>(failure));
    assert(failure.message() == "expected failure");

    using namespace usm::reconstructed;
    XperiaKeyRouter router;
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, true},
                 InputContext::Gameplay);
    assert(router.state().jump.pressed);
    assert(router.state().jump.held);
    assert(router.state().quickTimeEvent.pressed);

    router.beginFrame();
    assert(!router.state().jump.pressed);
    assert(router.state().jump.held);
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, false},
                 InputContext::Gameplay);
    assert(router.state().jump.released);
    assert(!router.state().jump.held);

    router.beginFrame();
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, true},
                 InputContext::UpgradeMenu);
    assert(router.state().upgrade.pressed);
    assert(router.state().upgradeProceed.pressed);
    assert(!router.state().jump.pressed);

    const std::filesystem::path dataRoot = USM_TEST_GAME_DATA_ROOT;
    const std::filesystem::path configArchive = dataRoot / "configs.pack";
    if (std::filesystem::exists(configArchive)) {
        const auto audioPath = dataRoot / "sound" / "SFX" / "INTERFACE" /
                               "sfx_point_spend.ogg";
        std::ifstream audioStream(audioPath, std::ios::binary);
        assert(audioStream);
        const std::vector<char> encodedAudio(
            (std::istreambuf_iterator<char>(audioStream)),
            std::istreambuf_iterator<char>());
        const auto encodedBytes = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(encodedAudio.data()),
            encodedAudio.size());
        usm::audio::PcmAudio decodedAudio;
        assert(usm::audio::decodeOggVorbis(encodedBytes, decodedAudio));
        assert(decodedAudio.sampleRate > 0);
        assert(decodedAudio.channelCount > 0);
        assert(decodedAudio.frameCount() > 0);

        usm::filesystem::GbmpArchive archive;
        assert(archive.open(configArchive));
        assert(archive.entries().size() == 40);

        std::vector<std::byte> resource;
        assert(archive.read("GS_BossRushEndLevel.json", resource));
        assert(resource.size() == 5300);

        usm::filesystem::GbmpArchive levelOne;
        assert(levelOne.open(dataRoot / "levelnew_01.pack"));
        assert(levelOne.entries().size() == 250);
        assert(levelOne.find("meshes_bin/camera_Lv1_beforeboss.bdae") != nullptr);

        std::vector<std::byte> meshResource;
        assert(levelOne.read("meshes_bin/geometry01.bdae", meshResource));
        usm::assets::BresFile meshFile;
        assert(meshFile.load(meshResource));
        assert(meshFile.header().fileSize == 441000);
        assert(meshFile.header().relocationCount == 4173);
        assert(meshFile.resolvePointer(0x14) == 0x20);
        assert(meshFile.resolvePointer(0x18) == 0x4154);
        assert(meshFile.resolvePointer(0x1c) == 0x8c0c);

        usm::assets::ColladaMeshFile colladaMesh;
        assert(colladaMesh.load(meshResource));
        assert(colladaMesh.geometries().size() == 152);
        const auto& firstGeometry = colladaMesh.geometries().front();
        assert(firstGeometry.id == "Object211420-mesh");
        assert(firstGeometry.name == "Object211420");
        assert(firstGeometry.vertices.size() == 16);
        assert(firstGeometry.meshBuffers.size() == 1);
        assert(firstGeometry.meshBuffers.front().indices.size() == 24);
        assert(colladaMesh.images().size() == 18);
        assert(colladaMesh.materials().size() == 64);
        const auto* alphaMaterial = colladaMesh.findMaterial("alphatest");
        assert(alphaMaterial != nullptr);
        assert(alphaMaterial->diffuseImageIndex == 0);
        assert(colladaMesh.images()[*alphaMaterial->diffuseImageIndex].sourcePath ==
               "level01_alphatest.tga");
        const auto* buildingMaterial = colladaMesh.findMaterial("Material__54");
        assert(buildingMaterial != nullptr);
        assert(buildingMaterial->diffuseImageIndex == 1);
        assert(colladaMesh.images()[*buildingMaterial->diffuseImageIndex]
                   .sourcePath == "041_building.tga");

        std::size_t meshFileCount = 0;
        std::size_t geometryCount = 0;
        std::size_t meshBufferCount = 0;
        for (const auto& entry : levelOne.entries()) {
            if (!std::string_view(entry.path).ends_with(".bdae")) {
                continue;
            }
            std::vector<std::byte> meshBytes;
            assert(levelOne.read(entry.path, meshBytes));
            usm::assets::ColladaMeshFile parsedMesh;
            const usm::Result meshResult = parsedMesh.load(meshBytes);
            if (!meshResult) {
                std::cerr << "Could not parse " << entry.path << ": "
                          << meshResult.message() << '\n';
                return 1;
            }
            ++meshFileCount;
            geometryCount += parsedMesh.geometries().size();
            for (const auto& geometry : parsedMesh.geometries()) {
                meshBufferCount += geometry.meshBuffers.size();
            }
        }
        assert(meshFileCount == 113);
        assert(geometryCount == 1178);
        assert(meshBufferCount == 1612);

        std::vector<std::byte> textureBytes;
        assert(levelOne.read("textures_bin/levelnew_01_02.tga", textureBytes));
        usm::assets::BtexTexture levelTexture;
        assert(levelTexture.load(textureBytes));
        assert(levelTexture.mipLevels().size() == 1);
        assert(levelTexture.mipLevels().front().width == 512);
        assert(levelTexture.mipLevels().front().height == 512);
        assert(levelTexture.mipLevels().front().pixels.size() == 512 * 512 * 4);
        assert(!levelTexture.containsAlpha());

        usm::game::LevelOneBootstrap bootstrap;
        const usm::Result bootstrapResult = bootstrap.load(dataRoot);
        if (!bootstrapResult) {
            std::cerr << "Level-one bootstrap failed: "
                      << bootstrapResult.message() << '\n';
            return 1;
        }
        assert(bootstrap.mainScene().nodes().size() == 154);
        assert(bootstrap.firstRoom().nodes().size() == 60);
        assert(!bootstrap.previewGeometry().vertices.empty());
        assert(!bootstrap.previewTexture().mipLevels().empty());
        assert(bootstrap.roomTextures().size() ==
               bootstrap.roomGeometry().images().size());

        std::vector<std::byte> roomResource;
        assert(levelOne.read("levelnew_01_0_Room1.irr", roomResource));
        usm::assets::IrrScene roomScene;
        assert(roomScene.load(roomResource));
        assert(roomScene.nodes().size() == 60);
        assert(roomScene.nodes().front().name == "Room1");
        assert(roomScene.nodes().front().gameType == "Geometry");
        assert(roomScene.nodes().front().meshFile ==
               "meshes_bin/geometry01.bdae");
    }
    return 0;
}
