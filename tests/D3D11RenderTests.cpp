#include "renderer/d3d11/D3D11Renderer.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/CinematicPlayer.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/LevelEffectRuntime.hpp"
#include "game/LevelHintRuntime.hpp"
#include "game/LevelObjectRuntime.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

void captureIfRequested(const usm::assets::RgbaImage& image,
                        std::string_view filename) {
    const char* directory = std::getenv("OPENANDROIDUSM_CAPTURE_DIR");
    if (directory == nullptr || *directory == '\0') {
        return;
    }
    const std::filesystem::path outputDirectory(directory);
    std::filesystem::create_directories(outputDirectory);
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    const std::uint32_t pixelBytes = image.width * image.height * 4;
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(image.width);
    infoHeader.biHeight = -static_cast<LONG>(image.height);
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = pixelBytes;
    std::vector<std::uint8_t> bgra(image.pixels.size());
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
        bgra[offset] = image.pixels[offset + 2];
        bgra[offset + 1] = image.pixels[offset + 1];
        bgra[offset + 2] = image.pixels[offset];
        bgra[offset + 3] = image.pixels[offset + 3];
    }
    std::ofstream stream(outputDirectory / filename, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    stream.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
    stream.write(reinterpret_cast<const char*>(bgra.data()),
                 static_cast<std::streamsize>(bgra.size()));
}

} // namespace

int main() {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    using namespace usm::assets;

    ColladaGeometry triangle;
    triangle.bounds = {{-1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    triangle.vertices = {
        {{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F}, 0xffffffff},
        {{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.5F, 0.0F}, 0xffffffff},
        {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {1.0F, 1.0F}, 0xffffffff},
    };
    ColladaMeshBuffer buffer;
    buffer.primitive = ColladaPrimitive::Triangles;
    buffer.indices = {0, 1, 2};
    triangle.meshBuffers.push_back(buffer);

    RgbaImage texture;
    texture.width = 1;
    texture.height = 1;
    texture.pixels = {220, 80, 40, 255};

    usm::renderer::D3D11Renderer renderer;
    assert(renderer.initializeOffscreen(64, 64));
    assert(renderer.uploadPreviewGeometry(triangle, {&texture, 1}));
    renderer.renderFrame();

    std::array<std::uint8_t, 4> center{};
    assert(renderer.readBackPixel(32, 32, center));
    assert(center[0] > 80);
    assert(center[1] > 20);
    assert(center[3] == 255);
    const usm::game::EnemyGunLineState gunLine{
        1, {0.5F, 0.0F, -0.1F}, {1.0F, 0.0F, 0.0F}, 30.0F, 100, true};
    assert(renderer.updateEnemyGunLines({&gunLine, 1}));
    renderer.renderFrame();
    assert(renderer.updateEnemyGunLines({}));

    const std::filesystem::path dataRoot = USM_TEST_GAME_DATA_ROOT;
    if (std::filesystem::exists(dataRoot / "levelnew_01.pack")) {
        usm::game::LevelOneBootstrap levelOne;
        assert(levelOne.load(dataRoot));
        usm::game::PlayerStateConfigDatabase playerStates;
        assert(playerStates.load(dataRoot));
        captureIfRequested(levelOne.hud().interfaceTexture.image(),
                           "interface-atlas.bmp");
        captureIfRequested(levelOne.effects().texture.image(),
                           "effects-atlas.bmp");
        for (std::size_t textureIndex = 0;
             textureIndex < levelOne.introSky().textures.size();
             ++textureIndex) {
            captureIfRequested(
                levelOne.introSky().textures[textureIndex].mipLevels().front(),
                "sky-texture-" + std::to_string(textureIndex) + ".bmp");
        }
        for (std::size_t textureIndex = 0;
             textureIndex < levelOne.roomTextures().size(); ++textureIndex) {
            captureIfRequested(
                levelOne.roomTextures()[textureIndex].mipLevels().front(),
                "room-texture-" + std::to_string(textureIndex) + ".bmp");
        }
        for (std::size_t textureIndex = 0;
             textureIndex < levelOne.introActors().front().textures.size();
             ++textureIndex) {
            captureIfRequested(
                levelOne.introActors()
                    .front()
                    .textures[textureIndex]
                    .mipLevels()
                    .front(),
                "spider-texture-" + std::to_string(textureIndex) + ".bmp");
        }

        usm::renderer::D3D11Renderer gameRenderer;
        const bool capturing =
            std::getenv("OPENANDROIDUSM_CAPTURE_DIR") != nullptr;
        const std::uint32_t captureWidth = capturing ? 1280U : 256U;
        const std::uint32_t captureHeight = capturing ? 720U : 256U;
        assert(gameRenderer.initializeOffscreen(captureWidth, captureHeight));
        const usm::Result uploadLevelResult =
            gameRenderer.uploadLevelOneScene(levelOne);
        if (!uploadLevelResult) {
            std::cerr << uploadLevelResult.message() << '\n';
            return 1;
        }
        usm::game::LevelObjectRuntime levelObjects;
        assert(levelObjects.initialize(levelOne));
        usm::game::GameplayCamera gameplayCamera;
        assert(gameplayCamera.bind(levelOne.cameraAreas(),
                                   levelOne.player().initialCameraAreaId));
        assert(gameRenderer.updateLevelOneObjects(levelOne, levelObjects));
        assert(gameRenderer.updateLevelOneActors(levelOne, 0));
        std::array<bool, 16> cinematicVisibleRooms{};
        std::fill_n(cinematicVisibleRooms.begin(), 5, true);
        gameRenderer.setCameraAreaRoomVisibility(
            gameplayCamera.mustInvisibleRooms(),
            gameplayCamera.mustVisibleRooms());
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(levelOne.introCamera().sample(0)));
        gameRenderer.renderFrame();

        RgbaImage rendered;
        assert(gameRenderer.readBackImage(rendered));
        captureIfRequested(rendered, "intro-00000.bmp");
        const std::size_t skyPixel =
            (rendered.width / 2U) * 4U;
        assert(rendered.pixels[skyPixel] + rendered.pixels[skyPixel + 1] +
                   rendered.pixels[skyPixel + 2] >
               50);
        std::size_t changedPixels = 0;
        for (std::size_t pixel = 0; pixel < rendered.pixels.size(); pixel += 4) {
            const bool isBackground = rendered.pixels[pixel] < 12 &&
                                      rendered.pixels[pixel + 1] < 18 &&
                                      rendered.pixels[pixel + 2] < 30;
            changedPixels += !isBackground;
        }
        assert(changedPixels > 100);

        assert(gameRenderer.setCamera(levelOne.introCamera().sample(10000)));
        assert(gameRenderer.updateLevelOneActors(levelOne, 10000));
        gameRenderer.renderFrame();
        RgbaImage laterFrame;
        assert(gameRenderer.readBackImage(laterFrame));
        captureIfRequested(laterFrame, "intro-10000.bmp");
        assert(laterFrame.pixels.size() == rendered.pixels.size());
        std::size_t changedBetweenFrames = 0;
        for (std::size_t component = 0; component < rendered.pixels.size();
             component += 4) {
            changedBetweenFrames +=
                rendered.pixels[component] != laterFrame.pixels[component] ||
                rendered.pixels[component + 1] !=
                    laterFrame.pixels[component + 1] ||
                rendered.pixels[component + 2] !=
                    laterFrame.pixels[component + 2];
        }
        assert(changedBetweenFrames > 100);

        assert(gameRenderer.setCamera(levelOne.introCamera().sample(20000)));
        assert(gameRenderer.updateLevelOneActors(levelOne, 20000));
        gameRenderer.renderFrame();
        RgbaImage actorFrame;
        assert(gameRenderer.readBackImage(actorFrame));
        captureIfRequested(actorFrame, "intro-20000.bmp");
        assert(actorFrame.pixels.size() == rendered.pixels.size());

        const auto* idleClip =
            levelOne.player().animationBank.findClip("idle_stand");
        assert(idleClip != nullptr);
        assert(gameRenderer.updateLevelOneActors(
            levelOne,
            levelOne.introCameraAnimation().durationMilliseconds()));
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, 0, levelOne.player().worldTransform));
        const auto gameplayPose =
            gameplayCamera.sample(levelOne.player().position);
        cinematicVisibleRooms.fill(false);
        gameRenderer.setCameraAreaRoomVisibility(
            gameplayCamera.mustInvisibleRooms(),
            gameplayCamera.mustVisibleRooms());
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(gameplayPose));
        gameRenderer.renderFrame();
        RgbaImage gameplayFrame;
        assert(gameRenderer.readBackImage(gameplayFrame));
        captureIfRequested(gameplayFrame, "gameplay-start.bmp");
        assert(gameplayFrame.pixels.size() == rendered.pixels.size());

        usm::game::LevelObjectRuntime comicObjects;
        assert(comicObjects.initialize(levelOne));
        for (const auto& object : comicObjects.states()) {
            assert(object.asset != nullptr);
            usm::game::CinematicThread objectThread;
            objectThread.objectId = object.asset->objectId;
            usm::game::CinematicCommand hideObject;
            hideObject.name = "SetVisible";
            hideObject.attributes.push_back({"bool", "Visible", "false"});
            assert(comicObjects.applyCinematicCommand(
                levelOne, objectThread, hideObject));
        }
        usm::game::CinematicThread comicThread;
        comicThread.objectId = 40010;
        usm::game::CinematicCommand showComic;
        showComic.name = "SetVisible";
        showComic.attributes.push_back({"bool", "Visible", "true"});
        assert(comicObjects.applyCinematicCommand(levelOne, comicThread,
                                                   showComic));
        usm::game::CinematicCommand moveComic;
        moveComic.name = "MoveObject";
        moveComic.attributes.push_back({"vector3d", "abspos", "0,0,0"});
        assert(comicObjects.applyCinematicCommand(levelOne, comicThread,
                                                   moveComic));
        assert(gameRenderer.updateLevelOneObjects(levelOne, comicObjects));
        cinematicVisibleRooms.fill(false);
        cinematicVisibleRooms[0] = true;
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        const usm::game::CameraPose comicCamera{
            {0.0F, -350.0F, 100.0F}, {0.0F, 0.0F, 25.0F},
            {0.0F, 0.0F, 1.0F}, 60.0F, 10.0F, 2000.0F};
        assert(gameRenderer.setCamera(comicCamera));
        gameRenderer.renderFrame();
        RgbaImage comicVisibleFrame;
        assert(gameRenderer.readBackImage(comicVisibleFrame));
        captureIfRequested(comicVisibleFrame, "gameplay-comic-cover.bmp");
        showComic.attributes.front().value = "false";
        assert(comicObjects.applyCinematicCommand(levelOne, comicThread,
                                                   showComic));
        assert(gameRenderer.updateLevelOneObjects(levelOne, comicObjects));
        gameRenderer.renderFrame();
        RgbaImage comicHiddenFrame;
        assert(gameRenderer.readBackImage(comicHiddenFrame));
        std::size_t comicChangedPixels = 0;
        for (std::size_t component = 0;
             component < comicVisibleFrame.pixels.size(); component += 4) {
            comicChangedPixels +=
                comicVisibleFrame.pixels[component] !=
                    comicHiddenFrame.pixels[component] ||
                comicVisibleFrame.pixels[component + 1] !=
                    comicHiddenFrame.pixels[component + 1] ||
                comicVisibleFrame.pixels[component + 2] !=
                    comicHiddenFrame.pixels[component + 2];
        }
        assert(comicChangedPixels > 20);

        const auto& renderedDrop = levelOne.dropObjects().front();
        assert(comicObjects.setRuntimeState(
            renderedDrop.objectId, {0.0F, 0.0F, 0.0F}, true, true));
        assert(gameRenderer.updateLevelOneObjects(levelOne, comicObjects));
        cinematicVisibleRooms.fill(false);
        cinematicVisibleRooms[static_cast<std::size_t>(
            renderedDrop.roomId - 1)] = true;
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(comicCamera));
        gameRenderer.renderFrame();
        RgbaImage dropVisibleFrame;
        assert(gameRenderer.readBackImage(dropVisibleFrame));
        captureIfRequested(dropVisibleFrame, "gameplay-falling-object.bmp");
        assert(comicObjects.setRuntimeState(
            renderedDrop.objectId, {0.0F, 0.0F, -150.0F}, false, false));
        assert(gameRenderer.updateLevelOneObjects(levelOne, comicObjects));
        gameRenderer.renderFrame();
        RgbaImage dropHiddenFrame;
        assert(gameRenderer.readBackImage(dropHiddenFrame));
        std::size_t dropChangedPixels = 0;
        for (std::size_t component = 0;
             component < dropVisibleFrame.pixels.size(); component += 4) {
            dropChangedPixels +=
                dropVisibleFrame.pixels[component] !=
                    dropHiddenFrame.pixels[component] ||
                dropVisibleFrame.pixels[component + 1] !=
                    dropHiddenFrame.pixels[component + 1] ||
                dropVisibleFrame.pixels[component + 2] !=
                    dropHiddenFrame.pixels[component + 2];
        }
        assert(dropChangedPixels > 20);
        assert(gameRenderer.updateLevelOneObjects(levelOne, levelObjects));
        cinematicVisibleRooms.fill(false);
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(gameplayPose));

        usm::game::LevelEffectRuntime environmentEffects;
        assert(environmentEffects.initialize(levelOne.effects().presets));
        usm::game::LevelBonusRuntime bonusRuntime;
        assert(bonusRuntime.initialize(levelOne.bonuses()));
        const auto& environmentEffect = levelOne.environmentEffects().front();
        assert(environmentEffects.addPersistentEffect(
            environmentEffect.effectType, environmentEffect.position,
            environmentEffect.roomId, environmentEffect.visible));
        const usm::game::CameraPose environmentEffectCamera{
            {environmentEffect.position.x,
             environmentEffect.position.y - 1200.0F,
             environmentEffect.position.z + 350.0F},
            {environmentEffect.position.x, environmentEffect.position.y,
             environmentEffect.position.z + 150.0F},
            {0.0F, 0.0F, 1.0F}, 70.0F, 10.0F, 5000.0F};
        cinematicVisibleRooms.fill(false);
        cinematicVisibleRooms[static_cast<std::size_t>(
            environmentEffect.roomId - 1)] = true;
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(environmentEffectCamera));
        assert(gameRenderer.updateLevelOneEffects(levelOne.effects(),
                                                   environmentEffects,
                                                   bonusRuntime));
        gameRenderer.renderFrame();
        RgbaImage environmentEffectBaseline;
        assert(gameRenderer.readBackImage(environmentEffectBaseline));
        captureIfRequested(environmentEffectBaseline,
                           "gameplay-environment-baseline.bmp");
        environmentEffects.update(100);
        assert(!environmentEffects.particles().empty());
        assert(gameRenderer.updateLevelOneEffects(levelOne.effects(),
                                                   environmentEffects,
                                                   bonusRuntime));
        gameRenderer.renderFrame();
        RgbaImage environmentEffectFrame;
        assert(gameRenderer.readBackImage(environmentEffectFrame));
        captureIfRequested(environmentEffectFrame,
                           "gameplay-environment-fire-smoke.bmp");
        std::size_t environmentEffectChangedPixels = 0;
        for (std::size_t component = 0;
             component < environmentEffectFrame.pixels.size();
             component += 4) {
            environmentEffectChangedPixels +=
                environmentEffectFrame.pixels[component] !=
                    environmentEffectBaseline.pixels[component] ||
                environmentEffectFrame.pixels[component + 1] !=
                    environmentEffectBaseline.pixels[component + 1] ||
                environmentEffectFrame.pixels[component + 2] !=
                    environmentEffectBaseline.pixels[component + 2];
        }
        assert(environmentEffectChangedPixels > 2);

        usm::game::LevelEffectRuntime bonusEffects;
        assert(bonusEffects.initialize(levelOne.effects().presets));
        usm::game::LevelBonusAsset bonus = levelOne.bonuses().front();
        bonus.position = gameplayPose.target;
        bonus.roomId = 1;
        const std::array renderBonuses{bonus};
        usm::game::LevelBonusRuntime renderBonusRuntime;
        assert(renderBonusRuntime.initialize(renderBonuses));
        const std::string_view bonusEffectType =
            bonus.type == usm::game::LevelBonusType::Health ? "bonus_green"
                                                            : "bonus_red";
        assert(bonusEffects.addPersistentEffect(
            bonusEffectType, bonus.position, bonus.roomId, bonus.visible,
            bonus.objectId));
        cinematicVisibleRooms.fill(false);
        cinematicVisibleRooms[0] = true;
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(gameplayPose));
        assert(gameRenderer.updateLevelOneEffects(
            levelOne.effects(), bonusEffects, renderBonusRuntime));
        gameRenderer.renderFrame();
        RgbaImage bonusBaseline;
        assert(gameRenderer.readBackImage(bonusBaseline));
        bonusEffects.update(100);
        bonusEffects.update(100);
        assert(!bonusEffects.particles().empty());
        assert(gameRenderer.updateLevelOneEffects(
            levelOne.effects(), bonusEffects, renderBonusRuntime));
        gameRenderer.renderFrame();
        RgbaImage bonusStationaryFrame;
        assert(gameRenderer.readBackImage(bonusStationaryFrame));
        captureIfRequested(bonusStationaryFrame,
                           "gameplay-bonus-stationary.bmp");
        std::size_t bonusStationaryChangedPixels = 0;
        for (std::size_t component = 0;
             component < bonusStationaryFrame.pixels.size(); component += 4) {
            bonusStationaryChangedPixels +=
                bonusStationaryFrame.pixels[component] !=
                    bonusBaseline.pixels[component] ||
                bonusStationaryFrame.pixels[component + 1] !=
                    bonusBaseline.pixels[component + 1] ||
                bonusStationaryFrame.pixels[component + 2] !=
                    bonusBaseline.pixels[component + 2];
        }
        assert(bonusStationaryChangedPixels > 2);
        renderBonusRuntime.update(
            {bonus.position.x, bonus.position.y, bonus.position.z - 100.0F},
            250);
        assert(renderBonusRuntime.orbs().size() == 1);
        assert(bonusEffects.setPersistentEffectVisible(bonus.objectId, false));
        bonusEffects.update(300);
        assert(gameRenderer.updateLevelOneEffects(
            levelOne.effects(), bonusEffects, renderBonusRuntime));
        gameRenderer.renderFrame();
        RgbaImage bonusOrbFrame;
        assert(gameRenderer.readBackImage(bonusOrbFrame));
        captureIfRequested(bonusOrbFrame, "gameplay-bonus-orb.bmp");
        std::size_t bonusOrbChangedPixels = 0;
        for (std::size_t component = 0;
             component < bonusOrbFrame.pixels.size(); component += 4) {
            bonusOrbChangedPixels +=
                bonusOrbFrame.pixels[component] !=
                    bonusBaseline.pixels[component] ||
                bonusOrbFrame.pixels[component + 1] !=
                    bonusBaseline.pixels[component + 1] ||
                bonusOrbFrame.pixels[component + 2] !=
                    bonusBaseline.pixels[component + 2];
        }
        assert(bonusOrbChangedPixels > 2);
        cinematicVisibleRooms.fill(false);
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(gameplayPose));

        usm::game::LevelEffectRuntime effects;
        assert(effects.initialize(levelOne.effects().presets));
        usm::game::CinematicCommand playEffect;
        playEffect.name = "PlayEffect";
        playEffect.attributes.push_back(
            {"string", "$EffectType", "cartoon_hit_splash_big"});
        const auto& effectPosition = gameplayPose.target;
        playEffect.attributes.push_back(
            {"vector3d", "abspos",
             std::to_string(effectPosition.x) + "," +
                 std::to_string(effectPosition.y) + "," +
                 std::to_string(effectPosition.z)});
        assert(effects.applyCinematicCommand(playEffect));
        effects.update(1);
        assert(!effects.particles().empty());
        assert(std::all_of(effects.particles().begin(),
                           effects.particles().end(),
                           [](const usm::game::EffectParticleState& particle) {
                               return (particle.color >> 24U) != 0;
                           }));
        assert(gameRenderer.updateLevelOneEffects(levelOne.effects(),
                                                   effects, bonusRuntime));
        gameRenderer.renderFrame();
        RgbaImage gameplayEffectFrame;
        assert(gameRenderer.readBackImage(gameplayEffectFrame));
        captureIfRequested(gameplayEffectFrame, "gameplay-hit-effect.bmp");
        std::size_t effectChangedPixels = 0;
        for (std::size_t component = 0;
             component < gameplayEffectFrame.pixels.size(); component += 4) {
            effectChangedPixels +=
                gameplayEffectFrame.pixels[component] !=
                    gameplayFrame.pixels[component] ||
                gameplayEffectFrame.pixels[component + 1] !=
                    gameplayFrame.pixels[component + 1] ||
                gameplayEffectFrame.pixels[component + 2] !=
                    gameplayFrame.pixels[component + 2];
        }
        assert(effectChangedPixels > 2);
        effects.update(300);
        assert(gameRenderer.updateLevelOneEffects(levelOne.effects(),
                                                   effects, bonusRuntime));
        const auto renderAuthoredEffect =
            [&](std::string_view effectType,
                std::uint32_t elapsedMilliseconds,
                std::string_view captureName) {
                assert(effects.initialize(levelOne.effects().presets));
                playEffect.attributes.front().value = effectType;
                assert(effects.applyCinematicCommand(playEffect));
                effects.update(elapsedMilliseconds);
                assert(!effects.particles().empty());
                assert(gameRenderer.updateLevelOneEffects(levelOne.effects(),
                                                           effects,
                                                           bonusRuntime));
                gameRenderer.renderFrame();
                RgbaImage frame;
                assert(gameRenderer.readBackImage(frame));
                captureIfRequested(frame, captureName);
                std::size_t changedPixels = 0;
                for (std::size_t component = 0;
                     component < frame.pixels.size(); component += 4) {
                    changedPixels +=
                        frame.pixels[component] !=
                            gameplayFrame.pixels[component] ||
                        frame.pixels[component + 1] !=
                            gameplayFrame.pixels[component + 1] ||
                        frame.pixels[component + 2] !=
                            gameplayFrame.pixels[component + 2];
                }
                assert(changedPixels > 2);
                return frame;
            };
        const RgbaImage explosionEffectFrame = renderAuthoredEffect(
            "explode_new", 120, "gameplay-explosion-effect.bmp");
        const RgbaImage rockSplashEffectFrame = renderAuthoredEffect(
            "rock_splash", 50, "gameplay-rock-splash-effect.bmp");
        assert(explosionEffectFrame.pixels != rockSplashEffectFrame.pixels);
        const auto beforeBossCinematic = std::find_if(
            levelOne.cinematics().begin(), levelOne.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1254;
            });
        assert(beforeBossCinematic != levelOne.cinematics().end());
        assert(gameRenderer.updateGameplayCinematicActors(
            levelOne, &*beforeBossCinematic, 0));
        assert(gameRenderer.setCamera(
            beforeBossCinematic->animatedCamera.sample(0)));
        gameRenderer.renderFrame();
        RgbaImage beforeBossStartFrame;
        assert(gameRenderer.readBackImage(beforeBossStartFrame));
        captureIfRequested(beforeBossStartFrame, "before-boss-00000.bmp");
        assert(gameRenderer.updateGameplayCinematicActors(
            levelOne, &*beforeBossCinematic, 16000));
        assert(gameRenderer.setCamera(
            beforeBossCinematic->animatedCamera.sample(16000)));
        gameRenderer.renderFrame();
        RgbaImage beforeBossSandmanFrame;
        assert(gameRenderer.readBackImage(beforeBossSandmanFrame));
        captureIfRequested(beforeBossSandmanFrame,
                           "before-boss-16000.bmp");
        std::size_t beforeBossChangedPixels = 0;
        for (std::size_t component = 0;
             component < beforeBossStartFrame.pixels.size(); component += 4) {
            beforeBossChangedPixels +=
                beforeBossStartFrame.pixels[component] !=
                    beforeBossSandmanFrame.pixels[component] ||
                beforeBossStartFrame.pixels[component + 1] !=
                    beforeBossSandmanFrame.pixels[component + 1] ||
                beforeBossStartFrame.pixels[component + 2] !=
                    beforeBossSandmanFrame.pixels[component + 2];
        }
        assert(beforeBossChangedPixels > 100);
        const auto renderColladaCinematicFrame =
            [&](std::int32_t cinematicId, std::uint32_t timestampMilliseconds,
                std::int32_t cameraAreaId, std::string_view captureName) {
                const auto cinematic = std::find_if(
                    levelOne.cinematics().begin(), levelOne.cinematics().end(),
                    [cinematicId](const auto& candidate) {
                        return candidate.objectId == cinematicId;
                    });
                assert(cinematic != levelOne.cinematics().end());
                assert(cinematic->hasColladaPlayback());
                assert(timestampMilliseconds <=
                       cinematic->colladaDurationMilliseconds);
                usm::game::LevelObjectRuntime cinematicObjects;
                assert(cinematicObjects.initialize(levelOne));
                usm::game::CinematicPlayer commandPlayer;
                assert(commandPlayer.start(cinematic->script));
                usm::Result commandResult = usm::Result::success();
                assert(commandPlayer.advanceTo(
                    timestampMilliseconds,
                    [&cinematicObjects, &commandResult, &levelOne](
                        const usm::game::CinematicThread& thread,
                        const usm::game::CinematicCommand& command) {
                        if (commandResult) {
                            commandResult =
                                cinematicObjects.applyCinematicCommand(
                                    levelOne, thread, command);
                        }
                    }));
                assert(commandResult);
                assert(gameRenderer.updateLevelOneObjects(levelOne,
                                                           cinematicObjects));
                assert(gameRenderer.updateGameplayCinematicActors(
                    levelOne, &*cinematic, timestampMilliseconds));
                assert(gameplayCamera.setCurrentArea(cameraAreaId));
                gameRenderer.setCameraAreaRoomVisibility(
                    gameplayCamera.mustInvisibleRooms(),
                    gameplayCamera.mustVisibleRooms());
                cinematicVisibleRooms.fill(false);
                gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
                assert(gameRenderer.setCamera(
                    cinematic->animatedCamera.sample(timestampMilliseconds)));
                gameRenderer.renderFrame();
                RgbaImage frame;
                assert(gameRenderer.readBackImage(frame));
                captureIfRequested(frame, captureName);
                return frame;
            };
        const auto countChangedPixels = [](const RgbaImage& first,
                                           const RgbaImage& second) {
            assert(first.pixels.size() == second.pixels.size());
            std::size_t changedPixels = 0;
            for (std::size_t component = 0;
                 component < first.pixels.size(); component += 4) {
                changedPixels +=
                    first.pixels[component] != second.pixels[component] ||
                    first.pixels[component + 1] !=
                        second.pixels[component + 1] ||
                    first.pixels[component + 2] !=
                        second.pixels[component + 2];
            }
            return changedPixels;
        };
        const RgbaImage levelEndingStartFrame =
            renderColladaCinematicFrame(1238, 0, 10100,
                                        "level-ending-00000.bmp");
        const RgbaImage levelEndingActorsFrame = renderColladaCinematicFrame(
            1238, 30000, 10100, "level-ending-30000.bmp");
        assert(countChangedPixels(levelEndingStartFrame,
                                  levelEndingActorsFrame) > 100);
        const RgbaImage gameOverStartFrame =
            renderColladaCinematicFrame(1267, 0, 283,
                                        "game-over-00000.bmp");
        const RgbaImage gameOverDialogueFrame = renderColladaCinematicFrame(
            1267, 20000, 283, "game-over-20000.bmp");
        assert(countChangedPixels(gameOverStartFrame,
                                  gameOverDialogueFrame) > 100);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, 0, levelOne.player().worldTransform));
        assert(gameRenderer.updateGameplayCinematicActors(levelOne, nullptr,
                                                          0));
        assert(gameRenderer.setCamera(
            gameplayCamera.sample(levelOne.player().position)));
        assert(gameRenderer.updatePlayerHud(levelOne.hud(), 0.65F, 0.85F,
                                            1.0F));
        gameRenderer.renderFrame();
        RgbaImage gameplayHudFrame;
        assert(gameRenderer.readBackImage(gameplayHudFrame));
        captureIfRequested(gameplayHudFrame, "gameplay-hud.bmp");
        std::size_t hudChangedPixels = 0;
        for (std::size_t component = 0;
             component < gameplayHudFrame.pixels.size(); component += 4) {
            hudChangedPixels +=
                gameplayHudFrame.pixels[component] !=
                    gameplayFrame.pixels[component] ||
                gameplayHudFrame.pixels[component + 1] !=
                    gameplayFrame.pixels[component + 1] ||
                gameplayHudFrame.pixels[component + 2] !=
                    gameplayFrame.pixels[component + 2];
        }
        assert(hudChangedPixels > 100);
        usm::game::LevelHintRuntime hintRuntime;
        assert(hintRuntime.initialize(levelOne.hints()));
        assert(gameRenderer.updateLevelOneHints(hintRuntime));
        cinematicVisibleRooms.fill(false);
        cinematicVisibleRooms[1] = true;
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        assert(gameRenderer.setCamera(gameplayPose));
        gameRenderer.renderFrame();
        RgbaImage hintHiddenFrame;
        assert(gameRenderer.readBackImage(hintHiddenFrame));
        usm::game::CinematicThread hintThread;
        hintThread.objectId = 1113;
        usm::game::CinematicCommand showHint;
        showHint.name = "SetVisible";
        showHint.attributes.push_back({"bool", "Visible", "true"});
        assert(hintRuntime.applyCinematicCommand(hintThread, showHint));
        hintRuntime.update(100, [&levelOne](std::int32_t objectId) {
            assert(objectId == levelOne.player().objectId);
            return levelOne.player().position;
        });
        assert(gameRenderer.updateLevelOneHints(hintRuntime));
        gameRenderer.renderFrame();
        RgbaImage hintVisibleFrame;
        assert(gameRenderer.readBackImage(hintVisibleFrame));
        captureIfRequested(hintVisibleFrame,
                           "gameplay-spider-sense-hint.bmp");
        assert(countChangedPixels(hintHiddenFrame, hintVisibleFrame) > 10);
        showHint.attributes.front().value = "false";
        assert(hintRuntime.applyCinematicCommand(hintThread, showHint));
        assert(gameRenderer.updateLevelOneHints(hintRuntime));
        cinematicVisibleRooms.fill(false);
        gameRenderer.setCinematicVisibleRooms(cinematicVisibleRooms);
        const usm::game::LevelBonusPopupState skillPointPopup{
            levelOne.player().position, 5, 0.25F, true};
        assert(gameRenderer.updatePlayerHud(
            levelOne.hud(), 0.65F, 0.85F, 1.0F, nullptr, 5, true,
            &skillPointPopup));
        gameRenderer.renderFrame();
        RgbaImage skillPointHudFrame;
        assert(gameRenderer.readBackImage(skillPointHudFrame));
        captureIfRequested(skillPointHudFrame,
                           "gameplay-skill-point-hud.bmp");
        assert(countChangedPixels(gameplayHudFrame, skillPointHudFrame) > 10);
        usm::game::LevelEnemyRuntime healthBarEnemies;
        assert(healthBarEnemies.initialize(levelOne));
        usm::game::CinematicThread showHealthThread;
        showHealthThread.objectId = levelOne.player().objectId;
        usm::game::CinematicCommand showHealthCommand;
        showHealthCommand.name = "ShowHealth";
        showHealthCommand.attributes.push_back(
            {"int", "ObjectID", "1199"});
        assert(healthBarEnemies.applyCinematicCommand(
            levelOne, showHealthThread, showHealthCommand));
        const usm::Result enemyHealthHudResult = gameRenderer.updatePlayerHud(
            levelOne.hud(), 0.65F, 0.85F, 1.0F,
            healthBarEnemies.shownHealthBarEnemy());
        if (!enemyHealthHudResult) {
            std::cerr << enemyHealthHudResult.message() << '\n';
            for (const std::size_t frameIndex : {0x68U, 0x69U, 0x6aU}) {
                for (const auto& module :
                     levelOne.hud().interfaceAtlas.modulesForFrame(frameIndex)) {
                    std::cerr << "frame " << frameIndex << " module "
                              << module.moduleIndex << " flags "
                              << static_cast<unsigned>(module.flags) << '\n';
                }
            }
            return 1;
        }
        gameRenderer.renderFrame();
        RgbaImage enemyHealthHudFrame;
        assert(gameRenderer.readBackImage(enemyHealthHudFrame));
        captureIfRequested(enemyHealthHudFrame,
                           "gameplay-sandman-health-hud.bmp");
        std::size_t enemyHealthChangedPixels = 0;
        for (std::size_t component = 0;
             component < enemyHealthHudFrame.pixels.size(); component += 4) {
            enemyHealthChangedPixels +=
                enemyHealthHudFrame.pixels[component] !=
                    gameplayHudFrame.pixels[component] ||
                enemyHealthHudFrame.pixels[component + 1] !=
                    gameplayHudFrame.pixels[component + 1] ||
                enemyHealthHudFrame.pixels[component + 2] !=
                    gameplayHudFrame.pixels[component + 2];
        }
        assert(enemyHealthChangedPixels > 100);
        usm::game::CinematicUiFrame tutorialUi;
        tutorialUi.text = u"Press [A] to jump";
        tutorialUi.textVisible = true;
        tutorialUi.letterboxVisible = true;
        tutorialUi.quickTimeEventVisible = true;
        tutorialUi.quickTimeEventProgress = 0.5F;
        assert(gameRenderer.updateCinematicUi(tutorialUi));
        gameRenderer.renderFrame();
        RgbaImage tutorialUiFrame;
        assert(gameRenderer.readBackImage(tutorialUiFrame));
        captureIfRequested(tutorialUiFrame, "gameplay-tutorial-ui.bmp");
        std::size_t tutorialUiChangedPixels = 0;
        for (std::size_t component = 0;
             component < tutorialUiFrame.pixels.size(); component += 4) {
            tutorialUiChangedPixels +=
                tutorialUiFrame.pixels[component] !=
                    gameplayHudFrame.pixels[component] ||
                tutorialUiFrame.pixels[component + 1] !=
                    gameplayHudFrame.pixels[component + 1] ||
                tutorialUiFrame.pixels[component + 2] !=
                    gameplayHudFrame.pixels[component + 2];
        }
        assert(tutorialUiChangedPixels > 100);
        assert(gameRenderer.updateCinematicUi({}));
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, idleClip->durationMilliseconds() / 2,
            levelOne.player().worldTransform));
        gameRenderer.renderFrame();
        RgbaImage gameplayMiddleFrame;
        assert(gameRenderer.readBackImage(gameplayMiddleFrame));
        captureIfRequested(gameplayMiddleFrame, "gameplay-idle-middle.bmp");

        usm::game::GameplayPlayer gameplayPlayer;
        usm::game::LevelCollision levelCollision;
        assert(levelCollision.build(levelOne.rooms()));
        assert(gameplayPlayer.initialize(levelOne.player(), &levelCollision));
        gameplayPlayer.update({0.0F, 1.0F},
                              gameplayCamera.sample(gameplayPlayer.position()),
                              750);
        (void)gameplayCamera.updateArea(gameplayPlayer.position(), 750);
        const auto* runClip =
            levelOne.player().animationBank.findClip("run");
        assert(runClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *runClip,
            gameplayPlayer.animationTimeMilliseconds(),
            gameplayPlayer.worldTransform()));
        assert(gameRenderer.setCamera(
            gameplayCamera.sample(gameplayPlayer.position())));
        gameRenderer.renderFrame();
        RgbaImage gameplayRunningFrame;
        assert(gameRenderer.readBackImage(gameplayRunningFrame));
        captureIfRequested(gameplayRunningFrame, "gameplay-running.bmp");
        std::size_t gameplayChangedPixels = 0;
        for (std::size_t component = 0;
             component < gameplayRunningFrame.pixels.size(); component += 4) {
            gameplayChangedPixels +=
                gameplayRunningFrame.pixels[component] !=
                    gameplayMiddleFrame.pixels[component] ||
                gameplayRunningFrame.pixels[component + 1] !=
                    gameplayMiddleFrame.pixels[component + 1] ||
                gameplayRunningFrame.pixels[component + 2] !=
                    gameplayMiddleFrame.pixels[component + 2];
        }
        assert(gameplayChangedPixels > 100);

        usm::game::GameplayPlayer jumpingPlayer;
        assert(jumpingPlayer.initialize(levelOne.player(), &levelCollision,
                                        &playerStates));
        assert(jumpingPlayer.requestJump());
        jumpingPlayer.update({},
                             gameplayCamera.sample(jumpingPlayer.position()),
                             150);
        const auto* jumpClip = levelOne.player().animationBank.findClip(
            jumpingPlayer.activeAnimation());
        assert(jumpClip != nullptr);
        assert(jumpingPlayer.animatedFootHeight() >
               jumpingPlayer.worldTransform()[14] + 100.0F);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *jumpClip,
            jumpingPlayer.animationTimeMilliseconds(),
            jumpingPlayer.worldTransform()));
        assert(gameRenderer.setCamera(
            gameplayCamera.sample(jumpingPlayer.position())));
        gameRenderer.renderFrame();
        RgbaImage playerJumpFrame;
        assert(gameRenderer.readBackImage(playerJumpFrame));
        captureIfRequested(playerJumpFrame, "gameplay-player-jump.bmp");
        std::size_t jumpChangedPixels = 0;
        for (std::size_t component = 0;
             component < playerJumpFrame.pixels.size(); component += 4) {
            jumpChangedPixels +=
                playerJumpFrame.pixels[component] !=
                    gameplayRunningFrame.pixels[component] ||
                playerJumpFrame.pixels[component + 1] !=
                    gameplayRunningFrame.pixels[component + 1] ||
                playerJumpFrame.pixels[component + 2] !=
                    gameplayRunningFrame.pixels[component + 2];
        }
        assert(jumpChangedPixels > 100);

        usm::assets::Vector3 testWebAttach = jumpingPlayer.position();
        testWebAttach.z += 100.0F;
        usm::assets::Vector3 testWebAnchor = testWebAttach;
        testWebAnchor.x += 100.0F;
        testWebAnchor.z += 500.0F;
        assert(gameRenderer.updateWebLine(true, testWebAnchor,
                                          testWebAttach));
        gameRenderer.renderFrame();
        RgbaImage playerWebLineFrame;
        assert(gameRenderer.readBackImage(playerWebLineFrame));
        captureIfRequested(playerWebLineFrame, "gameplay-web-line.bmp");
        std::size_t webLineChangedPixels = 0;
        for (std::size_t component = 0;
             component < playerWebLineFrame.pixels.size(); component += 4) {
            webLineChangedPixels +=
                playerWebLineFrame.pixels[component] !=
                    playerJumpFrame.pixels[component] ||
                playerWebLineFrame.pixels[component + 1] !=
                    playerJumpFrame.pixels[component + 1] ||
                playerWebLineFrame.pixels[component + 2] !=
                    playerJumpFrame.pixels[component + 2];
        }
        assert(webLineChangedPixels > 2);
        assert(gameRenderer.updateWebLine(false));

        assert(gameplayPlayer.requestPunch());
        gameplayPlayer.update({},
                              gameplayCamera.sample(gameplayPlayer.position()),
                              180);
        const auto* punchClip = levelOne.player().animationBank.findClip(
            gameplayPlayer.activeAnimation());
        assert(punchClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *punchClip,
            gameplayPlayer.animationTimeMilliseconds(),
            gameplayPlayer.worldTransform()));
        gameRenderer.renderFrame();
        RgbaImage playerPunchFrame;
        assert(gameRenderer.readBackImage(playerPunchFrame));
        captureIfRequested(playerPunchFrame, "gameplay-player-punch.bmp");
        std::size_t punchChangedPixels = 0;
        for (std::size_t component = 0;
             component < playerPunchFrame.pixels.size(); component += 4) {
            punchChangedPixels +=
                playerPunchFrame.pixels[component] !=
                    gameplayRunningFrame.pixels[component] ||
                playerPunchFrame.pixels[component + 1] !=
                    gameplayRunningFrame.pixels[component + 1] ||
                playerPunchFrame.pixels[component + 2] !=
                    gameplayRunningFrame.pixels[component + 2];
        }
        assert(punchChangedPixels > 100);

        const auto encounterCinematic = std::find_if(
            levelOne.cinematics().begin(), levelOne.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1162;
            });
        assert(encounterCinematic != levelOne.cinematics().end());
        usm::game::LevelEnemyRuntime enemies;
        assert(enemies.initialize(levelOne));
        usm::game::CinematicPlayer encounterPlayer;
        assert(encounterPlayer.start(encounterCinematic->script));
        usm::Result encounterResult = usm::Result::success();
        assert(encounterPlayer.advanceTo(
            1600, [&](const usm::game::CinematicThread& thread,
                      const usm::game::CinematicCommand& command) {
                if (encounterResult) {
                    encounterResult = enemies.applyCinematicCommand(
                        levelOne, thread, command);
                }
            }));
        assert(encounterResult);
        for (const std::int32_t encounterEnemyId : {394, 395, 397}) {
            const auto* encounterEnemy = enemies.find(encounterEnemyId);
            assert(encounterEnemy != nullptr);
            assert(encounterEnemy->visible);
        }
        const auto* encounterKnife = enemies.find(394);
        const auto* encounterBat = enemies.find(395);
        const auto* encounterKnifeTwo = enemies.find(397);
        const auto planarDistanceSquared =
            [](const usm::game::LevelEnemyState& left,
               const usm::game::LevelEnemyState& right) {
                const float x = left.position.x - right.position.x;
                const float y = left.position.y - right.position.y;
                return x * x + y * y;
            };
        assert(planarDistanceSquared(*encounterKnife, *encounterBat) > 2500.0F);
        assert(planarDistanceSquared(*encounterKnife, *encounterKnifeTwo) >
               2500.0F);
        assert(planarDistanceSquared(*encounterBat, *encounterKnifeTwo) >
               2500.0F);
        enemies.advanceAnimations(750);
        assert(gameRenderer.updateLevelOneEnemies(levelOne, enemies));
        const float encounterCenterX =
            (encounterKnife->position.x + encounterBat->position.x +
             encounterKnifeTwo->position.x) /
            3.0F;
        const float encounterCenterY =
            (encounterKnife->position.y + encounterBat->position.y +
             encounterKnifeTwo->position.y) /
            3.0F;
        const auto [minimumEncounterX, maximumEncounterX] = std::minmax(
            {encounterKnife->position.x, encounterBat->position.x,
             encounterKnifeTwo->position.x});
        const auto [minimumEncounterY, maximumEncounterY] = std::minmax(
            {encounterKnife->position.y, encounterBat->position.y,
             encounterKnifeTwo->position.y});
        const float encounterCameraDistance =
            std::max(1300.0F,
                     std::max(maximumEncounterX - minimumEncounterX,
                              maximumEncounterY - minimumEncounterY) *
                         1.5F);
        const usm::game::CameraPose encounterCamera{
            {encounterCenterX, encounterCenterY - encounterCameraDistance,
             550.0F},
            {encounterCenterX, encounterCenterY, 160.0F},
            {0.0F, 0.0F, 1.0F},
            70.0F,
            10.0F,
            5000.0F};
        assert(gameRenderer.setCamera(encounterCamera));
        gameRenderer.renderFrame();
        RgbaImage encounterFrame;
        assert(gameRenderer.readBackImage(encounterFrame));
        captureIfRequested(encounterFrame, "gameplay-first-encounter.bmp");
        std::size_t encounterChangedPixels = 0;
        for (std::size_t component = 0;
             component < encounterFrame.pixels.size(); component += 4) {
            encounterChangedPixels +=
                encounterFrame.pixels[component] !=
                    gameplayMiddleFrame.pixels[component] ||
                encounterFrame.pixels[component + 1] !=
                    gameplayMiddleFrame.pixels[component + 1] ||
                encounterFrame.pixels[component + 2] !=
                    gameplayMiddleFrame.pixels[component + 2];
        }
        assert(encounterChangedPixels > 100);

        usm::game::CinematicThread chasingEnemyThread;
        chasingEnemyThread.objectId = 394;
        usm::game::CinematicCommand enableEnemyAi;
        enableEnemyAi.name = "EnableAI";
        assert(enemies.applyCinematicCommand(
            levelOne, chasingEnemyThread, enableEnemyAi));
        const auto* chasingEnemy = enemies.find(394);
        assert(chasingEnemy != nullptr);
        const usm::assets::Vector3 chaseTarget{
            chasingEnemy->position.x + 1000.0F, chasingEnemy->position.y,
            chasingEnemy->position.z};
        enemies.updateGameplay(500, chaseTarget);
        chasingEnemy = enemies.find(394);
        assert(chasingEnemy->behavior ==
               usm::game::EnemyBehaviorState::Chasing);
        assert(gameRenderer.updateLevelOneEnemies(levelOne, enemies));
        const usm::game::CameraPose chaseCamera{
            {chasingEnemy->position.x, chasingEnemy->position.y - 1100.0F,
             350.0F},
            {chasingEnemy->position.x, chasingEnemy->position.y, 120.0F},
            {0.0F, 0.0F, 1.0F},
            50.0F,
            10.0F,
            3000.0F};
        assert(gameRenderer.setCamera(chaseCamera));
        gameRenderer.renderFrame();
        RgbaImage chaseFrame;
        assert(gameRenderer.readBackImage(chaseFrame));
        captureIfRequested(chaseFrame, "gameplay-enemy-chase.bmp");
        std::size_t chaseChangedPixels = 0;
        for (std::size_t component = 0;
             component < chaseFrame.pixels.size(); component += 4) {
            chaseChangedPixels +=
                chaseFrame.pixels[component] !=
                    encounterFrame.pixels[component] ||
                chaseFrame.pixels[component + 1] !=
                    encounterFrame.pixels[component + 1] ||
                chaseFrame.pixels[component + 2] !=
                    encounterFrame.pixels[component + 2];
        }
        assert(chaseChangedPixels > 100);

        const usm::assets::Vector3 enemyHitOrigin{
            chasingEnemy->position.x - 100.0F, chasingEnemy->position.y,
            chasingEnemy->position.z};
        assert(enemies.applyPlayerMeleeHit(
            enemyHitOrigin, {1.0F, 0.0F, 0.0F}, 200.0F, 35.0F));
        chasingEnemy = enemies.find(394);
        assert(chasingEnemy->behavior ==
               usm::game::EnemyBehaviorState::Hurt);
        const auto* hurtClip =
            levelOne.enemyArchetypes()[chasingEnemy->asset->archetypeIndex]
                .animationBank.findClip(chasingEnemy->activeAnimation);
        assert(hurtClip != nullptr);
        enemies.advanceAnimations(hurtClip->durationMilliseconds() / 2U);
        assert(gameRenderer.updateLevelOneEnemies(levelOne, enemies));
        gameRenderer.renderFrame();
        RgbaImage hurtFrame;
        assert(gameRenderer.readBackImage(hurtFrame));
        captureIfRequested(hurtFrame, "gameplay-enemy-hurt.bmp");

        assert(enemies.applyPlayerMeleeHit(
            enemyHitOrigin, {1.0F, 0.0F, 0.0F}, 200.0F, 1000.0F));
        chasingEnemy = enemies.find(394);
        assert(chasingEnemy->behavior ==
               usm::game::EnemyBehaviorState::Dead);
        const auto* deathClip =
            levelOne.enemyArchetypes()[chasingEnemy->asset->archetypeIndex]
                .animationBank.findClip(chasingEnemy->activeAnimation);
        assert(deathClip != nullptr);
        enemies.advanceAnimations(deathClip->durationMilliseconds() + 500U);
        assert(gameRenderer.updateLevelOneEnemies(levelOne, enemies));
        gameRenderer.renderFrame();
        RgbaImage deathFrame;
        assert(gameRenderer.readBackImage(deathFrame));
        captureIfRequested(deathFrame, "gameplay-enemy-death.bmp");
        std::size_t deathChangedPixels = 0;
        for (std::size_t component = 0;
             component < deathFrame.pixels.size(); component += 4) {
            deathChangedPixels +=
                deathFrame.pixels[component] != hurtFrame.pixels[component] ||
                deathFrame.pixels[component + 1] !=
                    hurtFrame.pixels[component + 1] ||
                deathFrame.pixels[component + 2] !=
                    hurtFrame.pixels[component + 2];
        }
        assert(deathChangedPixels > 100);

        const auto& finalRoom = levelOne.rooms().back();
        usm::assets::Vector3 minimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        usm::assets::Vector3 maximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        for (const usm::assets::ColladaGeometry& geometry :
             finalRoom.geometry.sceneGeometries()) {
            for (const usm::assets::ColladaVertex& vertex :
                 geometry.vertices) {
                minimum.x = std::min(minimum.x, vertex.position.x);
                minimum.y = std::min(minimum.y, vertex.position.y);
                minimum.z = std::min(minimum.z, vertex.position.z);
                maximum.x = std::max(maximum.x, vertex.position.x);
                maximum.y = std::max(maximum.y, vertex.position.y);
                maximum.z = std::max(maximum.z, vertex.position.z);
            }
        }
        const float finalRoomExtent =
            std::max({maximum.x - minimum.x, maximum.y - minimum.y,
                      maximum.z - minimum.z});
        assert(finalRoomExtent > 100.0F);
        const auto finalCameraArea = std::find_if(
            levelOne.cameraAreas().begin(), levelOne.cameraAreas().end(),
            [](const usm::game::CameraArea& area) {
                return area.objectId == 10100;
            });
        assert(finalCameraArea != levelOne.cameraAreas().end());
        usm::assets::Vector3 finalRoomFocus{};
        for (const auto& point : finalCameraArea->controlPoints) {
            finalRoomFocus.x += point.position.x * 0.25F;
            finalRoomFocus.y += point.position.y * 0.25F;
            finalRoomFocus.z += point.position.z * 0.25F;
        }
        usm::game::GameplayCamera finalRoomCamera;
        assert(finalRoomCamera.bind(levelOne.cameraAreas(), 10100));
        const auto finalRoomPose = finalRoomCamera.sample(finalRoomFocus);
        assert(gameRenderer.setCamera(finalRoomPose));
        gameRenderer.renderFrame();
        RgbaImage finalRoomFrame;
        assert(gameRenderer.readBackImage(finalRoomFrame));
        captureIfRequested(finalRoomFrame, "gameplay-room13-overview.bmp");
    }
    return 0;
}
