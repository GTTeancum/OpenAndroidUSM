#include "renderer/d3d11/D3D11Renderer.hpp"
#include "assets/ColladaSkinning.hpp"
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
#include <cmath>
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

    // CAnimObjEffect::Init snapshots only the bone position while retaining
    // the player's facing. A live attachment inherits the bone rotation too.
    const std::array<float, 16> effectPlayerWorld{
        0.0F, 1.0F, 0.0F, 0.0F,
        -1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        10.0F, 20.0F, 30.0F, 1.0F};
    const std::array<float, 16> effectBoneTransform{
        -1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, -1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        5.0F, 6.0F, 7.0F, 1.0F};
    const auto snapshotEffectTransform =
        usm::renderer::resolvePlayerHitEffectWorldTransform(
            effectPlayerWorld, effectBoneTransform, false);
    const auto attachedEffectTransform =
        usm::renderer::resolvePlayerHitEffectWorldTransform(
            effectPlayerWorld, effectBoneTransform, true);
    const auto driftingAttachedEffectTransform =
        usm::renderer::resolvePlayerHitEffectWorldTransform(
            effectPlayerWorld, effectBoneTransform, true,
            {30.0F, -20.0F, 10.0F});
    assert(snapshotEffectTransform[0] == effectPlayerWorld[0]);
    assert(snapshotEffectTransform[1] == effectPlayerWorld[1]);
    assert(snapshotEffectTransform[4] == effectPlayerWorld[4]);
    assert(snapshotEffectTransform[5] == effectPlayerWorld[5]);
    assert(snapshotEffectTransform[12] == 4.0F);
    assert(snapshotEffectTransform[13] == 25.0F);
    assert(snapshotEffectTransform[14] == 37.0F);
    assert(attachedEffectTransform[0] != snapshotEffectTransform[0] ||
           attachedEffectTransform[1] != snapshotEffectTransform[1]);
    assert(attachedEffectTransform[12] == snapshotEffectTransform[12]);
    assert(attachedEffectTransform[13] == snapshotEffectTransform[13]);
    assert(attachedEffectTransform[14] == snapshotEffectTransform[14]);
    // CAnimObjEffect::Update changes the parented scene node's relative
    // position, so the live bone basis rotates this numeric velocity offset.
    assert(driftingAttachedEffectTransform[12] ==
           attachedEffectTransform[12] - 20.0F);
    assert(driftingAttachedEffectTransform[13] ==
           attachedEffectTransform[13] - 30.0F);
    assert(driftingAttachedEffectTransform[14] ==
           attachedEffectTransform[14] + 10.0F);

    ColladaGeometry triangle;
    triangle.bounds = {{-1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}};
    triangle.vertices = {
        {{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F}, 0xffffffff},
        {{0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.5F, 0.0F}, 0xffffffff},
        {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {1.0F, 1.0F}, 0xffffffff},
    };
    ColladaMeshBuffer buffer;
    buffer.primitive = ColladaPrimitive::Triangles;
    // Match the counter-clockwise front-face winding used by the shipped
    // level geometry and the reconstructed D3D11 rasterizer state.
    buffer.indices = {0, 2, 1};
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

    // Native GL_MODULATE combines the sampled texture with baked COLOR0 and
    // does not add a second directional-light term.
    for (ColladaVertex& vertex : triangle.vertices) {
        vertex.color = 0xff806040U;
    }
    texture.pixels = {255, 255, 255, 255};
    assert(renderer.uploadPreviewGeometry(triangle, {&texture, 1}));
    renderer.renderFrame();
    assert(renderer.readBackPixel(32, 32, center));
    assert(center[0] >= 126 && center[0] <= 130);
    assert(center[1] >= 94 && center[1] <= 98);
    assert(center[2] >= 62 && center[2] <= 66);
    assert(center[3] == 255);
    const usm::game::EnemyGunLineState gunLine{
        1, {0.5F, 0.0F, -0.1F}, {1.0F, 0.0F, 0.0F}, 30.0F, 100, true};
    assert(renderer.updateEnemyGunLines({&gunLine, 1}));
    renderer.renderFrame();
    assert(renderer.updateEnemyGunLines({}));

    triangle.meshBuffers.front().indices = {0, 1, 2};
    assert(renderer.uploadPreviewGeometry(triangle, {&texture, 1}));
    renderer.renderFrame();
    assert(renderer.readBackPixel(32, 32, center));
    assert(center[0] < 20);
    assert(center[1] < 20);
    assert(center[2] < 35);

    const std::filesystem::path dataRoot = USM_TEST_GAME_DATA_ROOT;
    if (std::filesystem::exists(dataRoot / "levelnew_01.pack")) {
        usm::game::LevelOneBootstrap levelOne;
        assert(levelOne.load(dataRoot));
        usm::game::PlayerStateConfigDatabase playerStates;
        assert(playerStates.load(dataRoot));
        captureIfRequested(levelOne.hud().interfaceTexture.image(),
                           "interface-atlas.bmp");
        captureIfRequested(levelOne.hud().tutorialTexture.image(),
                           "tutorial-atlas.bmp");
        captureIfRequested(levelOne.effects().texture.image(),
                           "effects-atlas.bmp");
        for (std::size_t effectId = 22; effectId <= 25; ++effectId) {
            const auto& effect = levelOne.playerHitEffects()[effectId];
            for (std::size_t textureIndex = 0;
                 textureIndex < effect.textures.size(); ++textureIndex) {
                captureIfRequested(
                    effect.textures[textureIndex].mipLevels().front(),
                    "combat-effect-" + std::to_string(effectId) +
                        "-texture-" + std::to_string(textureIndex) +
                        ".bmp");
            }
        }
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
        const auto carActor = std::find_if(
            levelOne.introActors().begin(), levelOne.introActors().end(),
            [](const usm::game::CinematicActorAsset& actor) {
                return actor.objectId == 1262;
            });
        assert(carActor != levelOne.introActors().end());
        for (std::size_t textureIndex = 0;
             textureIndex < carActor->textures.size(); ++textureIndex) {
            captureIfRequested(
                carActor->textures[textureIndex].mipLevels().front(),
                "police-car-texture-" + std::to_string(textureIndex) +
                    ".bmp");
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
        usm::game::CinematicUiFrame deathBlackFrame;
        deathBlackFrame.blackOverlayAlpha = 1.0F;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), deathBlackFrame));
        usm::game::DeathConfirmationFrame deathConfirmation;
        deathConfirmation.visible = true;
        deathConfirmation.selection = 0;
        deathConfirmation.title = u"Confirmation";
        deathConfirmation.message = u"Rhino has escaped. Retry?";
        deathConfirmation.yes = u"YES";
        deathConfirmation.no = u"NO";
        assert(gameRenderer.updateDeathConfirmation(levelOne.hud(),
                                                    deathConfirmation));
        gameRenderer.renderFrame();
        RgbaImage confirmationFrame;
        assert(gameRenderer.readBackImage(confirmationFrame));
        captureIfRequested(confirmationFrame,
                           "gameplay-death-confirmation.bmp");
        std::size_t confirmationColoredPixels = 0;
        for (std::size_t component = 0;
             component < confirmationFrame.pixels.size(); component += 4) {
            confirmationColoredPixels +=
                confirmationFrame.pixels[component] > 12 ||
                confirmationFrame.pixels[component + 1] > 12 ||
                confirmationFrame.pixels[component + 2] > 12;
        }
        assert(confirmationColoredPixels > 1000);
        usm::game::DeathConfirmationFrame exitLoading;
        exitLoading.loadingVisible = true;
        exitLoading.loadingLabel = *levelOne.textCatalog().main().at(0x13);
        exitLoading.loadingSuffix = *levelOne.textCatalog().main().at(0x26a);
        assert(gameRenderer.updateDeathConfirmation(levelOne.hud(),
                                                    exitLoading));
        gameRenderer.renderFrame();
        RgbaImage exitLoadingFrame;
        assert(gameRenderer.readBackImage(exitLoadingFrame));
        std::size_t loadingColoredPixels = 0;
        for (std::size_t component = 0;
             component < exitLoadingFrame.pixels.size(); component += 4) {
            loadingColoredPixels +=
                exitLoadingFrame.pixels[component] > 12 ||
                exitLoadingFrame.pixels[component + 1] > 12 ||
                exitLoadingFrame.pixels[component + 2] > 12;
        }
        assert(loadingColoredPixels > 10);
        assert(gameRenderer.updateDeathConfirmation(levelOne.hud(), {}));
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), {}));
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

        RgbaImage policeCarFrame;
        for (const std::uint32_t timestampMilliseconds :
             {35300U, 37000U, 39000U, 41000U, 41800U, 45000U, 47000U,
              48000U, 50000U, 53033U}) {
            assert(gameRenderer.setCamera(
                levelOne.introCamera().sample(timestampMilliseconds)));
            assert(gameRenderer.updateLevelOneActors(
                levelOne, timestampMilliseconds));
            gameRenderer.renderFrame();
            assert(gameRenderer.readBackImage(policeCarFrame));
            captureIfRequested(
                policeCarFrame,
                "intro-" + std::to_string(timestampMilliseconds) +
                    "-police-car.bmp");
            assert(policeCarFrame.pixels.size() == rendered.pixels.size());
        }

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

        // Player::UpdateNormalEffect (0x00348f24) emits the authored BDAE
        // trail on the same frame as the attack hit. Render the identical
        // player pose before and after enabling the trail so this regression
        // cannot pass from animation movement alone.
        usm::game::GameplayPlayer hitEffectPlayer;
        assert(hitEffectPlayer.initialize(
            levelOne.player(), nullptr, &playerStates, {}, {}, {}, nullptr,
            &levelOne.playerHitEffectConfigs()));
        assert(hitEffectPlayer.requestPunch());
        hitEffectPlayer.update({}, gameplayPose, 174);
        assert(hitEffectPlayer.hitEffects().empty());
        hitEffectPlayer.update({}, gameplayPose, 1);
        assert(hitEffectPlayer.hitEffects().size() == 1);
        const auto* hitTrailPunchClip = levelOne.player().animationBank.findClip(
            hitEffectPlayer.activeAnimation());
        assert(hitTrailPunchClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *hitTrailPunchClip,
            hitEffectPlayer.animationTimeMilliseconds(),
            hitEffectPlayer.worldTransform()));
        gameRenderer.renderFrame();
        RgbaImage punchWithoutTrail;
        assert(gameRenderer.readBackImage(punchWithoutTrail));
        assert(gameRenderer.updatePlayerHitEffects(levelOne,
                                                   hitEffectPlayer));
        gameRenderer.renderFrame();
        RgbaImage punchWithTrail;
        assert(gameRenderer.readBackImage(punchWithTrail));
        captureIfRequested(punchWithTrail, "gameplay-punch-hit-trail.bmp");
        std::size_t hitTrailChangedPixels = 0;
        std::size_t hitTrailDarkenedChannels = 0;
        for (std::size_t component = 0;
             component < punchWithTrail.pixels.size(); component += 4) {
            hitTrailChangedPixels +=
                punchWithoutTrail.pixels[component] !=
                    punchWithTrail.pixels[component] ||
                punchWithoutTrail.pixels[component + 1] !=
                    punchWithTrail.pixels[component + 1] ||
                punchWithoutTrail.pixels[component + 2] !=
                    punchWithTrail.pixels[component + 2];
            for (std::size_t channel = 0; channel < 3; ++channel) {
                hitTrailDarkenedChannels +=
                    punchWithTrail.pixels[component + channel] <
                    punchWithoutTrail.pixels[component + channel];
            }
        }
        assert(hitTrailChangedPixels > 5);
        // Ordinary trails select native material 0x1d. Registration order in
        // Application::Init (0x003e1c78) resolves that to
        // ADDITIVE_MODULATE_NONTRANSPARENT; its onSetMaterial at 0x00396f68
        // installs GL_SRC_ALPHA,GL_ONE. The effect may brighten or saturate a
        // destination channel, but it cannot darken one.
        assert(hitTrailDarkenedChannels == 0);

        usm::game::GameplayPlayer noHitEffectsPlayer;
        assert(noHitEffectsPlayer.initialize(levelOne.player()));

        usm::game::GameplayPlayer ultimateEffectPlayer;
        assert(ultimateEffectPlayer.initialize(
            levelOne.player(), nullptr, &playerStates, {}, {}, {}, nullptr,
            &levelOne.playerHitEffectConfigs(),
            levelOne.playerHitEffects()));
        assert(ultimateEffectPlayer.requestUltimate());
        const auto* ultimatePrepare =
            playerStates.findState("k_state_ultimate_prepare");
        assert(ultimatePrepare != nullptr);
        const auto& ultimatePreparePrimary =
            levelOne.player().animationBank
                .clips()[ultimatePrepare->primaryAnimationId];
        const auto& ultimatePrepareSecondary =
            levelOne.player().animationBank
                .clips()[ultimatePrepare->animationIds.front()];
        // Native SwitchToNextLinkAnim returns from the current player tick at
        // an animation-link boundary. Exercise the two authored prepare links
        // as separate ticks instead of asking one oversized render-test update
        // to cross both boundaries.
        ultimateEffectPlayer.update(
            {}, gameplayPose,
            ultimatePreparePrimary.durationMilliseconds());
        assert(ultimateEffectPlayer.activeStateId() == 107);
        ultimateEffectPlayer.update(
            {}, gameplayPose,
            ultimatePrepareSecondary.durationMilliseconds());
        assert(ultimateEffectPlayer.activeStateId() == 108);
        // Effects reaching zero in CAnimObjEffect::Update remain renderable
        // until IsAlive reclaims them at the start of the next manager tick.
        // The prepare effect (24), four wheel meshes (22), and pulse (23) are
        // therefore all present at this exact boundary.
        assert(ultimateEffectPlayer.hitEffects().size() == 6);
        assert(std::count_if(
                   ultimateEffectPlayer.hitEffects().begin(),
                   ultimateEffectPlayer.hitEffects().end(),
                   [](const auto& effect) { return effect.effectId == 24; }) ==
               1);
        const auto* ultimateWheelClip =
            levelOne.player().animationBank.findClip(
                ultimateEffectPlayer.activeAnimation());
        assert(ultimateWheelClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *ultimateWheelClip,
            ultimateEffectPlayer.animationTimeMilliseconds(),
            ultimateEffectPlayer.worldTransform()));
        assert(gameRenderer.updatePlayerHitEffects(levelOne,
                                                   noHitEffectsPlayer));
        gameRenderer.renderFrame();
        RgbaImage ultimateWithoutEffects;
        assert(gameRenderer.readBackImage(ultimateWithoutEffects));
        const usm::Result ultimateEffectsResult =
            gameRenderer.updatePlayerHitEffects(levelOne,
                                                 ultimateEffectPlayer);
        if (!ultimateEffectsResult) {
            std::cerr << ultimateEffectsResult.message() << '\n';
        }
        assert(ultimateEffectsResult);
        gameRenderer.renderFrame();
        RgbaImage ultimateWithEffects;
        assert(gameRenderer.readBackImage(ultimateWithEffects));
        captureIfRequested(ultimateWithEffects,
                           "gameplay-ultimate-wheel-effects.bmp");
        std::size_t ultimateEffectChangedPixels = 0;
        for (std::size_t component = 0;
             component < ultimateWithEffects.pixels.size(); component += 4) {
            ultimateEffectChangedPixels +=
                ultimateWithoutEffects.pixels[component] !=
                    ultimateWithEffects.pixels[component] ||
                ultimateWithoutEffects.pixels[component + 1] !=
                    ultimateWithEffects.pixels[component + 1] ||
                ultimateWithoutEffects.pixels[component + 2] !=
                    ultimateWithEffects.pixels[component + 2];
        }
        assert(ultimateEffectChangedPixels > 5);

        // State 109 replaces the wheel rings with the animated inward web.
        // Verify it independently so a valid ring shader cannot mask a
        // broken effect-animation or bone-follow path.
        ultimateEffectPlayer.update({}, gameplayPose, 1);
        ultimateEffectPlayer.update({}, gameplayPose, 166);
        for (int circle = 0; circle < 5; ++circle) {
            ultimateEffectPlayer.update({}, gameplayPose, 201);
        }
        ultimateEffectPlayer.update({}, gameplayPose, 28);
        assert(ultimateEffectPlayer.activeStateId() == 109);
        ultimateEffectPlayer.update({}, gameplayPose, 100);
        assert(std::any_of(
            ultimateEffectPlayer.hitEffects().begin(),
            ultimateEffectPlayer.hitEffects().end(),
            [](const auto& effect) { return effect.effectId == 25; }));
        const auto* ultimateInClip =
            levelOne.player().animationBank.findClip(
                ultimateEffectPlayer.activeAnimation());
        assert(ultimateInClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *ultimateInClip,
            ultimateEffectPlayer.animationTimeMilliseconds(),
            ultimateEffectPlayer.worldTransform()));
        assert(gameRenderer.updatePlayerHitEffects(levelOne,
                                                   noHitEffectsPlayer));
        gameRenderer.renderFrame();
        RgbaImage ultimateInWithoutEffect;
        assert(gameRenderer.readBackImage(ultimateInWithoutEffect));
        assert(gameRenderer.updatePlayerHitEffects(levelOne,
                                                   ultimateEffectPlayer));
        gameRenderer.renderFrame();
        RgbaImage ultimateInWithEffect;
        assert(gameRenderer.readBackImage(ultimateInWithEffect));
        captureIfRequested(ultimateInWithEffect,
                           "gameplay-ultimate-in-effect.bmp");
        std::size_t ultimateInChangedPixels = 0;
        for (std::size_t component = 0;
             component < ultimateInWithEffect.pixels.size(); component += 4) {
            ultimateInChangedPixels +=
                ultimateInWithoutEffect.pixels[component] !=
                    ultimateInWithEffect.pixels[component] ||
                ultimateInWithoutEffect.pixels[component + 1] !=
                    ultimateInWithEffect.pixels[component + 1] ||
                ultimateInWithoutEffect.pixels[component + 2] !=
                    ultimateInWithEffect.pixels[component + 2];
        }
        assert(ultimateInChangedPixels > 5);

        assert(gameRenderer.updatePlayerHitEffects(levelOne,
                                                   noHitEffectsPlayer));
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, 0, levelOne.player().worldTransform));

        usm::game::TransportFrame coveredTransport;
        coveredTransport.state = usm::game::TransportState::Covered;
        coveredTransport.elapsedMilliseconds = 950.0F;
        assert(gameRenderer.updateTransport(levelOne.hud(),
                                            coveredTransport));
        gameRenderer.renderFrame();
        RgbaImage coveredTransportFrame;
        assert(gameRenderer.readBackImage(coveredTransportFrame));
        captureIfRequested(coveredTransportFrame,
                           "gameplay-transport-covered.bmp");
        for (std::size_t component = 0;
             component < coveredTransportFrame.pixels.size();
             component += 4) {
            assert(coveredTransportFrame.pixels[component] < 4);
            assert(coveredTransportFrame.pixels[component + 1] < 4);
            assert(coveredTransportFrame.pixels[component + 2] < 4);
        }

        usm::game::TransportFrame closingTransport;
        closingTransport.state = usm::game::TransportState::Closing;
        closingTransport.elapsedMilliseconds = 600.0F;
        closingTransport.scale = 8.0F;
        assert(gameRenderer.updateTransport(levelOne.hud(),
                                            closingTransport));
        gameRenderer.renderFrame();
        RgbaImage closingTransportFrame;
        assert(gameRenderer.readBackImage(closingTransportFrame));
        captureIfRequested(closingTransportFrame,
                           "gameplay-transport-closing.bmp");
        std::size_t transportBlackPixels = 0;
        std::size_t transportVisiblePixels = 0;
        for (std::size_t component = 0;
             component < closingTransportFrame.pixels.size();
             component += 4) {
            const bool black =
                closingTransportFrame.pixels[component] < 8 &&
                closingTransportFrame.pixels[component + 1] < 8 &&
                closingTransportFrame.pixels[component + 2] < 8;
            transportBlackPixels += black;
            transportVisiblePixels += !black;
        }
        assert(transportBlackPixels > 100);
        assert(transportVisiblePixels > 100);
        assert(gameRenderer.updateTransport(levelOne.hud(), {}));

        usm::game::GameplayPlayer hurtPlayer;
        assert(hurtPlayer.initialize(levelOne.player(), nullptr,
                                     &playerStates));
        assert(hurtPlayer.applyDamage(30.0F, 0, 1000));
        const auto* effectDamageHurtClip =
            levelOne.player().animationBank.findClip(
                hurtPlayer.activeAnimation());
        assert(effectDamageHurtClip != nullptr);
        hurtPlayer.update({}, gameplayPose, 100);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *effectDamageHurtClip,
            hurtPlayer.animationTimeMilliseconds(),
            hurtPlayer.worldTransform()));
        gameRenderer.renderFrame();
        RgbaImage effectDamageHurtFrame;
        assert(gameRenderer.readBackImage(effectDamageHurtFrame));
        captureIfRequested(effectDamageHurtFrame,
                           "gameplay-player-hurt.bmp");
        std::size_t hurtChangedPixels = 0;
        for (std::size_t component = 0;
             component < gameplayFrame.pixels.size(); component += 4) {
            hurtChangedPixels +=
                gameplayFrame.pixels[component] !=
                    effectDamageHurtFrame.pixels[component] ||
                gameplayFrame.pixels[component + 1] !=
                    effectDamageHurtFrame.pixels[component + 1] ||
                gameplayFrame.pixels[component + 2] !=
                    effectDamageHurtFrame.pixels[component + 2];
        }
        assert(hurtChangedPixels > 50);
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, 0, levelOne.player().worldTransform));

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
        // Let both persistent emitters reach visible color/size stages so this
        // readback covers the authored fire/smoke billboard dimensions.
        environmentEffects.update(0);
        for (std::size_t step = 0; step < 8; ++step) {
            environmentEffects.update(125);
        }
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
        const std::size_t environmentEffectPixelCount =
            environmentEffectFrame.pixels.size() / 4;
        assert(environmentEffectChangedPixels >
               environmentEffectPixelCount / 512);
        assert(environmentEffectChangedPixels <
               environmentEffectPixelCount / 32);

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
        bonusEffects.update(0);
        for (std::size_t step = 0; step < 8; ++step) {
            bonusEffects.update(125);
        }
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
        // Keep the deterministic orb near the collection point so this
        // renderer check remains in view under CGameCamera::ResetCamera's
        // recovered 30.236501-degree FOV (0x002f2c70).
        renderBonusRuntime.update(
            {bonus.position.x, bonus.position.y, bonus.position.z - 100.0F},
            100);
        assert(renderBonusRuntime.orbs().size() == 1);
        assert(bonusEffects.setPersistentEffectVisible(bonus.objectId, false));
        bonusEffects.update(150);
        bonusEffects.update(150);
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
        effects.update(150);
        effects.update(150);
        assert(gameRenderer.updateLevelOneEffects(levelOne.effects(),
                                                   effects, bonusRuntime));
        const auto renderAuthoredEffect =
            [&](std::string_view effectType,
                std::uint32_t elapsedMilliseconds,
                std::string_view captureName) {
                assert(effects.initialize(levelOne.effects().presets));
                playEffect.attributes.front().value = effectType;
                assert(effects.applyCinematicCommand(playEffect));
                effects.update(0);
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
        assert(gameRenderer.updatePlayerHud(
            levelOne.hud(), 0.65F, 0.85F, 1.0F, nullptr, 0, false, nullptr,
            false));
        gameRenderer.renderFrame();
        RgbaImage gameplayHudHiddenFrame;
        assert(gameRenderer.readBackImage(gameplayHudHiddenFrame));
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
                    gameplayHudHiddenFrame.pixels[component] ||
                gameplayHudFrame.pixels[component + 1] !=
                    gameplayHudHiddenFrame.pixels[component + 1] ||
                gameplayHudFrame.pixels[component + 2] !=
                    gameplayHudHiddenFrame.pixels[component + 2];
        }
        assert(hudChangedPixels > 100);
        assert(gameRenderer.updatePlayerHud(
            levelOne.hud(), 0.65F, 0.85F, 1.0F, nullptr, 0, false, nullptr,
            false));
        gameRenderer.renderFrame();
        RgbaImage gameplayHudHiddenAgainFrame;
        assert(gameRenderer.readBackImage(gameplayHudHiddenAgainFrame));
        assert(gameplayHudHiddenAgainFrame.pixels ==
               gameplayHudHiddenFrame.pixels);
        assert(gameRenderer.updatePlayerHud(levelOne.hud(), 0.65F, 0.85F,
                                            1.0F));
        gameRenderer.renderFrame();
        RgbaImage gameplayHudRestoredFrame;
        assert(gameRenderer.readBackImage(gameplayHudRestoredFrame));
        assert(gameplayHudRestoredFrame.pixels == gameplayHudFrame.pixels);
        assert(gameRenderer.updatePlayerHud(
            levelOne.hud(), 0.65F, 0.85F, 1.0F, nullptr, 0, false, nullptr,
            true, true, 0.5F));
        gameRenderer.renderFrame();
        RgbaImage bossProgressHudFrame;
        assert(gameRenderer.readBackImage(bossProgressHudFrame));
        captureIfRequested(bossProgressHudFrame,
                           "gameplay-boss-progress-hud.bmp");
        assert(countChangedPixels(gameplayHudFrame, bossProgressHudFrame) >
               20);
        assert(gameRenderer.updatePlayerHud(levelOne.hud(), 0.65F, 0.85F,
                                            1.0F));
        gameRenderer.renderFrame();
        RgbaImage bossProgressHiddenAgainFrame;
        assert(gameRenderer.readBackImage(bossProgressHiddenAgainFrame));
        assert(bossProgressHiddenAgainFrame.pixels ==
               gameplayHudFrame.pixels);
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
        hintRuntime.setCombatSenseCueVisible(true);
        hintRuntime.update(100, [&levelOne](std::int32_t objectId) {
            assert(objectId == levelOne.player().objectId);
            return levelOne.player().position;
        });
        assert(gameRenderer.updateLevelOneHints(hintRuntime));
        gameRenderer.renderFrame();
        RgbaImage combatSenseHintFrame;
        assert(gameRenderer.readBackImage(combatSenseHintFrame));
        assert(countChangedPixels(hintHiddenFrame, combatSenseHintFrame) >
               10);
        hintRuntime.setCombatSenseCueVisible(false);
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
        showHealthThread.type = 1;
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
        tutorialUi.text = u"Press [X] for a NORMAL ATTACK!";
        tutorialUi.textVisible = true;
        tutorialUi.tutorialPanelVisible = true;
        tutorialUi.tutorialButton = -1;
        tutorialUi.letterboxVisible = true;
        tutorialUi.quickTimeEventVisible = true;
        tutorialUi.quickTimeEventProgress = 0.5F;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), tutorialUi));
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
        std::size_t xboxXGlyphPixels = 0;
        for (std::uint32_t y = tutorialUiFrame.height * 2U / 3U;
             y < tutorialUiFrame.height; ++y) {
            for (std::uint32_t x = tutorialUiFrame.width * 2U / 5U;
                 x < tutorialUiFrame.width * 3U / 5U; ++x) {
                const std::size_t component =
                    (static_cast<std::size_t>(y) * tutorialUiFrame.width +
                     x) * 4U;
                const std::uint8_t red = tutorialUiFrame.pixels[component];
                const std::uint8_t green =
                    tutorialUiFrame.pixels[component + 1U];
                const std::uint8_t blue =
                    tutorialUiFrame.pixels[component + 2U];
                xboxXGlyphPixels += red < 80U && green > 70U &&
                                    blue > 100U;
            }
        }
        assert(xboxXGlyphPixels > 5U);
        tutorialUi.text =
            u"When SPIDER-SENSE appears, press [LB] to counter!";
        tutorialUi.quickTimeEventVisible = false;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), tutorialUi));
        gameRenderer.renderFrame();
        RgbaImage tutorialLbUiFrame;
        assert(gameRenderer.readBackImage(tutorialLbUiFrame));
        captureIfRequested(tutorialLbUiFrame,
                           "gameplay-tutorial-lb-ui.bmp");
        std::size_t tutorialLbChangedPixels = 0;
        for (std::size_t component = 0;
             component < tutorialLbUiFrame.pixels.size(); component += 4) {
            tutorialLbChangedPixels +=
                tutorialLbUiFrame.pixels[component] !=
                    gameplayHudFrame.pixels[component] ||
                tutorialLbUiFrame.pixels[component + 1] !=
                    gameplayHudFrame.pixels[component + 1] ||
                tutorialLbUiFrame.pixels[component + 2] !=
                    gameplayHudFrame.pixels[component + 2];
        }
        assert(tutorialLbChangedPixels > 100);
        usm::game::CinematicUiRuntime collectibleUi;
        collectibleUi.bind(levelOne.textCatalog());
        assert(collectibleUi.showComicCover(7));
        const auto renderInformationNotice = [&](const char* filename) {
            assert(gameRenderer.updateCinematicUi(
                levelOne.hud(), collectibleUi.frame()));
            gameRenderer.renderFrame();
            RgbaImage notice;
            assert(gameRenderer.readBackImage(notice));
            captureIfRequested(notice, filename);
            return notice;
        };
        const RgbaImage firstInformationNotice = renderInformationNotice(
            "gameplay-artwork-first-information-panel.bmp");
        // The original no-portrait panel uses the same shipped frame/font
        // as tutorials. Assert pixel identity for equal text and frame size,
        // not just that some subtitle pixels appeared.
        auto equivalentTutorial = collectibleUi.frame();
        equivalentTutorial.informationPanel =
            usm::game::InformationPanel::None;
        equivalentTutorial.tutorialPanelVisible = true;
        assert(gameRenderer.updateCinematicUi(
            levelOne.hud(), equivalentTutorial));
        gameRenderer.renderFrame();
        RgbaImage equivalentTutorialImage;
        assert(gameRenderer.readBackImage(equivalentTutorialImage));
        assert(firstInformationNotice.pixels ==
               equivalentTutorialImage.pixels);
        assert(collectibleUi.showComicCover(8));
        const RgbaImage repeatInformationNotice = renderInformationNotice(
            "gameplay-artwork-repeat-information-panel.bmp");
        assert(firstInformationNotice.pixels != repeatInformationNotice.pixels);
        equivalentTutorial = collectibleUi.frame();
        equivalentTutorial.informationPanel =
            usm::game::InformationPanel::None;
        equivalentTutorial.tutorialPanelVisible = true;
        assert(gameRenderer.updateCinematicUi(
            levelOne.hud(), equivalentTutorial));
        gameRenderer.renderFrame();
        assert(gameRenderer.readBackImage(equivalentTutorialImage));
        assert(repeatInformationNotice.pixels ==
               equivalentTutorialImage.pixels);
        const std::u16string* copMessage =
            levelOne.textCatalog().findLevelString(
                "STR_PROLOGUE_CINEMATIC_COP_03");
        assert(copMessage != nullptr);
        usm::game::CinematicUiFrame messageUi;
        messageUi.text = *copMessage;
        messageUi.textVisible = true;
        messageUi.messagePanelVisible = true;
        messageUi.messageFace = 4;
        messageUi.messageDurationMilliseconds = 6200;
        messageUi.messageElapsedMilliseconds = 1800;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), messageUi));
        gameRenderer.renderFrame();
        RgbaImage cinematicMessageFrame;
        assert(gameRenderer.readBackImage(cinematicMessageFrame));
        captureIfRequested(cinematicMessageFrame,
                           "gameplay-cinematic-message-page-1.bmp");
        std::size_t messageChangedPixels = 0;
        for (std::size_t component = 0;
             component < cinematicMessageFrame.pixels.size();
             component += 4) {
            messageChangedPixels +=
                cinematicMessageFrame.pixels[component] !=
                    gameplayHudFrame.pixels[component] ||
                cinematicMessageFrame.pixels[component + 1] !=
                    gameplayHudFrame.pixels[component + 1] ||
                cinematicMessageFrame.pixels[component + 2] !=
                    gameplayHudFrame.pixels[component + 2];
        }
        assert(messageChangedPixels > 100);
        messageUi.messageElapsedMilliseconds = 6100;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), messageUi));
        gameRenderer.renderFrame();
        RgbaImage cinematicMessageLastPage;
        assert(gameRenderer.readBackImage(cinematicMessageLastPage));
        captureIfRequested(cinematicMessageLastPage,
                           "gameplay-cinematic-message-last-page.bmp");
        std::size_t pageChangedPixels = 0;
        for (std::size_t component = 0;
             component < cinematicMessageLastPage.pixels.size();
             component += 4) {
            pageChangedPixels +=
                cinematicMessageLastPage.pixels[component] !=
                    cinematicMessageFrame.pixels[component] ||
                cinematicMessageLastPage.pixels[component + 1] !=
                    cinematicMessageFrame.pixels[component + 1] ||
                cinematicMessageLastPage.pixels[component + 2] !=
                    cinematicMessageFrame.pixels[component + 2];
        }
        assert(pageChangedPixels > 100);
        usm::game::CinematicUiFrame senseEffectUi;
        senseEffectUi.interfaceEffectFrame = 13;
        senseEffectUi.interfaceEffectAlpha = 160;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), senseEffectUi));
        gameRenderer.renderFrame();
        RgbaImage senseEffectFrame;
        assert(gameRenderer.readBackImage(senseEffectFrame));
        captureIfRequested(senseEffectFrame,
                           "gameplay-spider-sense-interface-effect.bmp");
        std::size_t senseEffectChangedPixels = 0;
        for (std::size_t component = 0;
             component < senseEffectFrame.pixels.size(); component += 4) {
            senseEffectChangedPixels +=
                senseEffectFrame.pixels[component] !=
                    gameplayHudFrame.pixels[component] ||
                senseEffectFrame.pixels[component + 1] !=
                    gameplayHudFrame.pixels[component + 1] ||
                senseEffectFrame.pixels[component + 2] !=
                    gameplayHudFrame.pixels[component + 2];
        }
        assert(senseEffectChangedPixels > 100);
        usm::game::CinematicUiFrame restoreFadeUi;
        restoreFadeUi.blackOverlayAlpha = 1.0F;
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), restoreFadeUi));
        gameRenderer.renderFrame();
        RgbaImage restoreFadeFrame;
        assert(gameRenderer.readBackImage(restoreFadeFrame));
        captureIfRequested(restoreFadeFrame, "gameplay-restore-black.bmp");
        const std::size_t restoreCenter =
            ((restoreFadeFrame.height / 2U) * restoreFadeFrame.width +
             restoreFadeFrame.width / 2U) *
            4U;
        assert(restoreFadeFrame.pixels[restoreCenter] < 3);
        assert(restoreFadeFrame.pixels[restoreCenter + 1] < 3);
        assert(restoreFadeFrame.pixels[restoreCenter + 2] < 3);
        assert(gameRenderer.updateCinematicUi(levelOne.hud(), {}));
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
        // This renderer fixture exercises run and punch skinning. Keep it
        // collision-independent now that forward motion can legitimately
        // enter the authored wall-climb state in the real level geometry.
        assert(gameplayPlayer.initialize(levelOne.player(), nullptr,
                                         &playerStates));
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
        // The old guessed LINELIST changed only a handful of pixels and let
        // an effectively absent strand pass.  At the normal 256x256 test
        // resolution the native 20 cm textured ribbon covers more than 30
        // pixels (the optional 1280x720 capture path covers over 500).
        const std::size_t minimumWebLinePixels =
            captureWidth >= 1000U ? 500U : 30U;
        assert(webLineChangedPixels > minimumWebLinePixels);
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
        enemies.advanceAnimations(hurtClip->durationMilliseconds() / 2U,
                                  &levelCollision);
        assert(gameRenderer.updateLevelOneEnemies(levelOne, enemies));
        gameRenderer.renderFrame();
        RgbaImage hurtFrame;
        assert(gameRenderer.readBackImage(hurtFrame));
        captureIfRequested(hurtFrame, "gameplay-enemy-hurt.bmp");

        assert(enemies.applyPlayerTargetedHitDetailed(
            394, 35.0F, 109, &enemyHitOrigin));
        assert(enemies.find(394)->hurtStateId == 69);
        for (std::uint32_t elapsed = 0;
             elapsed < 5000 && enemies.find(394)->hurtStateId == 69;
             elapsed += 25) {
            enemies.updateGameplay(25, chaseTarget, &levelCollision);
        }
        assert(enemies.find(394)->hurtStateId == 70);
        assert(enemies.landingAnimatedEffects().size() == 2);
        const usm::Result kickdownEnemyRender =
            gameRenderer.updateLevelOneEnemies(levelOne, enemies);
        if (!kickdownEnemyRender) {
            std::cerr << kickdownEnemyRender.message() << '\n';
        }
        assert(kickdownEnemyRender);
        assert(gameRenderer.updateEnemyElectroEffects(levelOne, enemies));
        gameRenderer.renderFrame();
        RgbaImage kickdownLandingFrame;
        assert(gameRenderer.readBackImage(kickdownLandingFrame));
        captureIfRequested(kickdownLandingFrame,
                           "gameplay-enemy-kickdown-landing.bmp");
        std::size_t kickdownLandingChangedPixels = 0;
        for (std::size_t component = 0;
             component < kickdownLandingFrame.pixels.size();
             component += 4) {
            kickdownLandingChangedPixels +=
                kickdownLandingFrame.pixels[component] !=
                    hurtFrame.pixels[component] ||
                kickdownLandingFrame.pixels[component + 1] !=
                    hurtFrame.pixels[component + 1] ||
                kickdownLandingFrame.pixels[component + 2] !=
                    hurtFrame.pixels[component + 2];
        }
        assert(kickdownLandingChangedPixels > 20);

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

        usm::game::LevelOneBootstrap levelTwo;
        const usm::Result levelTwoLoad = levelTwo.load(dataRoot, 2);
        if (!levelTwoLoad) {
            std::cerr << levelTwoLoad.message() << '\n';
            return 1;
        }
        assert(levelTwo.levelNumber() == 2);
        assert(levelTwo.rooms().size() == 10);
        assert(gameRenderer.uploadLevelOneScene(levelTwo));
        usm::game::LevelObjectRuntime levelTwoObjects;
        assert(levelTwoObjects.initialize(levelTwo));
        assert(gameRenderer.updateLevelOneObjects(levelTwo, levelTwoObjects));
        usm::game::LevelEnemyRuntime levelTwoEnemies;
        assert(levelTwoEnemies.initialize(levelTwo));
        assert(gameRenderer.updateLevelOneEnemies(levelTwo, levelTwoEnemies));
        assert(gameRenderer.updateEnemyMolotovs(levelTwo, {}));
        usm::game::EnemyMolotovState levelTwoMolotov;
        levelTwoMolotov.sourceObjectId = 642;
        levelTwoMolotov.roomId = 1;
        levelTwoMolotov.position = levelTwo.player().position;
        levelTwoMolotov.position.z += 150.0F;
        levelTwoMolotov.facing = {1.0F, 0.0F, 0.0F};
        levelTwoMolotov.damage = 50.0F;
        levelTwoMolotov.phaseElapsedMilliseconds = 400;
        levelTwoMolotov.phase = usm::game::EnemyMolotovPhase::Flying;
        levelTwoMolotov.active = true;
        const std::array<usm::game::EnemyMolotovState, 1>
            levelTwoMolotovs{levelTwoMolotov};
        assert(gameRenderer.updateEnemyMolotovs(levelTwo,
                                                levelTwoMolotovs));
        assert(gameRenderer.updateLevelOneActors(levelTwo, 0));
        const auto* levelTwoIdleClip =
            levelTwo.player().animationBank.findClip("idle_stand");
        assert(levelTwoIdleClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelTwo, *levelTwoIdleClip, 0,
            levelTwo.player().worldTransform));
        usm::game::GameplayCamera levelTwoCamera;
        assert(levelTwoCamera.bind(levelTwo.cameraAreas(),
                                   levelTwo.player().initialCameraAreaId));
        gameRenderer.setCameraAreaRoomVisibility(
            levelTwoCamera.mustInvisibleRooms(),
            levelTwoCamera.mustVisibleRooms());
        std::array<bool, 16> levelTwoCinematicRooms{};
        gameRenderer.setCinematicVisibleRooms(levelTwoCinematicRooms);
        assert(gameRenderer.setCamera(
            levelTwoCamera.sample(levelTwo.player().position)));
        gameRenderer.renderFrame();
        RgbaImage levelTwoOpeningFrame;
        assert(gameRenderer.readBackImage(levelTwoOpeningFrame));
        captureIfRequested(levelTwoOpeningFrame, "level2-opening.bmp");
        std::size_t levelTwoNonBlackPixels = 0;
        std::size_t levelTwoColorfulPixels = 0;
        for (std::size_t component = 0;
             component < levelTwoOpeningFrame.pixels.size();
             component += 4) {
            const auto red = levelTwoOpeningFrame.pixels[component];
            const auto green = levelTwoOpeningFrame.pixels[component + 1];
            const auto blue = levelTwoOpeningFrame.pixels[component + 2];
            levelTwoNonBlackPixels += red > 8 || green > 8 || blue > 8;
            levelTwoColorfulPixels +=
                std::max({red, green, blue}) -
                    std::min({red, green, blue}) >
                12;
        }
        assert(levelTwoNonBlackPixels >
               levelTwoOpeningFrame.pixels.size() / 16);
        assert(levelTwoColorfulPixels > 1000);

        usm::game::LevelOneBootstrap levelThree;
        const usm::Result levelThreeLoad = levelThree.load(dataRoot, 3);
        if (!levelThreeLoad) {
            std::cerr << levelThreeLoad.message() << '\n';
            return 1;
        }
        assert(levelThree.levelNumber() == 3);
        assert(levelThree.rooms().size() == 10);
        assert(gameRenderer.uploadLevelOneScene(levelThree));
        usm::game::LevelObjectRuntime levelThreeObjects;
        assert(levelThreeObjects.initialize(levelThree));
        assert(gameRenderer.updateLevelOneObjects(levelThree,
                                                  levelThreeObjects));
        usm::game::LevelEnemyRuntime levelThreeEnemies;
        assert(levelThreeEnemies.initialize(levelThree));
        assert(gameRenderer.updateLevelOneEnemies(levelThree,
                                                  levelThreeEnemies));
        assert(gameRenderer.updateEnemyElectroEffects(levelThree,
                                                       levelThreeEnemies));
        assert(gameRenderer.updateLevelOneActors(levelThree, 0));
        const auto* levelThreeIdleClip =
            levelThree.player().animationBank.findClip("idle_stand");
        assert(levelThreeIdleClip != nullptr);
        assert(gameRenderer.updateLevelOnePlayer(
            levelThree, *levelThreeIdleClip, 0,
            levelThree.player().worldTransform));
        usm::game::GameplayCamera levelThreeCamera;
        assert(levelThreeCamera.bind(levelThree.cameraAreas(),
                                     levelThree.player().initialCameraAreaId));
        gameRenderer.setCameraAreaRoomVisibility(
            levelThreeCamera.mustInvisibleRooms(),
            levelThreeCamera.mustVisibleRooms());
        std::array<bool, 16> levelThreeCinematicRooms{};
        gameRenderer.setCinematicVisibleRooms(levelThreeCinematicRooms);
        assert(gameRenderer.setCamera(
            levelThreeCamera.sample(levelThree.player().position)));
        gameRenderer.renderFrame();
        RgbaImage levelThreeOpeningFrame;
        assert(gameRenderer.readBackImage(levelThreeOpeningFrame));
        captureIfRequested(levelThreeOpeningFrame, "level3-opening.bmp");
        std::size_t levelThreeNonBlackPixels = 0;
        std::size_t levelThreeColorfulPixels = 0;
        for (std::size_t component = 0;
             component < levelThreeOpeningFrame.pixels.size();
             component += 4) {
            const auto red = levelThreeOpeningFrame.pixels[component];
            const auto green = levelThreeOpeningFrame.pixels[component + 1];
            const auto blue = levelThreeOpeningFrame.pixels[component + 2];
            levelThreeNonBlackPixels += red > 8 || green > 8 || blue > 8;
            levelThreeColorfulPixels +=
                std::max({red, green, blue}) -
                    std::min({red, green, blue}) >
                12;
        }
        assert(levelThreeNonBlackPixels >
               levelThreeOpeningFrame.pixels.size() / 16);
        assert(levelThreeColorfulPixels > 1000);

        // Native CSummonObject/CElectricPost rendering uses the exact shipped
        // Electro meshes and material type 0x0d. Render them through WARP and
        // require additive brightening so opaque black lightning planes cannot
        // return unnoticed.
        assert(levelThreeEnemies.setDiagnosticAiEnabled(31094, true));
        const auto* renderedElectro = levelThreeEnemies.find(31094);
        assert(renderedElectro != nullptr);
        usm::assets::Vector3 electroVictim = renderedElectro->position;
        levelThreeEnemies.updateGameplay(1, electroVictim, nullptr);
        levelThreeEnemies.updateGameplay(416, electroVictim, nullptr);
        levelThreeEnemies.updateGameplay(1, electroVictim, nullptr);
        assert(levelThreeEnemies.thunderclaps().size() == 3);
        const auto thunderclapPosition =
            levelThreeEnemies.thunderclaps().front().position;
        gameRenderer.setCameraAreaRoomVisibility({}, {});
        const usm::game::CameraPose thunderclapCamera{
            {thunderclapPosition.x, thunderclapPosition.y - 900.0F,
             thunderclapPosition.z + 350.0F},
            {thunderclapPosition.x, thunderclapPosition.y,
             thunderclapPosition.z + 300.0F},
            {0.0F, 0.0F, 1.0F}, 60.0F, 1.0F, 5000.0F};
        assert(gameRenderer.setCamera(thunderclapCamera));
        usm::game::LevelEnemyRuntime emptyLevelThreeEnemies;
        assert(emptyLevelThreeEnemies.initialize(levelThree));
        assert(gameRenderer.updateEnemyElectroEffects(
            levelThree, emptyLevelThreeEnemies));
        gameRenderer.renderFrame();
        RgbaImage thunderclapHiddenFrame;
        assert(gameRenderer.readBackImage(thunderclapHiddenFrame));
        assert(gameRenderer.updateEnemyElectroEffects(levelThree,
                                                       levelThreeEnemies));
        gameRenderer.renderFrame();
        RgbaImage thunderclapVisibleFrame;
        assert(gameRenderer.readBackImage(thunderclapVisibleFrame));
        captureIfRequested(thunderclapVisibleFrame,
                           "level3-thunderclap-additive.bmp");
        std::size_t thunderclapBrightenedPixels = 0;
        std::size_t thunderclapDarkenedPixels = 0;
        for (std::size_t component = 0;
             component < thunderclapVisibleFrame.pixels.size();
             component += 4) {
            const int hidden =
                thunderclapHiddenFrame.pixels[component] +
                thunderclapHiddenFrame.pixels[component + 1] +
                thunderclapHiddenFrame.pixels[component + 2];
            const int visible =
                thunderclapVisibleFrame.pixels[component] +
                thunderclapVisibleFrame.pixels[component + 1] +
                thunderclapVisibleFrame.pixels[component + 2];
            thunderclapBrightenedPixels += visible > hidden + 6;
            thunderclapDarkenedPixels += visible + 6 < hidden;
        }
        assert(thunderclapBrightenedPixels > 20);
        assert(thunderclapDarkenedPixels == 0);

        bool electroBurstRendered = false;
        std::size_t electroBurstBrightenedPixels = 0;
        std::size_t electroBurstDarkenedPixels = 0;
        for (std::uint32_t elapsed = 0;
             elapsed < 30000 &&
             levelThreeEnemies.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::Rotate;
             elapsed += 25) {
            levelThreeEnemies.updateGameplay(25, electroVictim, nullptr);
            if (!electroBurstRendered &&
                !levelThreeEnemies.electroBursts().empty()) {
                const auto burstPosition =
                    levelThreeEnemies.electroBursts().front().position;
                const usm::game::CameraPose burstCamera{
                    {burstPosition.x, burstPosition.y - 1200.0F,
                     burstPosition.z + 300.0F},
                    burstPosition, {0.0F, 0.0F, 1.0F}, 60.0F, 1.0F,
                    6000.0F};
                assert(gameRenderer.setCamera(burstCamera));
                assert(gameRenderer.updateEnemyElectroEffects(
                    levelThree, emptyLevelThreeEnemies));
                gameRenderer.renderFrame();
                RgbaImage burstHiddenFrame;
                assert(gameRenderer.readBackImage(burstHiddenFrame));
                assert(gameRenderer.updateEnemyElectroEffects(
                    levelThree, levelThreeEnemies));
                gameRenderer.renderFrame();
                RgbaImage burstVisibleFrame;
                assert(gameRenderer.readBackImage(burstVisibleFrame));
                captureIfRequested(burstVisibleFrame,
                                   "level3-electro-weak-burst-additive.bmp");
                for (std::size_t component = 0;
                     component < burstVisibleFrame.pixels.size();
                     component += 4) {
                    const int hidden =
                        burstHiddenFrame.pixels[component] +
                        burstHiddenFrame.pixels[component + 1] +
                        burstHiddenFrame.pixels[component + 2];
                    const int visible =
                        burstVisibleFrame.pixels[component] +
                        burstVisibleFrame.pixels[component + 1] +
                        burstVisibleFrame.pixels[component + 2];
                    electroBurstBrightenedPixels += visible > hidden + 6;
                    electroBurstDarkenedPixels += visible + 6 < hidden;
                }
                electroBurstRendered = true;
            }
            (void)levelThreeEnemies.consumePlayerHits();
            (void)levelThreeEnemies.consumeProjectileEvents();
            (void)levelThreeEnemies.consumeEffectCues();
        }
        assert(electroBurstRendered);
        assert(electroBurstBrightenedPixels > 20);
        assert(electroBurstDarkenedPixels == 0);
        assert(levelThreeEnemies.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::Rotate);
        assert(levelThreeEnemies.electricPosts().size() == 3);
        const auto postCenter = levelThreeEnemies.electricPosts()[0].position;
        const usm::game::CameraPose electricPostCamera{
            {postCenter.x, postCenter.y - 1200.0F, postCenter.z + 500.0F},
            {postCenter.x, postCenter.y, postCenter.z + 200.0F},
            {0.0F, 0.0F, 1.0F}, 60.0F, 1.0F, 6000.0F};
        assert(gameRenderer.setCamera(electricPostCamera));
        assert(gameRenderer.updateEnemyElectroEffects(
            levelThree, emptyLevelThreeEnemies));
        gameRenderer.renderFrame();
        RgbaImage electricPostsHiddenFrame;
        assert(gameRenderer.readBackImage(electricPostsHiddenFrame));
        assert(gameRenderer.updateEnemyElectroEffects(levelThree,
                                                       levelThreeEnemies));
        gameRenderer.renderFrame();
        RgbaImage electricPostsVisibleFrame;
        assert(gameRenderer.readBackImage(electricPostsVisibleFrame));
        captureIfRequested(electricPostsVisibleFrame,
                           "level3-electric-posts-additive.bmp");
        std::size_t electricPostBrightenedPixels = 0;
        std::size_t electricPostDarkenedPixels = 0;
        for (std::size_t component = 0;
             component < electricPostsVisibleFrame.pixels.size();
             component += 4) {
            const int hidden =
                electricPostsHiddenFrame.pixels[component] +
                electricPostsHiddenFrame.pixels[component + 1] +
                electricPostsHiddenFrame.pixels[component + 2];
            const int visible =
                electricPostsVisibleFrame.pixels[component] +
                electricPostsVisibleFrame.pixels[component + 1] +
                electricPostsVisibleFrame.pixels[component + 2];
            electricPostBrightenedPixels += visible > hidden + 6;
            electricPostDarkenedPixels += visible + 6 < hidden;
        }
        assert(electricPostBrightenedPixels > 20);
        assert(electricPostDarkenedPixels == 0);

        // CCinematicThread::ActiveRoom (0x00371d54) opens the CRoom+0x90
        // movement gate; CRoom::Move (0x0036d8c0) changes the complete room
        // scene-node transform. Exercise the shipped Level 7 Room22 mesh
        // through WARP before and after that exact command path.
        usm::game::LevelOneBootstrap levelSeven;
        const usm::Result levelSevenLoad = levelSeven.load(dataRoot, 7);
        if (!levelSevenLoad) {
            std::cerr << levelSevenLoad.message() << '\n';
            return 1;
        }
        assert(gameRenderer.uploadLevelOneScene(levelSeven));

        // CLevel::LoadNextObject's Train branch must materialize the shipped
        // train.bdae scene node before its commands can affect a real object.
        // Compare the same WARP frame with Level 7 Train3 visible/hidden so
        // this cannot regress to a telemetry-only train state.
        usm::game::LevelObjectRuntime levelSevenObjects;
        assert(levelSevenObjects.initialize(levelSeven));
        const auto* renderedTrain = levelSevenObjects.find(60564);
        assert(renderedTrain != nullptr && renderedTrain->asset != nullptr);
        assert(renderedTrain->asset->kind ==
               usm::game::LevelObjectKind::Train);
        std::array<bool, 16> levelSevenVisibleRooms{};
        levelSevenVisibleRooms[6] = true;
        gameRenderer.setCinematicVisibleRooms(levelSevenVisibleRooms);
        const usm::game::CameraPose trainCameraPose{
            {renderedTrain->position.x + 2200.0F,
             renderedTrain->position.y,
             renderedTrain->position.z + 700.0F},
            {renderedTrain->position.x, renderedTrain->position.y,
             renderedTrain->position.z + 100.0F},
            {0.0F, 0.0F, 1.0F}, 60.0F, 10.0F, 6000.0F};
        assert(gameRenderer.updateLevelOneObjects(levelSeven,
                                                  levelSevenObjects));
        assert(gameRenderer.setCamera(trainCameraPose));
        gameRenderer.renderFrame();
        RgbaImage trainVisibleFrame;
        assert(gameRenderer.readBackImage(trainVisibleFrame));

        const auto trainBossRush = std::find_if(
            levelSeven.cinematics().begin(), levelSeven.cinematics().end(),
            [](const auto& cinematic) {
                return cinematic.objectId == 61175;
            });
        assert(trainBossRush != levelSeven.cinematics().end());
        const auto movingTrainThread = std::find_if(
            trainBossRush->script.threads().begin(),
            trainBossRush->script.threads().end(), [](const auto& thread) {
                return thread.objectId == 60564;
            });
        assert(movingTrainThread != trainBossRush->script.threads().end());
        usm::game::LevelObjectRuntime movingTrainObjects;
        assert(movingTrainObjects.initialize(levelSeven));
        for (const std::string_view commandName :
             {std::string_view{"CutTrain"},
              std::string_view{"FollowWayPoint"},
              std::string_view{"EnableAI"}}) {
            const auto command = std::find_if(
                movingTrainThread->commands.begin(),
                movingTrainThread->commands.end(),
                [commandName](const auto& candidate) {
                    return candidate.name == commandName;
                });
            assert(command != movingTrainThread->commands.end());
            assert(movingTrainObjects.applyCinematicCommand(
                levelSeven, *movingTrainThread, *command));
        }
        movingTrainObjects.advanceAnimations(500);
        const auto* advancedTrain = movingTrainObjects.find(60564);
        assert(advancedTrain != nullptr);
        assert(std::hypot(advancedTrain->position.x -
                              renderedTrain->position.x,
                          advancedTrain->position.y -
                              renderedTrain->position.y,
                          advancedTrain->position.z -
                              renderedTrain->position.z) > 490.0F);
        assert(gameRenderer.updateLevelOneObjects(levelSeven,
                                                  movingTrainObjects));
        gameRenderer.renderFrame();
        RgbaImage trainAdvancedFrame;
        assert(gameRenderer.readBackImage(trainAdvancedFrame));
        std::size_t trainMotionChangedPixels = 0;
        for (std::size_t component = 0;
             component < trainVisibleFrame.pixels.size(); component += 4) {
            trainMotionChangedPixels +=
                trainVisibleFrame.pixels[component] !=
                    trainAdvancedFrame.pixels[component] ||
                trainVisibleFrame.pixels[component + 1] !=
                    trainAdvancedFrame.pixels[component + 1] ||
                trainVisibleFrame.pixels[component + 2] !=
                    trainAdvancedFrame.pixels[component + 2];
        }
        assert(trainMotionChangedPixels > 100);

        usm::game::CinematicThread trainThread;
        trainThread.objectId = 60564;
        usm::game::CinematicCommand hideTrain;
        hideTrain.name = "SetVisible";
        hideTrain.attributes.push_back({"bool", "Visible", "false"});
        assert(levelSevenObjects.applyCinematicCommand(
            levelSeven, trainThread, hideTrain));
        assert(gameRenderer.updateLevelOneObjects(levelSeven,
                                                  levelSevenObjects));
        gameRenderer.renderFrame();
        RgbaImage trainHiddenFrame;
        assert(gameRenderer.readBackImage(trainHiddenFrame));
        std::size_t trainChangedPixels = 0;
        for (std::size_t component = 0;
             component < trainVisibleFrame.pixels.size(); component += 4) {
            trainChangedPixels +=
                trainVisibleFrame.pixels[component] !=
                    trainHiddenFrame.pixels[component] ||
                trainVisibleFrame.pixels[component + 1] !=
                    trainHiddenFrame.pixels[component + 1] ||
                trainVisibleFrame.pixels[component + 2] !=
                    trainHiddenFrame.pixels[component + 2];
        }
        assert(trainChangedPixels > 100);

        const auto movingRoom = std::find_if(
            levelSeven.rooms().begin(), levelSeven.rooms().end(),
            [](const auto& room) { return room.objectId == 61204; });
        assert(movingRoom != levelSeven.rooms().end());
        usm::assets::Vector3 movingRoomMinimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        usm::assets::Vector3 movingRoomMaximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        for (const auto& geometry :
             movingRoom->geometry.sceneGeometries()) {
            movingRoomMinimum.x =
                std::min(movingRoomMinimum.x, geometry.bounds.minimum.x);
            movingRoomMinimum.y =
                std::min(movingRoomMinimum.y, geometry.bounds.minimum.y);
            movingRoomMinimum.z =
                std::min(movingRoomMinimum.z, geometry.bounds.minimum.z);
            movingRoomMaximum.x =
                std::max(movingRoomMaximum.x, geometry.bounds.maximum.x);
            movingRoomMaximum.y =
                std::max(movingRoomMaximum.y, geometry.bounds.maximum.y);
            movingRoomMaximum.z =
                std::max(movingRoomMaximum.z, geometry.bounds.maximum.z);
        }
        const usm::assets::Vector3 movingRoomCenter{
            (movingRoomMinimum.x + movingRoomMaximum.x) * 0.5F,
            (movingRoomMinimum.y + movingRoomMaximum.y) * 0.5F,
            (movingRoomMinimum.z + movingRoomMaximum.z) * 0.5F};
        const float movingRoomExtent = std::max(
            {movingRoomMaximum.x - movingRoomMinimum.x,
             movingRoomMaximum.y - movingRoomMinimum.y,
             movingRoomMaximum.z - movingRoomMinimum.z});
        assert(std::isfinite(movingRoomExtent) && movingRoomExtent > 0.0F);
        const float movingRoomCameraDistance =
            std::max(1000.0F, movingRoomExtent * 0.8F);
        const usm::game::CameraPose movingRoomCameraPose{
            {movingRoomCenter.x,
             movingRoomCenter.y - movingRoomCameraDistance,
             movingRoomCenter.z + movingRoomExtent * 0.15F},
            movingRoomCenter,
            {0.0F, 0.0F, 1.0F}, 60.0F,
            std::max(1.0F, movingRoomExtent * 0.001F),
            movingRoomCameraDistance * 8.0F};
        usm::game::LevelTriggerRuntime levelSevenTriggers;
        levelSevenTriggers.bind(levelSeven.triggers());
        usm::game::GameplayCamera levelSevenCamera;
        assert(levelSevenCamera.bind(
            levelSeven.cameraAreas(),
            levelSeven.player().initialCameraAreaId));
        usm::game::LevelCinematicRuntime levelSevenCinematics;
        levelSevenCinematics.bind(levelSevenTriggers, levelSevenCamera,
                                  levelSeven.waypoints(),
                                  levelSeven.rooms());
        assert(gameRenderer.updateLevelRooms(levelSeven,
                                             levelSevenCinematics));
        assert(gameRenderer.setCamera(movingRoomCameraPose));
        gameRenderer.renderFrame();
        RgbaImage movingRoomInitialFrame;
        assert(gameRenderer.readBackImage(movingRoomInitialFrame));

        const auto bossRush = std::find_if(
            levelSeven.cinematics().begin(), levelSeven.cinematics().end(),
            [](const auto& cinematic) {
                return cinematic.objectId == 61175;
            });
        assert(bossRush != levelSeven.cinematics().end());
        const usm::game::CinematicThread* activeRoomThread = nullptr;
        const usm::game::CinematicCommand* activeRoomCommand = nullptr;
        for (const auto& thread : bossRush->script.threads()) {
            for (const auto& command : thread.commands) {
                const auto* geometry =
                    command.findAttribute("^ID^Geometry");
                if (command.name == "ActiveRoom" && geometry != nullptr &&
                    geometry->value == "61204") {
                    activeRoomThread = &thread;
                    activeRoomCommand = &command;
                    break;
                }
            }
            if (activeRoomCommand != nullptr) {
                break;
            }
        }
        assert(activeRoomThread != nullptr && activeRoomCommand != nullptr);
        assert(levelSevenCinematics.applyCommand(*activeRoomThread,
                                                 *activeRoomCommand));
        levelSevenCinematics.advanceRoomMotion(1000);
        assert(std::abs(
                   levelSevenCinematics.findRoomMotion(61204)->position.y) >
               1000.0F);
        assert(gameRenderer.updateLevelRooms(levelSeven,
                                             levelSevenCinematics));
        assert(gameRenderer.setCamera(movingRoomCameraPose));
        gameRenderer.renderFrame();
        RgbaImage movingRoomAdvancedFrame;
        assert(gameRenderer.readBackImage(movingRoomAdvancedFrame));
        std::size_t movingRoomChangedPixels = 0;
        for (std::size_t component = 0;
             component < movingRoomAdvancedFrame.pixels.size();
             component += 4) {
            movingRoomChangedPixels +=
                movingRoomAdvancedFrame.pixels[component] !=
                    movingRoomInitialFrame.pixels[component] ||
                movingRoomAdvancedFrame.pixels[component + 1] !=
                    movingRoomInitialFrame.pixels[component + 1] ||
                movingRoomAdvancedFrame.pixels[component + 2] !=
                    movingRoomInitialFrame.pixels[component + 2];
        }
        assert(movingRoomChangedPixels > 100);

        // Both CBoomerang constructors (0x0035b3fc/0x0035b604) load
        // phantom_unit_weapons.bdae. At 0x0035b568/0x0035b728, r1 is the
        // native string `weapons` at 0x004e538a, r2=true and r3=0. Exercise
        // that exact packaged mesh/clip through WARP so the recovered
        // projectile cannot regress into a logic-only placeholder.
        usm::game::LevelOneBootstrap levelEight;
        const usm::Result levelEightLoad = levelEight.load(dataRoot, 8);
        if (!levelEightLoad) {
            std::cerr << levelEightLoad.message() << '\n';
            return 1;
        }
        assert(levelEight.levelNumber() == 8);
        assert(gameRenderer.uploadLevelOneScene(levelEight));
        usm::game::LevelEnemyRuntime levelEightEnemies;
        assert(levelEightEnemies.initialize(levelEight));
        assert(gameRenderer.updateLevelOneEnemies(levelEight,
                                                  levelEightEnemies));
        assert(gameRenderer.updateLevelOneActors(levelEight, 0));
        assert(gameRenderer.updateEnemyBoomerangs(levelEight, {}));

        const auto& boomerangAsset = levelEight.boomerangProjectile();
        const auto* boomerangAnimation =
            boomerangAsset.animationBank.findClip("weapons");
        assert(boomerangAnimation != nullptr);
        std::vector<usm::assets::ColladaGeometry> boomerangGeometry;
        assert(usm::assets::evaluateColladaPose(
            boomerangAsset.mesh, boomerangAsset.animationBank,
            boomerangAnimation->startMilliseconds, boomerangGeometry));
        usm::assets::Vector3 boomerangMinimum{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
        usm::assets::Vector3 boomerangMaximum{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest()};
        for (const auto& geometry : boomerangGeometry) {
            for (const auto& vertex : geometry.vertices) {
                boomerangMinimum.x =
                    std::min(boomerangMinimum.x, vertex.position.x);
                boomerangMinimum.y =
                    std::min(boomerangMinimum.y, vertex.position.y);
                boomerangMinimum.z =
                    std::min(boomerangMinimum.z, vertex.position.z);
                boomerangMaximum.x =
                    std::max(boomerangMaximum.x, vertex.position.x);
                boomerangMaximum.y =
                    std::max(boomerangMaximum.y, vertex.position.y);
                boomerangMaximum.z =
                    std::max(boomerangMaximum.z, vertex.position.z);
            }
        }
        const usm::assets::Vector3 boomerangCenter{
            (boomerangMinimum.x + boomerangMaximum.x) * 0.5F,
            (boomerangMinimum.y + boomerangMaximum.y) * 0.5F,
            (boomerangMinimum.z + boomerangMaximum.z) * 0.5F};
        const float boomerangExtent = std::max(
            {boomerangMaximum.x - boomerangMinimum.x,
             boomerangMaximum.y - boomerangMinimum.y,
             boomerangMaximum.z - boomerangMinimum.z});
        assert(std::isfinite(boomerangExtent) && boomerangExtent > 0.0F);

        std::array<bool, 16> levelEightCinematicRooms{};
        gameRenderer.setCinematicVisibleRooms(levelEightCinematicRooms);
        gameRenderer.setCameraAreaRoomVisibility({}, {});
        const float boomerangCameraDistance =
            boomerangExtent * 1.5F;
        const float boomerangNearPlane =
            std::max(boomerangExtent * 0.01F, 0.001F);
        const usm::game::CameraPose boomerangCamera{
            {0.0F, -boomerangCameraDistance, 0.0F},
            {0.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, 1.0F}, 60.0F, boomerangNearPlane,
            std::max(boomerangCameraDistance * 5.0F,
                     boomerangNearPlane + 1.0F)};
        assert(gameRenderer.setCamera(boomerangCamera));
        gameRenderer.renderFrame();
        RgbaImage boomerangHiddenFrame;
        assert(gameRenderer.readBackImage(boomerangHiddenFrame));

        usm::game::EnemyBoomerangState renderedBoomerang;
        renderedBoomerang.sourceObjectId = 40524;
        renderedBoomerang.roomId = 7;
        // Facing -Y yields the identity local basis in the portable renderer;
        // offset the authored mesh center to put the exact asset on camera.
        renderedBoomerang.position = {-boomerangCenter.x,
                                      -boomerangCenter.y,
                                      -boomerangCenter.z};
        renderedBoomerang.facing = {0.0F, -1.0F, 0.0F};
        renderedBoomerang.damage = 40.0F;
        renderedBoomerang.phaseElapsedMilliseconds = 100;
        renderedBoomerang.phase =
            usm::game::EnemyBoomerangPhase::Outbound;
        renderedBoomerang.active = true;
        assert(gameRenderer.updateEnemyBoomerangs(
            levelEight, {&renderedBoomerang, 1}));
        gameRenderer.renderFrame();
        RgbaImage boomerangVisibleFrame;
        assert(gameRenderer.readBackImage(boomerangVisibleFrame));
        captureIfRequested(boomerangVisibleFrame,
                           "level8-robot-phantom-boomerang.bmp");
        std::size_t boomerangChangedPixels = 0;
        for (std::size_t component = 0;
             component < boomerangVisibleFrame.pixels.size();
             component += 4) {
            boomerangChangedPixels +=
                boomerangVisibleFrame.pixels[component] !=
                    boomerangHiddenFrame.pixels[component] ||
                boomerangVisibleFrame.pixels[component + 1] !=
                    boomerangHiddenFrame.pixels[component + 1] ||
                boomerangVisibleFrame.pixels[component + 2] !=
                    boomerangHiddenFrame.pixels[component + 2];
        }
        assert(boomerangChangedPixels > 20);
    }
    return 0;
}
