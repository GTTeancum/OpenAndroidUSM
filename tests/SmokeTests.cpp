#include "assets/BresFile.hpp"
#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/ColladaSkinning.hpp"
#include "assets/IrrScene.hpp"
#include "audio/OggAudio.hpp"
#include "audio/CinematicSoundBank.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "core/Result.hpp"
#include "filesystem/GbmpArchive.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelCollision.hpp"
#include "game/CinematicScript.hpp"
#include "game/CinematicPlayer.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <cassert>
#include <array>
#include <cmath>
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

    usm::assets::ColladaGeometry collisionFixture;
    collisionFixture.vertices = {
        {{0.0F, 0.0F, 0.0F}},
        {{1000.0F, 0.0F, 0.0F}},
        {{1000.0F, 1000.0F, 0.0F}},
        {{0.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 0.0F}},
        {{500.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 200.0F}},
        {{500.0F, 1000.0F, 200.0F}},
    };
    usm::assets::ColladaMeshBuffer collisionFixtureBuffer;
    collisionFixtureBuffer.indices = {
        0, 1, 2, 0, 2, 3,
        4, 6, 7, 4, 7, 5,
    };
    collisionFixture.meshBuffers.push_back(collisionFixtureBuffer);
    std::array<usm::assets::ColladaGeometry, 1> collisionFixtureSet{
        collisionFixture};
    usm::game::LevelCollision collisionFixtureWorld;
    assert(collisionFixtureWorld.build(collisionFixtureSet));
    float fixtureGround = -1.0F;
    assert(collisionFixtureWorld.groundHeight({250.0F, 500.0F, 20.0F},
                                              50.0F, 100.0F,
                                              fixtureGround));
    assert(std::abs(fixtureGround) < 0.001F);
    usm::assets::Vector3 wallResolved;
    assert(collisionFixtureWorld.resolveGroundMotion(
        {400.0F, 500.0F, 0.0F}, {600.0F, 500.0F, 0.0F}, wallResolved));
    assert(std::abs(wallResolved.x - 450.0F) < 0.01F);

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

        usm::audio::SoundEventCatalog soundCatalog;
        assert(soundCatalog.index(dataRoot / "sound"));
        assert(soundCatalog.eventCount() == 510);
        assert(soundCatalog.ambiguousEventCount() == 5);
        assert(soundCatalog.resolve("SFX_WEB_SWING_START") != nullptr);
        assert(soundCatalog.resolve("VFX_PROLOGUE_SPIDY_01") != nullptr);
        assert(soundCatalog.resolve("SFX_CUTSCENE_LV3_SPIDY_ARRIVES") !=
               nullptr);
        assert(soundCatalog.resolve("SFX_THUG_KNIFE_HURT_1") == nullptr);
        assert(soundCatalog.resolve("SFX_VERTICAL_IMPACT") == nullptr);
        usm::audio::PcmAudio catalogAudio;
        assert(soundCatalog.decode("SFX_WEB_SWING_START", catalogAudio));
        assert(catalogAudio.frameCount() > 0);

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

        usm::filesystem::GbmpArchive entities;
        const usm::Result entitiesResult =
            entities.open(dataRoot / "entities.pack");
        if (!entitiesResult) {
            std::cerr << entitiesResult.message() << '\n';
            return 1;
        }
        std::vector<std::byte> playerAnimationResource;
        const usm::Result playerAnimationRead = entities.read(
            "meshes_bin/spiderman_anim.bdae", playerAnimationResource);
        if (!playerAnimationRead) {
            std::cerr << playerAnimationRead.message() << '\n';
            return 1;
        }
        usm::assets::ColladaAnimationFile playerAnimations;
        const usm::Result playerAnimationResult =
            playerAnimations.load(playerAnimationResource);
        if (!playerAnimationResult) {
            std::cerr << playerAnimationResult.message() << '\n';
            return 1;
        }
        if (playerAnimations.tracks().size() != 46 ||
            playerAnimations.clips().size() != 242) {
            std::cerr << "Unexpected Spider-Man animation bank: "
                      << playerAnimations.tracks().size() << " tracks, "
                      << playerAnimations.clips().size() << " clips\n";
            return 1;
        }
        const auto* idleClip = playerAnimations.findClip("idle_stand");
        if (idleClip == nullptr || idleClip->startMilliseconds != 3133 ||
            idleClip->endMilliseconds != 4466 ||
            idleClip->durationMilliseconds() != 1333) {
            std::cerr << "idle_stand animation clip was not recovered\n";
            return 1;
        }
        const auto* runClip = playerAnimations.findClip("run");
        if (runClip == nullptr || runClip->startMilliseconds != 8033 ||
            runClip->endMilliseconds != 8833) {
            std::cerr << "run animation clip was not recovered\n";
            return 1;
        }

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
        if (colladaMesh.sceneGeometries().size() != 152) {
            std::cerr << "Unexpected scene geometry count: "
                      << colladaMesh.sceneGeometries().size() << '\n';
            return 1;
        }
        const auto& firstGeometry = colladaMesh.geometries().front();
        assert(firstGeometry.id == "Object211420-mesh");
        assert(firstGeometry.name == "Object211420");
        assert(firstGeometry.vertices.size() == 16);
        assert(firstGeometry.meshBuffers.size() == 1);
        assert(firstGeometry.meshBuffers.front().indices.size() == 24);
        if (colladaMesh.sceneGeometries().front().name == firstGeometry.name) {
            std::cerr << "Visual-scene geometry transform was not selected\n";
            return 1;
        }
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
        assert(bootstrap.mainScene().findNode(288) != nullptr);
        assert(bootstrap.mainScene().findNode(288)->gameType == "SpiderMan");
        assert(bootstrap.mainScene().findNode(288)->animationFile ==
               "../entities/meshes_bin/spiderman_anim.bdae");
        assert(bootstrap.mainScene().findNode(288)->initialAnimation ==
               "kick_left_double_kick");
        assert(bootstrap.mainScene().findNode(288)->hasCollision);
        assert(bootstrap.mainScene().findNode(288)->initialCameraAreaId == 283);
        assert(bootstrap.mainScene().findNode(288)->linkedCinematicId == 1265);
        assert(bootstrap.mainScene().findNode(288)->endGameCinematicId == 1267);
        const auto* initialCameraNode = bootstrap.mainScene().findNode(283);
        assert(initialCameraNode != nullptr);
        assert(initialCameraNode->cameraAreaHeight == 500.0F);
        assert(initialCameraNode->cameraAreaZFollowRate == 0.5F);
        assert(!initialCameraNode->cameraAreaInverseNormal);
        assert(!initialCameraNode->cameraAreaDisabled);
        assert(initialCameraNode->nextCameraAreaIds[0] == 304);
        assert(initialCameraNode->cameraAreaSwitchTimeUnits[0] == 30);
        assert(bootstrap.player().objectId == 288);
        assert(bootstrap.player().sceneNodeName == "SpiderMan");
        assert(bootstrap.player().initialAnimation ==
               "kick_left_double_kick");
        assert(bootstrap.player().initialCameraAreaId == 283);
        assert(bootstrap.player().linkedCinematicId == 1265);
        assert(bootstrap.player().endGameCinematicId == 1267);
        assert(bootstrap.player().hasCollision);
        assert(bootstrap.player().mesh.skins().size() == 1);
        assert(bootstrap.player().textures.size() == 2);
        assert(bootstrap.player().animationBank.tracks().size() == 46);
        assert(bootstrap.player().animationBank.clips().size() == 242);
        assert(bootstrap.player().animationBank.tracks()[1].property ==
               usm::assets::ColladaAnimationProperty::TranslationZ);
        assert(bootstrap.player().animationBank.tracks()[40].property ==
               usm::assets::ColladaAnimationProperty::Translation);
        assert(bootstrap.player().animationBank.tracks()[44].property ==
               usm::assets::ColladaAnimationProperty::TranslationX);
        usm::game::LevelCollision levelCollision;
        assert(levelCollision.build(bootstrap.introRooms()));
        assert(levelCollision.triangleCount() > 100);
        float initialGroundHeight = 0.0F;
        assert(levelCollision.groundHeight(bootstrap.player().position,
                                           100.0F, 500.0F,
                                           initialGroundHeight));
        usm::game::GameplayPlayer groundedPlayer;
        assert(groundedPlayer.initialize(bootstrap.player(), &levelCollision));
        assert(std::abs(groundedPlayer.position().z - initialGroundHeight) <
               0.001F);
        std::vector<usm::assets::ColladaGeometry> idlePose;
        const auto* recoveredIdle =
            bootstrap.player().animationBank.findClip("idle_stand");
        assert(recoveredIdle != nullptr);
        assert(usm::assets::evaluateColladaPose(
            bootstrap.player().mesh, bootstrap.player().animationBank,
            recoveredIdle->startMilliseconds, idlePose));
        assert(idlePose.size() == 1);
        assert(std::abs(idlePose.front().bounds.minimum.x - -51.1281F) < 0.05F);
        assert(std::abs(idlePose.front().bounds.minimum.y - -68.0298F) < 0.05F);
        assert(std::abs(idlePose.front().bounds.minimum.z - -1.47334F) < 0.05F);
        assert(std::abs(idlePose.front().bounds.maximum.x - 46.8421F) < 0.05F);
        assert(std::abs(idlePose.front().bounds.maximum.y - 49.5647F) < 0.05F);
        assert(std::abs(idlePose.front().bounds.maximum.z - 134.96F) < 0.05F);
        if (bootstrap.cameraAreas().empty()) {
            std::cerr << "No gameplay camera areas were reconstructed\n";
            return 1;
        }
        usm::game::GameplayCamera gameplayCamera;
        const usm::Result gameplayCameraResult = gameplayCamera.bind(
            bootstrap.cameraAreas(), bootstrap.player().initialCameraAreaId);
        if (!gameplayCameraResult || gameplayCamera.currentAreaId() != 283) {
            std::cerr << "Initial gameplay camera could not bind: "
                      << gameplayCameraResult.message() << '\n';
            return 1;
        }
        const auto gameplayCameraPose =
            gameplayCamera.sample(bootstrap.player().position);
        if (std::abs(gameplayCameraPose.target.x - 14688.7148F) >= 0.1F ||
            std::abs(gameplayCameraPose.target.y - -9614.6006F) >= 0.1F ||
            std::abs(gameplayCameraPose.target.z - 126.8340F) >= 0.1F ||
            std::abs(gameplayCameraPose.position.x - 15133.0039F) >= 0.1F ||
            std::abs(gameplayCameraPose.position.y - -10275.4551F) >= 0.1F ||
            std::abs(gameplayCameraPose.position.z - 203.5090F) >= 0.1F) {
            std::cerr << "Unexpected initial gameplay camera pose: target "
                      << gameplayCameraPose.target.x << ' '
                      << gameplayCameraPose.target.y << ' '
                      << gameplayCameraPose.target.z << ", position "
                      << gameplayCameraPose.position.x << ' '
                      << gameplayCameraPose.position.y << ' '
                      << gameplayCameraPose.position.z << '\n';
            return 1;
        }
        usm::game::GameplayPlayer gameplayPlayer;
        assert(gameplayPlayer.initialize(bootstrap.player()));
        const auto initialPlayerPosition = gameplayPlayer.position();
        gameplayPlayer.update({0.0F, 1.0F}, gameplayCameraPose, 1000);
        const auto movedPlayerPosition = gameplayPlayer.position();
        const float playerDisplacement = std::hypot(
            movedPlayerPosition.x - initialPlayerPosition.x,
            movedPlayerPosition.y - initialPlayerPosition.y);
        assert(std::abs(playerDisplacement - 700.0F) < 0.1F);
        assert(gameplayPlayer.activeAnimation() == "run");
        assert(gameplayPlayer.animationTimeMilliseconds() == 1000);
        assert(std::abs(gameplayPlayer.worldTransform()[12] -
                        movedPlayerPosition.x) < 0.001F);
        assert(std::abs(gameplayPlayer.worldTransform()[13] -
                        movedPlayerPosition.y) < 0.001F);
        gameplayPlayer.update({}, gameplayCameraPose, 16);
        assert(gameplayPlayer.activeAnimation() == "idle_stand");
        assert(gameplayPlayer.animationTimeMilliseconds() == 16);

        auto makeCameraArea = [](std::int32_t id, float minimumX,
                                 float maximumX) {
            usm::game::CameraArea area;
            area.objectId = id;
            area.height = 100.0F;
            area.controlPoints[0].position = {minimumX, 0.0F, 0.0F};
            area.controlPoints[1].position = {maximumX, 0.0F, 0.0F};
            area.controlPoints[2].position = {maximumX, 10.0F, 0.0F};
            area.controlPoints[3].position = {minimumX, 10.0F, 0.0F};
            for (auto& point : area.controlPoints) {
                point.direction = {0.0F, 1.0F, 0.0F};
                point.distance = 100.0F;
            }
            return area;
        };
        std::array<usm::game::CameraArea, 2> adjacentCameraAreas{
            makeCameraArea(1, 0.0F, 10.0F),
            makeCameraArea(2, 10.0F, 20.0F),
        };
        adjacentCameraAreas[0].nextAreaIds[0] = 2;
        adjacentCameraAreas[0].switchTimeUnits[0] = 5;
        for (auto& point : adjacentCameraAreas[1].controlPoints) {
            point.direction = {1.0F, 0.0F, 0.0F};
            point.distance = 200.0F;
        }
        usm::game::GameplayCamera switchingCamera;
        assert(switchingCamera.bind(adjacentCameraAreas, 1));
        assert(!switchingCamera.updateArea({5.0F, 5.0F, 0.0F}));
        const auto cameraBeforeSwitch =
            switchingCamera.sample({15.0F, 5.0F, 0.0F});
        assert(switchingCamera.updateArea({15.0F, 5.0F, 0.0F}));
        assert(switchingCamera.currentAreaId() == 2);
        assert(switchingCamera.lastSwitchDurationMilliseconds() == 250);
        const auto cameraAtSwitch =
            switchingCamera.sample({15.0F, 5.0F, 0.0F});
        assert(std::abs(cameraAtSwitch.position.x -
                        cameraBeforeSwitch.position.x) < 0.001F);
        assert(std::abs(cameraAtSwitch.position.y -
                        cameraBeforeSwitch.position.y) < 0.001F);
        assert(!switchingCamera.updateArea({15.0F, 5.0F, 0.0F}, 125));
        const auto cameraHalfway =
            switchingCamera.sample({15.0F, 5.0F, 0.0F});
        assert(std::abs(cameraHalfway.position.x -
                        cameraAtSwitch.position.x) > 1.0F);
        assert(!switchingCamera.updateArea({15.0F, 5.0F, 0.0F}, 125));
        const auto cameraAfterSwitch =
            switchingCamera.sample({15.0F, 5.0F, 0.0F});
        assert(std::abs(cameraAfterSwitch.position.x - -185.0F) < 0.01F);
        assert(bootstrap.introRooms().size() == 5);
        assert(bootstrap.introRooms()[4].name == "Room5");
        assert(!bootstrap.introRooms()[4].geometry.geometries().empty());
        assert(bootstrap.introSky().cameraRelative);
        assert(!bootstrap.introSky().geometry.geometries().empty());
        assert(bootstrap.introSky().geometry.images().size() == 3);
        const auto* skyMaterial =
            bootstrap.introSky().geometry.findMaterial("sky");
        assert(skyMaterial != nullptr);
        assert(skyMaterial->diffuseImageIndex == 2);
        const auto& skyPixels =
            bootstrap.introSky().textures[2].mipLevels().front().pixels;
        bool skyHasTransparency = false;
        for (std::size_t alpha = 3; alpha < skyPixels.size(); alpha += 4) {
            skyHasTransparency |= skyPixels[alpha] != 255;
        }
        assert(skyHasTransparency);
        assert(bootstrap.firstRoom().nodes().size() == 60);
        assert(!bootstrap.previewGeometry().vertices.empty());
        assert(!bootstrap.previewTexture().mipLevels().empty());
        assert(bootstrap.roomTextures().size() ==
               bootstrap.roomGeometry().images().size());
        assert(bootstrap.introStartScript().threads().size() == 1);
        assert(bootstrap.introStartScript().commandCount() == 2);
        const auto& startCommands =
            bootstrap.introStartScript().threads().front().commands;
        assert(startCommands.front().name == "StartCinematic");
        assert(startCommands.front().findAttribute("CinematicID") != nullptr);
        assert(startCommands.front().findAttribute("CinematicID")->value ==
               "1265");
        assert(bootstrap.introScript().threads().size() == 11);
        assert(bootstrap.introScript().commandCount() == 38);
        const auto& playerCommands =
            bootstrap.introScript().threads().front().commands;
        assert(playerCommands.front().name == "PlayDAEAnim");
        assert(playerCommands.front().findAttribute("AnimFile") != nullptr);
        assert(playerCommands.front().findAttribute("AnimFile")->value ==
               ".\\meshes_bin\\spiderman_lv1_start.bdae");
        const auto cameraCommand = std::find_if(
            bootstrap.introScript().threads().begin(),
            bootstrap.introScript().threads().end(),
            [](const usm::game::CinematicThread& thread) {
                return std::any_of(
                    thread.commands.begin(), thread.commands.end(),
                    [](const usm::game::CinematicCommand& command) {
                        return command.name == "PlayDAECamera";
                    });
            });
        assert(cameraCommand != bootstrap.introScript().threads().end());
        assert(bootstrap.introEndScript().commandCount() == 1);
        const auto& cameraAnimation = bootstrap.introCameraAnimation();
        assert(cameraAnimation.tracks().size() == 3);
        assert(cameraAnimation.durationMilliseconds() == 53033);
        assert(cameraAnimation.tracks()[0].id == "Camera01-node-rotation");
        assert(cameraAnimation.tracks()[0].property ==
               usm::assets::ColladaAnimationProperty::Rotation);
        assert(cameraAnimation.tracks()[0].componentCount == 4);
        assert(cameraAnimation.tracks()[0].timestampsMilliseconds.size() == 823);
        assert(cameraAnimation.tracks()[1].id ==
               "Camera01-node-translation");
        assert(cameraAnimation.tracks()[1].componentCount == 3);
        assert(cameraAnimation.tracks()[2].id ==
               "Camera01.Target-node-translation");
        if (!cameraAnimation.camera() || !bootstrap.introCamera().valid()) {
            std::cerr << "Intro camera metadata was not reconstructed\n";
            return 1;
        }
        if (cameraAnimation.camera()->id != "Camera01-camera" ||
            cameraAnimation.camera()->targetNode != "#Camera01.Target-node" ||
            cameraAnimation.camera()->verticalFieldOfViewDegrees != 45.0F) {
            std::cerr << "Unexpected camera metadata: "
                      << cameraAnimation.camera()->id << ' '
                      << cameraAnimation.camera()->targetNode << ' '
                      << cameraAnimation.camera()->verticalFieldOfViewDegrees
                      << '\n';
            return 1;
        }
        const auto introPose = bootstrap.introCamera().sample(0);
        if (std::abs(introPose.position.x - 16014.4F) > 0.01F ||
            std::abs(introPose.target.x - 15381.5F) > 0.01F ||
            introPose.up.z != 1.0F || introPose.farPlane != 10000.0F) {
            std::cerr << "Unexpected intro camera pose: "
                      << introPose.position.x << ' ' << introPose.target.x
                      << ' ' << introPose.up.z << ' ' << introPose.farPlane
                      << '\n';
            return 1;
        }
        assert(bootstrap.introActors().size() == 8);
        assert(bootstrap.introActors().front().objectId == 288);
        assert(bootstrap.introActors().front().sceneNodeName == "SpiderMan");
        assert(bootstrap.introActors().front().animation.tracks().size() == 40);
        assert(bootstrap.introActors().front().mesh.geometries().size() == 1);
        assert(bootstrap.introActors().front().textures.size() == 2);
        const auto& spiderMaterial =
            bootstrap.introActors().front().mesh.materials().front();
        if (spiderMaterial.diffuseImageIndex != 0 ||
            spiderMaterial.secondaryImageIndex != 1 ||
            spiderMaterial.secondaryTextureMode != 0) {
            std::cerr << "Unexpected Spider-Man texture layers: ";
            if (spiderMaterial.diffuseImageIndex) {
                std::cerr << *spiderMaterial.diffuseImageIndex;
            } else {
                std::cerr << "none";
            }
            std::cerr << ", ";
            if (spiderMaterial.secondaryImageIndex) {
                std::cerr << *spiderMaterial.secondaryImageIndex;
            } else {
                std::cerr << "none";
            }
            std::cerr << ", mode " << spiderMaterial.secondaryTextureMode
                      << '\n';
            return 1;
        }
        assert(std::abs(bootstrap.introActors().front().worldTransform[12] -
                        14701.2F) < 0.1F);
        assert(std::abs(bootstrap.introActors()[1].worldTransform[12] -
                        16247.8F) < 0.1F);
        const auto& spiderSkins =
            bootstrap.introActors().front().mesh.skins();
        assert(spiderSkins.size() == 1);
        assert(spiderSkins.front().jointNames.size() == 38);
        assert(spiderSkins.front().inverseBindMatrices.size() == 38);
        assert(spiderSkins.front().vertexInfluences.size() == 641);
        assert(bootstrap.introActors().front().mesh.sceneNodes().size() >= 38);
        for (const std::string& jointName : spiderSkins.front().jointNames) {
            const auto* jointNode = bootstrap.introActors()
                                        .front()
                                        .mesh.findSceneNodeByScopeId(jointName);
            assert(jointNode != nullptr);
            assert(!jointNode->id.empty());
        }
        std::size_t spiderInfluenceCount = 0;
        for (const auto& influences :
             spiderSkins.front().vertexInfluences) {
            float totalWeight = 0.0F;
            for (const auto& influence : influences) {
                totalWeight += influence.weight;
            }
            assert(std::abs(totalWeight - 1.0F) < 0.0001F);
            spiderInfluenceCount += influences.size();
        }
        assert(spiderInfluenceCount == 974);
        std::vector<usm::assets::ColladaGeometry> spiderStartPose;
        std::vector<usm::assets::ColladaGeometry> spiderLaterPose;
        assert(usm::assets::evaluateColladaPose(
            bootstrap.introActors().front().mesh,
            bootstrap.introActors().front().animation, 0, spiderStartPose));
        assert(usm::assets::evaluateColladaPose(
            bootstrap.introActors().front().mesh,
            bootstrap.introActors().front().animation, 1000,
            spiderLaterPose));
        assert(spiderStartPose.size() == 1);
        assert(spiderStartPose.front().vertices.size() == 641);
        assert(spiderStartPose.front().vertices.front().position.x !=
               spiderLaterPose.front().vertices.front().position.x);
        const auto carActor = std::find_if(
            bootstrap.introActors().begin(), bootstrap.introActors().end(),
            [](const usm::game::CinematicActorAsset& actor) {
                return actor.objectId == 1262;
            });
        assert(carActor != bootstrap.introActors().end());
        assert(carActor->animationStartMilliseconds == 35300);
        assert(carActor->mesh.geometries().size() == 6);
        std::vector<usm::assets::ColladaGeometry> carStartPose;
        std::vector<usm::assets::ColladaGeometry> carLaterPose;
        assert(usm::assets::evaluateColladaPose(
            carActor->mesh, carActor->animation, 0, carStartPose));
        assert(usm::assets::evaluateColladaPose(
            carActor->mesh, carActor->animation,
            carActor->animation.durationMilliseconds(), carLaterPose));
        assert(carStartPose.size() == carLaterPose.size());
        bool carPoseChanged = false;
        for (std::size_t geometryIndex = 0;
             geometryIndex < carStartPose.size(); ++geometryIndex) {
            assert(carStartPose[geometryIndex].vertices.size() ==
                   carLaterPose[geometryIndex].vertices.size());
            for (std::size_t vertexIndex = 0;
                 vertexIndex < carStartPose[geometryIndex].vertices.size();
                 ++vertexIndex) {
                if (carStartPose[geometryIndex].vertices[vertexIndex].position.x !=
                        carLaterPose[geometryIndex].vertices[vertexIndex].position.x ||
                    carStartPose[geometryIndex].vertices[vertexIndex].position.y !=
                        carLaterPose[geometryIndex].vertices[vertexIndex].position.y ||
                    carStartPose[geometryIndex].vertices[vertexIndex].position.z !=
                        carLaterPose[geometryIndex].vertices[vertexIndex].position.z) {
                    carPoseChanged = true;
                    break;
                }
            }
        }
        if (!carPoseChanged) {
            std::cerr << "Police-car pose did not change. Tracks:";
            for (const auto& track : carActor->animation.tracks()) {
                std::cerr << ' ' << track.targetNode << '/' << track.id;
            }
            std::cerr << " Nodes:";
            for (const auto& node : carActor->mesh.sceneNodes()) {
                std::cerr << ' ' << node.id << '/' << node.scopeId;
            }
            std::cerr << '\n';
            return 1;
        }
        const auto cameraStart = cameraAnimation.tracks()[1].sample(0);
        const auto cameraMiddle = cameraAnimation.tracks()[1].sample(1000);
        assert(cameraStart.componentCount == 3);
        assert(cameraMiddle.componentCount == 3);
        assert(cameraStart.value != cameraMiddle.value);

        usm::game::CinematicPlayer cinematicPlayer;
        assert(cinematicPlayer.start(bootstrap.introScript()));
        assert(cinematicPlayer.durationMilliseconds() == 41800);
        std::vector<std::string> dispatchedCommands;
        const auto collectCommand =
            [&dispatchedCommands](const usm::game::CinematicThread&,
                                  const usm::game::CinematicCommand& command) {
                dispatchedCommands.push_back(command.name);
            };
        assert(cinematicPlayer.advanceTo(0, collectCommand));
        assert(dispatchedCommands.size() == 5);
        assert(dispatchedCommands.front() == "PlayDAEAnim");
        assert(cinematicPlayer.advanceTo(299, collectCommand));
        assert(dispatchedCommands.size() == 5);
        assert(cinematicPlayer.advanceTo(300, collectCommand));
        assert(dispatchedCommands.size() == 6);
        assert(dispatchedCommands.back() == "SoundControl");
        assert(cinematicPlayer.advanceTo(41800, collectCommand));
        assert(dispatchedCommands.size() == 38);
        assert(cinematicPlayer.finished());

        usm::audio::CinematicSoundBank introSounds;
        assert(introSounds.preload(bootstrap.introScript(), soundCatalog));
        assert(introSounds.loadedEventCount() == 17);
        assert(introSounds.unresolvedEvents().size() == 2);
        assert(std::find(introSounds.unresolvedEvents().begin(),
                         introSounds.unresolvedEvents().end(),
                         "SFX_THUG_KNIFE_HURT_1") !=
               introSounds.unresolvedEvents().end());
        assert(std::find(introSounds.unresolvedEvents().begin(),
                         introSounds.unresolvedEvents().end(),
                         "SFX_VERTICAL_IMPACT") !=
               introSounds.unresolvedEvents().end());
        std::size_t playedSoundCount = 0;
        assert(introSounds.dispatch(
            bootstrap.introScript().threads().front().commands.back(),
            [&playedSoundCount](const usm::audio::PcmAudio&, bool) {
                ++playedSoundCount;
                return usm::Result::success();
            }));
        assert(playedSoundCount == 1);

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
