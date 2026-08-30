#include "assets/BresFile.hpp"
#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/ColladaSkinning.hpp"
#include "assets/DdsAtcTexture.hpp"
#include "assets/IrrScene.hpp"
#include "assets/SpriteAtlas.hpp"
#include "audio/OggAudio.hpp"
#include "audio/EnemyBehaviorSoundBank.hpp"
#include "audio/PlayerStateSoundBank.hpp"
#include "audio/CinematicSoundBank.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "audio/VoxSoundTable.hpp"
#include "core/Result.hpp"
#include "filesystem/GbmpArchive.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/LevelSlideRuntime.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/EnemyRangeAttackConfig.hpp"
#include "game/PlayerHudHealthState.hpp"
#include "game/PlayerStateConfig.hpp"
#include "game/QuickTimeEventRuntime.hpp"
#include "game/LevelTriggerRuntime.hpp"
#include "game/WebGrabPointRuntime.hpp"
#include "game/WebSwingRuntime.hpp"
#include "game/CinematicScript.hpp"
#include "game/CinematicPlayer.hpp"
#include "game/CinematicUiRuntime.hpp"
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

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    const auto success = usm::Result::success();
    assert(static_cast<bool>(success));

    const auto failure = usm::Result::failure("expected failure");
    assert(!static_cast<bool>(failure));
    assert(failure.message() == "expected failure");

    usm::game::PlayerHudHealthState hudHealth;
    hudHealth.initialize(1000.0F, 1000.0F);
    assert(hudHealth.currentRatio() == 1.0F);
    assert(hudHealth.delayedRatio() == 1.0F);
    hudHealth.update(750.0F, 25);
    assert(std::abs(hudHealth.currentRatio() - 0.75F) < 0.001F);
    assert(hudHealth.delayedRatio() == 1.0F);
    hudHealth.update(750.0F, 25);
    assert(hudHealth.delayedRatio() == 1.0F);
    hudHealth.update(750.0F, 250);
    assert(hudHealth.delayedRatio() < 1.0F);
    assert(hudHealth.delayedRatio() > hudHealth.currentRatio());
    hudHealth.update(750.0F, 250);
    assert(std::abs(hudHealth.delayedRatio() - 0.75F) < 0.001F);
    hudHealth.update(900.0F, 16);
    assert(std::abs(hudHealth.currentRatio() - 0.9F) < 0.001F);
    assert(std::abs(hudHealth.delayedRatio() - 0.9F) < 0.001F);

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
    assert(collisionFixtureWorld.segmentBlocked({400.0F, 500.0F, 25.0F},
                                                {600.0F, 500.0F, 100.0F}));
    assert(!collisionFixtureWorld.segmentBlocked({100.0F, 500.0F, 25.0F},
                                                 {300.0F, 500.0F, 100.0F}));

    std::array<usm::game::LevelWebGrabPointAsset, 3> selectionPoints{};
    selectionPoints[0].objectId = 1;
    selectionPoints[0].position = {100.0F, 0.0F, 0.0F};
    selectionPoints[0].visibleLength = 90.0F;
    selectionPoints[1].objectId = 2;
    selectionPoints[1].position = {200.0F, 0.0F, 0.0F};
    selectionPoints[1].visibleLength = -1.0F;
    selectionPoints[2].objectId = 3;
    selectionPoints[2].position = {-50.0F, 0.0F, 0.0F};
    selectionPoints[2].visibleLength = -1.0F;
    usm::game::WebGrabPointRuntime webGrabSelection;
    webGrabSelection.bind(selectionPoints);
    const usm::assets::Vector3 selectionOrigin{};
    const usm::assets::Vector3 selectionFacing{1.0F, 0.0F, 0.0F};
    assert(webGrabSelection.findBest(selectionOrigin, selectionFacing)->
               objectId == 1);
    assert(webGrabSelection.findBest(selectionOrigin, selectionFacing, 1)->
               objectId == 2);
    assert(webGrabSelection.findClosestVisible(selectionOrigin)->objectId ==
           3);
    // SearchWebGrabPoint applies VisiableLength only after choosing the best
    // facing candidate, so it does not fall through to point 2 here.
    assert(webGrabSelection.search(selectionOrigin, selectionFacing) ==
           nullptr);
    selectionPoints[0].visibleLength = 100.0F;
    assert(webGrabSelection.search(selectionOrigin, selectionFacing)->
               objectId == 1);

    std::array<usm::game::LevelWebGrabPointAsset, 1> occludedPoint{};
    occludedPoint[0].objectId = 4;
    occludedPoint[0].position = {600.0F, 500.0F, 100.0F};
    occludedPoint[0].visibleLength = -1.0F;
    webGrabSelection.bind(occludedPoint, &collisionFixtureWorld);
    assert(webGrabSelection.search({400.0F, 500.0F, 0.0F}, selectionFacing) ==
           nullptr);

    usm::game::LevelWebGrabPointAsset swingPoint;
    swingPoint.objectId = 377;
    swingPoint.position = {0.0F, 0.0F, 600.0F};
    swingPoint.direction = {1.0F, 0.0F, 0.0F};
    swingPoint.length = 600.0F;
    swingPoint.exitSpeed = 0.45F;
    usm::game::WebSwingRuntime webSwing;
    assert(webSwing.start(swingPoint, {-300.0F, 0.0F, 80.0F},
                          {500.0F, 0.0F, 0.0F}));
    assert(webSwing.active());
    const auto swingStartPosition = webSwing.position();
    const auto ropeLength = [&swingPoint](const usm::assets::Vector3& value) {
        const float x = value.x - swingPoint.position.x;
        const float y = value.y - swingPoint.position.y;
        const float z = value.z - swingPoint.position.z;
        return std::sqrt(x * x + y * y + z * z);
    };
    assert(std::abs(ropeLength(swingStartPosition) - 600.0F) < 0.01F);
    webSwing.update(100);
    assert(std::abs(ropeLength(webSwing.position()) - 600.0F) < 0.01F);
    assert(std::abs(webSwing.position().y) < 0.001F);
    assert(std::abs(webSwing.position().x - swingStartPosition.x) > 0.01F);
    const auto swingRelease = webSwing.release();
    assert(!webSwing.active());
    assert(!swingRelease.hasTargetWaypoint);
    const float horizontalReleaseSpeed = std::sqrt(
        swingRelease.velocityCentimetersPerSecond.x *
            swingRelease.velocityCentimetersPerSecond.x +
        swingRelease.velocityCentimetersPerSecond.y *
            swingRelease.velocityCentimetersPerSecond.y);
    assert(horizontalReleaseSpeed <= 450.0F);
    assert(std::abs(swingRelease.velocityCentimetersPerSecond.z) <= 900.0F);

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

        usm::audio::VoxSoundTable voxSounds;
        const usm::Result voxResult = voxSounds.load(dataRoot);
        if (!voxResult) {
            std::cerr << voxResult.message() << '\n';
            return 1;
        }
        assert(voxSounds.records().size() == 493);
        const auto* knifeHurt = voxSounds.find("SFX_THUG_KNIFE_HURT_1");
        assert(knifeHurt != nullptr);
        assert(knifeHurt->resourcePath ==
               "sfx/NPC/Thugs/sfx_thug_hurt_1.wav");
        assert(voxSounds.find("SFX_VERTICAL_IMPACT") != nullptr);

        usm::game::PlayerStateConfigDatabase playerStateConfigs;
        const usm::Result playerStateConfigResult =
            playerStateConfigs.load(dataRoot);
        if (!playerStateConfigResult) {
            std::cerr << playerStateConfigResult.message() << '\n';
            return 1;
        }
        assert(playerStateConfigs.states().size() == 131);
        assert(playerStateConfigs.soundConfigs().size() == 38);
        const auto& jumpReadyState = playerStateConfigs.states()[12];
        const auto& jumpStartState = playerStateConfigs.states()[13];
        const auto& jumpFallState = playerStateConfigs.states()[14];
        const auto& jumpLandState = playerStateConfigs.states()[16];
        assert(jumpReadyState.name == "k_state_jump_ready");
        assert(jumpReadyState.stateClass == 1);
        assert(jumpReadyState.motionType == 17);
        assert(jumpReadyState.primaryAnimationId == 71);
        assert(jumpReadyState.nextStateId == 13);
        assert(jumpStartState.name == "k_state_jump_start");
        assert(jumpStartState.motionType == 18);
        assert(jumpStartState.primaryAnimationId == 95);
        assert(jumpStartState.nextStateId == 14);
        assert(jumpFallState.name == "k_state_jump_fall");
        assert(jumpFallState.motionType == 20);
        assert(jumpFallState.primaryAnimationId == 96);
        assert(jumpFallState.nextStateId == 16);
        assert(jumpLandState.name == "k_state_jump_land");
        assert(jumpLandState.motionType == 23);
        assert(jumpLandState.primaryAnimationId == 40);
        assert(jumpLandState.nextStateId == 0);
        const auto& swingThrowState = playerStateConfigs.states()[17];
        const auto& swingHangState = playerStateConfigs.states()[18];
        const auto& swingIdleState = playerStateConfigs.states()[19];
        assert(swingThrowState.name == "k_state_swing_web_throw");
        assert(swingThrowState.stateClass == 1);
        assert(swingThrowState.motionType == 26);
        assert(swingThrowState.primaryAnimationId == -1);
        assert(swingThrowState.animationIds ==
               (std::vector<std::int16_t>{98, 99}));
        assert(swingThrowState.nextStateId == 18);
        assert(swingThrowState.enterSoundConfigIds ==
               std::vector<std::int16_t>{9});
        assert(swingHangState.name == "k_state_swing_hang");
        assert(swingHangState.motionType == 27);
        assert(swingHangState.primaryAnimationId == -1);
        assert(swingHangState.nextStateId == -2);
        assert(swingHangState.enterSoundConfigIds ==
               std::vector<std::int16_t>{12});
        assert(swingIdleState.name == "k_state_swing_idle");
        assert(swingIdleState.motionType == 28);
        assert(swingIdleState.primaryAnimationId == -1);
        assert(swingIdleState.nextStateId == -2);
        assert(swingIdleState.enterSoundConfigIds ==
               std::vector<std::int16_t>{13});
        const auto& sliderMoveState = playerStateConfigs.states()[22];
        const auto& sliderLandState = playerStateConfigs.states()[23];
        assert(sliderMoveState.name == "k_state_trigger_slider_move");
        assert(sliderMoveState.stateClass == 5);
        assert(sliderMoveState.motionType == 400);
        assert(sliderMoveState.primaryAnimationId == 136);
        assert(sliderMoveState.nextStateId == -2);
        assert(sliderMoveState.enterSoundConfigIds ==
               std::vector<std::int16_t>{14});
        assert(sliderLandState.name == "k_state_trigger_slider_land");
        assert(sliderLandState.stateClass == 5);
        assert(sliderLandState.motionType == 401);
        assert(sliderLandState.primaryAnimationId == 42);
        assert(sliderLandState.nextStateId == 22);
        assert(jumpStartState.enterSoundConfigIds ==
               std::vector<std::int16_t>{8});
        assert(jumpLandState.enterSoundConfigIds ==
               std::vector<std::int16_t>{11});
        const auto* jumpSwoosh = playerStateConfigs.findSoundConfig(8);
        const auto* jumpLand = playerStateConfigs.findSoundConfig(11);
        assert(jumpSwoosh != nullptr);
        assert(jumpSwoosh->name == "k_mc_sfx_swoosh_jump");
        assert(jumpSwoosh->voxSoundIds ==
               (std::vector<std::int16_t>{44, 45, 46, 47, 48, 49}));
        assert(jumpLand != nullptr);
        assert(jumpLand->name == "k_mc_sfx_land");
        assert(jumpLand->voxSoundIds ==
               std::vector<std::int16_t>{38});
        const auto* webThrow = playerStateConfigs.findSoundConfig(9);
        const auto* swingStart = playerStateConfigs.findSoundConfig(12);
        const auto* swingEnd = playerStateConfigs.findSoundConfig(13);
        const auto* slideSound = playerStateConfigs.findSoundConfig(14);
        assert(webThrow != nullptr);
        assert(webThrow->name == "k_mc_sfx_web_throw");
        assert(webThrow->voxSoundIds ==
               (std::vector<std::int16_t>{67, 68, 69}));
        assert(swingStart != nullptr);
        assert(swingStart->name == "k_mc_sfx_swing_start");
        assert(swingStart->voxSoundIds ==
               std::vector<std::int16_t>{70});
        assert(swingEnd != nullptr);
        assert(swingEnd->name == "k_mc_sfx_swing_end");
        assert(swingEnd->voxSoundIds ==
               std::vector<std::int16_t>{71});
        assert(slideSound != nullptr);
        assert(slideSound->name == "k_mc_sfx_sliding");
        assert(slideSound->voxSoundIds ==
               std::vector<std::int16_t>{77});
        const auto* punchState = playerStateConfigs.findState(
            "k_state_idle_to_punch_right");
        assert(punchState != nullptr);
        assert(punchState->soundTriggerFrame == 9);
        assert(punchState->enterSoundConfigIds ==
               std::vector<std::int16_t>{1});
        assert(punchState->frameSoundConfigIds ==
               std::vector<std::int16_t>{34});
        const auto* punchSwoosh = playerStateConfigs.findSoundConfig(1);
        const auto* punchImpact = playerStateConfigs.findSoundConfig(34);
        assert(punchSwoosh != nullptr);
        assert(punchSwoosh->name == "k_mc_sfx_swoosh_punch_lag");
        assert(punchSwoosh->voxSoundIds ==
               (std::vector<std::int16_t>{60, 61}));
        assert(punchImpact != nullptr);
        assert(punchImpact->name == "k_mc_sfx_punch_impact");
        assert(punchImpact->voxSoundIds ==
               (std::vector<std::int16_t>{58, 59}));
        const auto* hurtState =
            playerStateConfigs.findState("k_state_hurt_light");
        assert(hurtState != nullptr);
        assert(hurtState->soundTriggerFrame == -1);
        assert(hurtState->enterSoundConfigIds ==
               std::vector<std::int16_t>{15});
        assert(hurtState->frameSoundConfigIds.empty());

        usm::audio::SoundEventCatalog soundCatalog;
        assert(soundCatalog.index(dataRoot / "sound", &voxSounds));
        assert(soundCatalog.eventCount() == 510);
        assert(soundCatalog.ambiguousEventCount() == 5);
        assert(soundCatalog.configuredEventCount() == 483);
        assert(soundCatalog.resolve("SFX_WEB_SWING_START") != nullptr);
        assert(soundCatalog.resolve("VFX_PROLOGUE_SPIDY_01") != nullptr);
        assert(soundCatalog.resolve("SFX_CUTSCENE_LV3_SPIDY_ARRIVES") !=
               nullptr);
        assert(soundCatalog.resolve("SFX_THUG_KNIFE_HURT_1") != nullptr);
        assert(soundCatalog.resolve("SFX_VERTICAL_IMPACT") != nullptr);
        usm::audio::PcmAudio catalogAudio;
        assert(soundCatalog.decode("SFX_WEB_SWING_START", catalogAudio));
        assert(catalogAudio.frameCount() > 0);

        usm::audio::PlayerStateSoundBank playerSounds;
        constexpr std::array<std::string_view, 8> gameplaySoundStates{
            "k_state_idle_to_punch_right", "k_state_hurt_light",
            "k_state_jump_start", "k_state_jump_land",
            "k_state_swing_web_throw", "k_state_swing_hang",
            "k_state_swing_idle", "k_state_trigger_slider_move"};
        assert(playerSounds.preload(playerStateConfigs, voxSounds,
                                    soundCatalog, gameplaySoundStates));
        assert(playerSounds.decodedVariantCount() == 20);
        std::size_t playerSoundPlayCount = 0;
        std::size_t loopingPlayerSoundCount = 0;
        const auto countPlayerSound =
            [&playerSoundPlayCount,
             &loopingPlayerSoundCount](const usm::audio::PcmAudio& clip,
                                       bool loop) {
                assert(clip.frameCount() > 0);
                loopingPlayerSoundCount += loop ? 1U : 0U;
                ++playerSoundPlayCount;
                return usm::Result::success();
            };
        assert(playerSounds.dispatchStateEnter(
            "k_state_idle_to_punch_right", countPlayerSound));
        assert(playerSounds.dispatchStateFrame(
            "k_state_idle_to_punch_right", countPlayerSound));
        assert(playerSounds.dispatchStateEnter("k_state_hurt_light",
                                               countPlayerSound));
        assert(playerSounds.dispatchStateEnter("k_state_jump_start",
                                               countPlayerSound));
        assert(playerSounds.dispatchStateEnter("k_state_jump_land",
                                               countPlayerSound));
        assert(playerSounds.dispatchStateEnter("k_state_swing_web_throw",
                                               countPlayerSound));
        assert(playerSounds.dispatchStateEnter("k_state_swing_hang",
                                               countPlayerSound));
        assert(playerSounds.dispatchStateEnter("k_state_swing_idle",
                                               countPlayerSound));
        assert(playerSounds.dispatchStateEnter(
            "k_state_trigger_slider_move", countPlayerSound));
        assert(playerSoundPlayCount == 9);
        assert(loopingPlayerSoundCount == 1);

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

        usm::filesystem::GbmpArchive sprites;
        assert(sprites.open(dataRoot / "sprites.pack"));
        std::vector<std::byte> spriteMetadataBytes;
        assert(sprites.read("interface.bsprite", spriteMetadataBytes));
        usm::assets::SpriteAtlas interfaceAtlas;
        const usm::Result atlasResult =
            interfaceAtlas.load(spriteMetadataBytes);
        if (!atlasResult) {
            std::cerr << "Interface sprite metadata failed: "
                      << atlasResult.message() << '\n';
            return 1;
        }
        assert(interfaceAtlas.flags() == 0x2000);
        assert(interfaceAtlas.modules().size() == 149);
        assert(interfaceAtlas.frameModules().size() == 257);
        assert(interfaceAtlas.frames().size() == 158);
        assert(interfaceAtlas.animationFrames().size() == 134);
        assert(interfaceAtlas.animations().size() == 40);
        for (const std::size_t hudFrame : {0x18U, 0x19U, 0x1bU, 0x1cU,
                                          0x1dU, 0x1fU}) {
            assert(!interfaceAtlas.modulesForFrame(hudFrame).empty());
        }

        std::vector<std::byte> interfaceTextureBytes;
        assert(sprites.read("interface.tga", interfaceTextureBytes));
        usm::assets::DdsAtcTexture interfaceTexture;
        const usm::Result interfaceTextureResult =
            interfaceTexture.load(interfaceTextureBytes);
        if (!interfaceTextureResult) {
            std::cerr << "Interface texture failed: "
                      << interfaceTextureResult.message() << '\n';
            return 1;
        }
        assert(interfaceTexture.image().width == 1024);
        assert(interfaceTexture.image().height == 1024);
        assert(interfaceTexture.image().pixels.size() == 1024U * 1024U * 4U);
        bool hasTransparentPixel = false;
        bool hasOpaquePixel = false;
        for (std::size_t alpha = 3;
             alpha < interfaceTexture.image().pixels.size(); alpha += 4) {
            hasTransparentPixel |=
                interfaceTexture.image().pixels[alpha] == 0;
            hasOpaquePixel |= interfaceTexture.image().pixels[alpha] == 0xff;
        }
        assert(hasTransparentPixel && hasOpaquePixel);

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
        assert(bootstrap.textCatalog().level().size() == 18);
        assert(bootstrap.textCatalog().tutorial().size() == 17);
        const auto* openingCaption =
            bootstrap.textCatalog().findLevelString(
                "STR_PROLOGUE_SPIDERMAN_01");
        assert(openingCaption != nullptr);
        assert(*openingCaption ==
               u"My spider-sense has been going wild all morning! What the "
               u"heck's wrong?!");
        const auto* jumpTutorial =
            bootstrap.textCatalog().findTutorialString("STR_JUMP");
        assert(jumpTutorial != nullptr);
        assert(jumpTutorial->find(u"jump") != std::u16string::npos);
        usm::game::CinematicUiRuntime cinematicUi;
        cinematicUi.bind(bootstrap.textCatalog());
        const auto tutorialCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 71;
            });
        assert(tutorialCinematic != bootstrap.cinematics().end());
        const auto tutorialCommand = std::find_if(
            tutorialCinematic->script.threads().back().commands.begin(),
            tutorialCinematic->script.threads().back().commands.end(),
            [](const usm::game::CinematicCommand& command) {
                return command.name == "Tutorial";
            });
        assert(tutorialCommand !=
               tutorialCinematic->script.threads().back().commands.end());
        assert(cinematicUi.applyCommand(*tutorialCommand));
        assert(cinematicUi.tutorialVisible());
        assert(cinematicUi.frame().text.find(u"[X]") !=
               std::u16string::npos);
        cinematicUi.update(2999, false);
        assert(cinematicUi.tutorialVisible());
        cinematicUi.update(1, false);
        assert(!cinematicUi.tutorialVisible());
        const usm::game::CinematicCommand* openingMessageCommand = nullptr;
        for (const auto& thread : bootstrap.introScript().threads()) {
            const auto command = std::find_if(
                thread.commands.begin(), thread.commands.end(),
                [](const usm::game::CinematicCommand& candidate) {
                    return candidate.name == "ShowMessage";
                });
            if (command != thread.commands.end()) {
                openingMessageCommand = &*command;
                break;
            }
        }
        assert(openingMessageCommand != nullptr);
        assert(cinematicUi.applyCommand(*openingMessageCommand));
        assert(cinematicUi.messageVisible());
        assert(cinematicUi.frame().text == *openingCaption);
        cinematicUi.update(3850, false);
        assert(!cinematicUi.messageVisible());
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
        const auto& swingThrowLeft =
            bootstrap.player().animationBank.clips()[98];
        const auto& swingThrowRight =
            bootstrap.player().animationBank.clips()[99];
        const auto& swingHangLeft =
            bootstrap.player().animationBank.clips()[168];
        const auto& swingHangRight =
            bootstrap.player().animationBank.clips()[169];
        assert(swingThrowLeft.name == "jump_to_throw_web_left");
        assert(swingThrowLeft.durationMilliseconds() == 134);
        assert(swingThrowRight.name == "jump_to_throw_web_right");
        assert(swingThrowRight.durationMilliseconds() == 267);
        assert(swingHangLeft.name == "swing_hang_fwd_left");
        assert(swingHangLeft.durationMilliseconds() == 733);
        assert(swingHangRight.name == "swing_hang_fwd_right");
        assert(swingHangRight.durationMilliseconds() == 734);
        const auto& sliderLandClip =
            bootstrap.player().animationBank.clips()[42];
        const auto& sliderMoveClip =
            bootstrap.player().animationBank.clips()[136];
        const auto& sliderJumpClip =
            bootstrap.player().animationBank.clips()[141];
        assert(sliderLandClip.name == "fall_to_slide");
        assert(sliderLandClip.durationMilliseconds() == 401);
        assert(sliderMoveClip.name == "slide");
        assert(sliderMoveClip.durationMilliseconds() == 532);
        assert(sliderJumpClip.name == "slide_to_jump");
        assert(sliderJumpClip.durationMilliseconds() == 433);
        constexpr std::array<std::string_view, 6> releaseClipNames{
            "swing_hang_left_to_swing_idle_2",
            "swing_hang_left_to_swing_idle_3",
            "swing_hang_left_to_swing_idle_4",
            "swing_hang_right_to_swing_idle_2",
            "swing_hang_right_to_swing_idle_3",
            "swing_hang_right_to_swing_idle_4",
        };
        for (std::size_t index = 0; index < releaseClipNames.size(); ++index) {
            const auto& clip =
                bootstrap.player().animationBank.clips()[170 + index];
            assert(clip.name == releaseClipNames[index]);
            assert(clip.durationMilliseconds() >= 1066);
            assert(clip.durationMilliseconds() <= 1067);
        }
        assert(bootstrap.player().animationBank.tracks()[1].property ==
               usm::assets::ColladaAnimationProperty::TranslationZ);
        assert(bootstrap.player().animationBank.tracks()[40].property ==
               usm::assets::ColladaAnimationProperty::Translation);
        assert(bootstrap.player().animationBank.tracks()[44].property ==
               usm::assets::ColladaAnimationProperty::TranslationX);
        assert(bootstrap.waypoints().size() == 15);
        assert(bootstrap.webGrabPoints().size() == 9);
        assert(bootstrap.slides().size() == 2);
        const auto roofSlide = std::find_if(
            bootstrap.slides().begin(), bootstrap.slides().end(),
            [](const auto& slide) { return slide.objectId == 1038; });
        assert(roofSlide != bootstrap.slides().end());
        assert(roofSlide->linkedWaypointId == 429);
        assert(roofSlide->enabled);
        assert(!roofSlide->electricShock);
        assert(roofSlide->waypointIds ==
               (std::vector<std::int32_t>{429, 430}));
        const auto exitSlide = std::find_if(
            bootstrap.slides().begin(), bootstrap.slides().end(),
            [](const auto& slide) { return slide.objectId == 1039; });
        assert(exitSlide != bootstrap.slides().end());
        assert(exitSlide->waypointIds ==
               (std::vector<std::int32_t>{445, 446}));

        std::array<usm::game::LevelWayPointAsset, 3> testSlideWaypoints;
        testSlideWaypoints[0].objectId = 1;
        testSlideWaypoints[0].position = {0.0F, 0.0F, 0.0F};
        testSlideWaypoints[0].nextWaypointIds[0] = 2;
        testSlideWaypoints[1].objectId = 2;
        testSlideWaypoints[1].position = {100.0F, 0.0F, 0.0F};
        testSlideWaypoints[1].nextWaypointIds[0] = 3;
        testSlideWaypoints[2].objectId = 3;
        testSlideWaypoints[2].position = {200.0F, 0.0F, 0.0F};
        testSlideWaypoints[2].useGravityWhenEnd = false;
        testSlideWaypoints[2].electricShock = true;
        usm::game::LevelSlideAsset testSlide;
        testSlide.objectId = 99;
        testSlide.waypointIds = {1, 2, 3};
        const std::array<usm::game::LevelSlideAsset, 1> testSlides{
            testSlide};
        usm::game::LevelSlideRuntime testSlideRuntime;
        testSlideRuntime.bind(testSlides, testSlideWaypoints);
        assert(testSlideRuntime.findCatch({25.0F, 801.0F, 0.0F}).slide ==
               nullptr);
        const auto caughtSlide =
            testSlideRuntime.findCatch({25.0F, 10.0F, 0.0F});
        assert(caughtSlide.slide == &testSlides[0]);
        assert(caughtSlide.segmentIndex == 0);
        assert(std::abs(caughtSlide.projectedPosition.x - 25.0F) < 0.001F);
        assert(std::abs(caughtSlide.distanceSquared - 100.0F) < 0.001F);
        assert(testSlideRuntime.start(caughtSlide, 100.0F));
        assert(testSlideRuntime.active());
        assert(std::abs(testSlideRuntime.position().x - 25.0F) < 0.001F);
        testSlideRuntime.update(500);
        assert(std::abs(testSlideRuntime.position().x - 75.0F) < 0.001F);
        testSlideRuntime.update(2000);
        assert(!testSlideRuntime.active());
        assert(std::abs(testSlideRuntime.position().x - 200.0F) < 0.001F);
        const auto slideExit = testSlideRuntime.finish();
        assert(std::abs(slideExit.velocityCentimetersPerSecond.x - 100.0F) <
               0.001F);
        assert(!slideExit.useGravity);
        assert(slideExit.electricShock);
        const auto webGrab377 = std::find_if(
            bootstrap.webGrabPoints().begin(),
            bootstrap.webGrabPoints().end(), [](const auto& point) {
                return point.objectId == 377;
            });
        assert(webGrab377 != bootstrap.webGrabPoints().end());
        assert(webGrab377->directionControlPointId == 408);
        assert(std::abs(webGrab377->position.x - 4845.52F) < 0.01F);
        assert(std::abs(webGrab377->position.y + 6079.96F) < 0.01F);
        assert(std::abs(webGrab377->position.z - 535.477F) < 0.01F);
        assert(std::abs(webGrab377->direction.x + 0.969569F) < 0.00001F);
        assert(std::abs(webGrab377->direction.y - 0.232773F) < 0.00001F);
        assert(std::abs(webGrab377->direction.z + 0.075849F) < 0.00001F);
        assert(webGrab377->length == 600.0F);
        assert(webGrab377->visibleLength == 1500.0F);
        assert(webGrab377->verticalAngleDegrees == 80.0F);
        assert(webGrab377->horizontalAngleDegrees == -15.0F);
        assert(webGrab377->exitSpeed == 0.45F);
        assert(!webGrab377->cannotControl);
        assert(!webGrab377->hasTargetWaypoint);
        const auto webGrab383 = std::find_if(
            bootstrap.webGrabPoints().begin(),
            bootstrap.webGrabPoints().end(), [](const auto& point) {
                return point.objectId == 383;
            });
        assert(webGrab383 != bootstrap.webGrabPoints().end());
        assert(webGrab383->cannotControl);
        assert(webGrab383->targetWaypointId == 30031);
        assert(webGrab383->hasTargetWaypoint);
        assert(std::abs(webGrab383->targetWaypointPosition.x - 2712.0F) <
               0.01F);
        assert(std::abs(webGrab383->targetWaypointPosition.y - 2953.27F) <
               0.01F);
        assert(std::abs(webGrab383->targetWaypointPosition.z - 148.214F) <
               0.01F);
        const auto slideWaypoint = std::find_if(
            bootstrap.waypoints().begin(), bootstrap.waypoints().end(),
            [](const auto& waypoint) { return waypoint.objectId == 429; });
        assert(slideWaypoint != bootstrap.waypoints().end());
        assert(slideWaypoint->enabled);
        assert(slideWaypoint->nextWaypointIds[0] == 430);
        assert(slideWaypoint->nextWaypointIds[1] == -1);
        assert(slideWaypoint->useGravityWhenEnd);
        assert(!slideWaypoint->unstandable);
        assert(slideWaypoint->jumpDirection == 0);
        assert(slideWaypoint->linkedCameraAreaId == -1);
        assert(bootstrap.attackConfigs().attacks().size() == 85);
        const auto* normalAttack = bootstrap.attackConfigs().find(7);
        assert(normalAttack != nullptr);
        assert(normalAttack->name == "ATTACK_HIT_NORMAL");
        assert(normalAttack->damage == 35.0F);
        assert(normalAttack->maximumReach() == 200.0F);
        assert(normalAttack->minimumAngleDegrees == -90.0F);
        assert(normalAttack->maximumAngleDegrees == 90.0F);
        const auto* hammerAttack = bootstrap.attackConfigs().find(19);
        const auto* bigThugAttack = bootstrap.attackConfigs().find(21);
        const auto* sandmanAttack = bootstrap.attackConfigs().find(69);
        assert(hammerAttack != nullptr && hammerAttack->damage == 50.0F &&
               hammerAttack->maximumReach() == 300.0F);
        assert(bigThugAttack != nullptr && bigThugAttack->damage == 70.0F &&
               bigThugAttack->maximumReach() == 500.0F);
        assert(sandmanAttack != nullptr && sandmanAttack->damage == 75.0F &&
               sandmanAttack->maximumReach() == 400.0F);
        assert(bootstrap.enemySpecialActions().actions().size() == 230);
        const auto& behaviorConfigs = bootstrap.enemyBehaviorConfigs();
        const auto& rangeAttackConfigs = bootstrap.enemyRangeAttackConfigs();
        const auto& enemyAttributes = bootstrap.enemyAttributeConfigs();
        assert(enemyAttributes.definitions().size() == 25);
        const auto* gunThugAttributes = enemyAttributes.find(3);
        const auto* bigThugAttributes = enemyAttributes.find(4);
        assert(gunThugAttributes != nullptr &&
               gunThugAttributes->exportedId == 3 &&
               gunThugAttributes->name == "THUG_GUN" &&
               gunThugAttributes->rangedAttackTypeMapIndices ==
                   std::vector<std::int32_t>{4});
        assert(bigThugAttributes != nullptr &&
               bigThugAttributes->name == "THUG_BIG" &&
               bigThugAttributes->rangedAttackTypeMapIndices ==
                   std::vector<std::int32_t>{6});
        assert(usm::game::resolveEnemyRangeWeaponType(4) == 13);
        assert(usm::game::resolveEnemyRangeWeaponType(6) == 17);
        const auto& attackIntervals = bootstrap.enemyAttackIntervalConfigs();
        assert(attackIntervals.definitions().size() == 24);
        const auto* gunLineInterval = attackIntervals.findForWeaponType(13);
        assert(gunLineInterval != nullptr && gunLineInterval->id == 8 &&
               gunLineInterval->name == "ENEMY_RANGE_ATTACK_GUN_LINE" &&
               gunLineInterval->weaponTypeMapIndex == 4 &&
               gunLineInterval->intervalMilliseconds[3] == 2000.0F);
        assert(rangeAttackConfigs.definitions().size() == 17);
        const auto* rangeAttack01 = rangeAttackConfigs.findByMapId(7);
        const auto* rangeAttack05 = rangeAttackConfigs.findByMapId(11);
        const auto* rangeAttack15 = rangeAttackConfigs.findByMapId(22);
        assert(rangeAttack01 != nullptr && rangeAttack01->id == 0 &&
               rangeAttack01->name == "RANGE_ATTACK_01" &&
               rangeAttack01->animationDurationMilliseconds == 1000.0F &&
               rangeAttack01->projectileSpeedCentimetersPerSecond == -1.0F &&
               rangeAttack01->damage == 50.0F);
        assert(rangeAttack05 != nullptr && rangeAttack05->id == 4 &&
               rangeAttack05->animationDurationMilliseconds == 1000.0F &&
               rangeAttack05->projectileSpeedCentimetersPerSecond == 600.0F &&
               rangeAttack05->damage == 50.0F);
        assert(rangeAttack15 != nullptr && rangeAttack15->id == 15 &&
               rangeAttack15->animationDurationMilliseconds == 0.0F &&
               rangeAttack15->projectileSpeedCentimetersPerSecond == -1.0F &&
               rangeAttack15->damage == 80.0F);
        const auto* gunLineAttack =
            rangeAttackConfigs.findByMapId(gunLineInterval->id);
        assert(gunLineAttack != nullptr && gunLineAttack->id == 1 &&
               gunLineAttack->animationDurationMilliseconds == 1000.0F &&
               gunLineAttack->damage == 30.0F);
        assert(behaviorConfigs.animationMaps().size() == 239);
        assert(behaviorConfigs.animationLists().size() == 202);
        assert(behaviorConfigs.soundMaps().size() == 63);
        assert(behaviorConfigs.states().size() == 221);
        const auto* fireLeftState = behaviorConfigs.findState(
            "ENEMY_BEHAVIOR_RANGE_ATTACK_STATE_DO_ATTACK_FIRE_LEFT");
        const auto* fireRightState = behaviorConfigs.findState(
            "ENEMY_BEHAVIOR_RANGE_ATTACK_STATE_DO_ATTACK_FIRE_RIGHT");
        assert(fireLeftState != nullptr && fireLeftState->id == 26);
        assert(fireRightState != nullptr && fireRightState->id == 28);
        assert(behaviorConfigs.resolveStateAnimationNames(fireLeftState->name, 3) ==
               std::vector<std::string_view>{"idle_shoot_left_idle"});
        assert(behaviorConfigs.resolveStateAnimationNames(fireRightState->name, 3) ==
               std::vector<std::string_view>{"idle_shoot_right_idle"});
        const auto* commonHurtState = behaviorConfigs.findState(
            "ENEMY_BEHAVIOR_HURT_STATE_COMMON");
        assert(commonHurtState != nullptr);
        assert(commonHurtState->id == 49);
        assert(commonHurtState->animationListIds ==
               std::vector<std::int16_t>{28});
        assert(commonHurtState->soundMapIds ==
               (std::vector<std::int16_t>{12, 13, 14}));
        assert(behaviorConfigs.resolveStateAnimationNames(
                   commonHurtState->name, 0) ==
               (std::vector<std::string_view>{"idle_hurt_idle",
                                               "idle_hurt_left_idle",
                                               "idle_hurt_right_idle"}));
        assert(behaviorConfigs.resolveStateSoundIds(commonHurtState->name,
                                                    0) ==
               (std::vector<std::int32_t>{185, 186, 187}));
        assert(behaviorConfigs.resolveStateSoundIds(commonHurtState->name,
                                                    1) ==
               (std::vector<std::int32_t>{189, 190, 191}));
        assert(behaviorConfigs.resolveStateAnimationNames(
                   "ENEMY_BEHAVIOR_DEAD_STATE", 0) ==
               std::vector<std::string_view>{"idle_onground"});
        assert(behaviorConfigs.resolveStateSoundIds(
                   "ENEMY_BEHAVIOR_DEAD_STATE", 0) ==
               std::vector<std::int32_t>{188});
        assert(behaviorConfigs.resolveSoundMap(16, 0) == 178);
        assert(behaviorConfigs.resolveSoundMap(16, 1) == 178);
        const auto knifeAttackEvents =
            bootstrap.enemySpecialActions().findAttackEvents(
                0, "idle_knife_at_idle");
        assert(knifeAttackEvents.size() == 2);
        assert(knifeAttackEvents[0]->name == "THUG_KNIFE_01");
        assert(knifeAttackEvents[0]->keyFramePercent == 45);
        assert(knifeAttackEvents[0]->attackId == 6);
        assert(knifeAttackEvents[0]->soundMapIds.size() == 1);
        assert(knifeAttackEvents[0]->soundMapIds.front() == 16);
        assert(knifeAttackEvents[1]->keyFramePercent == 75);
        const auto batAttackEvents =
            bootstrap.enemySpecialActions().findAttackEvents(
                1, "idle_at1_idle");
        assert(batAttackEvents.size() == 1);
        assert(batAttackEvents.front()->name == "THUG_BAT_01");
        assert(batAttackEvents.front()->keyFramePercent == 47);
        assert(batAttackEvents.front()->attackId == 7);
        const auto bigThugAttackEvents =
            bootstrap.enemySpecialActions().findAttackEvents(
                4, "idlebaz_rush_attack_idlebaz");
        assert(bigThugAttackEvents.size() == 4);
        assert(bigThugAttackEvents.front()->keyFramePercent == 64);
        assert(bigThugAttackEvents.back()->keyFramePercent == 76);
        assert(bigThugAttackEvents.front()->attackId == 21);
        const auto hammerAttackEvents =
            bootstrap.enemySpecialActions().findAttackEvents(
                5, "idle_attack_hammer_idle");
        assert(hammerAttackEvents.size() == 1);
        assert(hammerAttackEvents.front()->keyFramePercent == 60);
        assert(hammerAttackEvents.front()->attackId == 19);
        const auto sandmanAttackEvents =
            bootstrap.enemySpecialActions().findAttackEvents(
                16, "ground_attack1");
        assert(sandmanAttackEvents.size() == 1);
        assert(sandmanAttackEvents.front()->keyFramePercent == 50);
        assert(sandmanAttackEvents.front()->attackId == 69);
        usm::audio::EnemyBehaviorSoundBank enemySounds;
        constexpr std::array<std::int16_t, 6> firstLevelEnemyTypes{
            0, 1, 3, 4, 5, 16};
        assert(enemySounds.preload(behaviorConfigs,
                                   bootstrap.enemySpecialActions(),
                                   voxSounds, soundCatalog,
                                   firstLevelEnemyTypes));
        assert(enemySounds.decodedSoundCount() == 32);
        std::size_t enemySoundPlayCount = 0;
        assert(enemySounds.dispatch(
            185, [&enemySoundPlayCount](const usm::audio::PcmAudio& clip,
                                        bool loop) {
                assert(clip.frameCount() > 0);
                assert(!loop);
                ++enemySoundPlayCount;
                return usm::Result::success();
            }));
        assert(enemySoundPlayCount == 1);
        assert(bootstrap.triggers().size() == 27);
        const auto firstEncounterTrigger = std::find_if(
            bootstrap.triggers().begin(), bootstrap.triggers().end(),
            [](const usm::game::LevelTriggerAsset& trigger) {
                return trigger.objectId == 534;
            });
        assert(firstEncounterTrigger != bootstrap.triggers().end());
        assert(firstEncounterTrigger->name == "Trigger_3thugs");
        assert(firstEncounterTrigger->whileOutsideCinematicId == 1162);
        assert(!firstEncounterTrigger->autoDisabled);
        assert(firstEncounterTrigger->sizes.y == 1664.575195F);
        assert(std::any_of(
            bootstrap.triggers().begin(), bootstrap.triggers().end(),
            [](const usm::game::LevelTriggerAsset& trigger) {
                return trigger.objectId == 1263;
            }));
        usm::game::LevelTriggerRuntime initialTriggerRuntime;
        initialTriggerRuntime.bind(
            std::span<const usm::game::LevelTriggerAsset>(
                &*firstEncounterTrigger, 1));
        const auto initialEncounterEvents =
            initialTriggerRuntime.update(bootstrap.player().position);
        assert(initialEncounterEvents.size() == 1);
        assert(initialEncounterEvents.front().cinematicId == 1162);
        usm::game::LevelTriggerRuntime triggerRuntime;
        triggerRuntime.bind(std::span<const usm::game::LevelTriggerAsset>(
            &*firstEncounterTrigger, 1));
        const usm::assets::Vector3 triggerCenter{
            firstEncounterTrigger->worldTransform[12],
            firstEncounterTrigger->worldTransform[13],
            firstEncounterTrigger->worldTransform[14]};
        assert(triggerRuntime.update(triggerCenter).empty());
        const auto encounterEvents = triggerRuntime.update(
            {triggerCenter.x + firstEncounterTrigger->sizes.x + 10.0F,
             triggerCenter.y, triggerCenter.z});
        assert(encounterEvents.size() == 1);
        assert(encounterEvents.front().triggerId == 534);
        assert(encounterEvents.front().cinematicId == 1162);
        assert(encounterEvents.front().kind ==
               usm::game::TriggerEventKind::WhileOutside);
        assert(bootstrap.cinematics().size() == 43);
        const auto unavailableCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return !cinematic.scriptAvailable;
            });
        assert(unavailableCinematic != bootstrap.cinematics().end());
        assert(unavailableCinematic->objectId == 1239);
        assert(unavailableCinematic->scriptFile ==
               "cinematics/levelnew_01_1239_cinematic.cff");
        assert(std::count_if(
                   bootstrap.cinematics().begin(),
                   bootstrap.cinematics().end(),
                   [](const usm::game::LevelCinematicAsset& cinematic) {
                       return cinematic.scriptAvailable;
                   }) == 42);
        const auto firstEncounterCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1162;
            });
        assert(firstEncounterCinematic != bootstrap.cinematics().end());
        assert(firstEncounterCinematic->scriptFile ==
               "cinematics/levelnew_01_1162_cinematic.cff");
        assert(firstEncounterCinematic->script.commandCount() > 0);
        assert(bootstrap.buttonConfigs().definitions().size() == 20);
        const auto* levelOneQteConfig = bootstrap.buttonConfigs().find(6);
        assert(levelOneQteConfig != nullptr);
        assert(levelOneQteConfig->name == "k_igm_button_qte_1_2");
        assert(levelOneQteConfig->interactionType == 1);
        assert(levelOneQteConfig->interactionValue == 14);
        assert(levelOneQteConfig->screenX == 360.0F);
        assert(levelOneQteConfig->screenY == 101.0F);
        assert(levelOneQteConfig->durationMilliseconds == 3900.0F);
        assert(levelOneQteConfig->normalAnimationId == 6);
        assert(levelOneQteConfig->activeAnimationId == 7);
        assert(levelOneQteConfig->requiredActionCount == -1);
        assert(levelOneQteConfig->sequence.empty());
        usm::game::CinematicCommand startLevelOneQte;
        startLevelOneQte.name = "StartQTE";
        startLevelOneQte.attributes = {
            {"int", "QTEID", "6"},
            {"int", "^ID^Cinematic^Success", "20006"},
            {"int", "^ID^Cinematic^Fail", "20010"},
        };
        usm::game::QuickTimeEventRuntime successfulQte;
        successfulQte.bind(bootstrap.buttonConfigs());
        assert(successfulQte.applyCommand(startLevelOneQte));
        assert(successfulQte.active());
        assert(successfulQte.durationMilliseconds() == 3900);
        successfulQte.update(2000, true);
        assert(!successfulQte.active());
        assert(successfulQte.consumeCinematicRequest() == 20006);
        assert(!successfulQte.consumeCinematicRequest().has_value());
        usm::game::QuickTimeEventRuntime failedQte;
        failedQte.bind(bootstrap.buttonConfigs());
        assert(failedQte.applyCommand(startLevelOneQte));
        failedQte.update(3899, false);
        assert(failedQte.active());
        failedQte.update(1, false);
        assert(!failedQte.active());
        assert(failedQte.consumeCinematicRequest() == 20010);
        const auto roomNineCameraCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 20013;
            });
        assert(roomNineCameraCinematic != bootstrap.cinematics().end());
        assert(roomNineCameraCinematic->cameraTrack.valid());
        assert(roomNineCameraCinematic->cameraTrack.keyframes().size() == 8);
        const auto firstRoomNineCamera =
            roomNineCameraCinematic->cameraTrack.sample(0);
        assert(std::abs(firstRoomNineCamera.target.x - 13078.953125F) <
               0.01F);
        assert(std::abs(firstRoomNineCamera.position.x - 13170.248F) <
               0.01F);
        assert(std::abs(firstRoomNineCamera.position.y + 7534.116F) <
               0.01F);
        const auto middleRoomNineCamera =
            roomNineCameraCinematic->cameraTrack.sample(500);
        const auto secondRoomNineCamera =
            roomNineCameraCinematic->cameraTrack.sample(1000);
        assert(std::abs(middleRoomNineCamera.target.x -
                        (firstRoomNineCamera.target.x +
                         secondRoomNineCamera.target.x) *
                            0.5F) < 0.01F);
        const auto heldRoomNineCamera =
            roomNineCameraCinematic->cameraTrack.sample(2175);
        const auto cameraAt2150 =
            roomNineCameraCinematic->cameraTrack.sample(2150);
        assert(std::abs(heldRoomNineCamera.position.x -
                        cameraAt2150.position.x) < 0.01F);
        assert(std::count_if(
                   bootstrap.cinematics().begin(),
                   bootstrap.cinematics().end(),
                   [](const usm::game::LevelCinematicAsset& cinematic) {
                       return cinematic.cameraTrack.valid();
                   }) == 8);
        assert(std::count_if(
                   bootstrap.cinematics().begin(),
                   bootstrap.cinematics().end(),
                   [](const usm::game::LevelCinematicAsset& cinematic) {
                       return cinematic.hasColladaPlayback();
                   }) == 3);
        const auto beforeBossCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1254;
            });
        assert(beforeBossCinematic != bootstrap.cinematics().end());
        assert(beforeBossCinematic->cameraAnimationFile ==
               "meshes_bin/camera_lv1_beforeboss.bdae");
        assert(beforeBossCinematic->animatedCamera.valid());
        assert(beforeBossCinematic->actors.size() == 2);
        assert(beforeBossCinematic->actors.front().objectId == 288);
        assert(beforeBossCinematic->actors.back().objectId == 1272);
        assert(beforeBossCinematic->actors.back()
                   .animationStartMilliseconds == 15700);
        assert(beforeBossCinematic->colladaDurationMilliseconds > 34000);
        assert(beforeBossCinematic->nextCinematicId == 1256);
        assert(!beforeBossCinematic->levelEndAfterPlayback);
        assert(!beforeBossCinematic->gameEndAfterPlayback);
        const auto levelEndCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1238;
            });
        assert(levelEndCinematic != bootstrap.cinematics().end());
        assert(levelEndCinematic->actors.size() == 4);
        assert(levelEndCinematic->actors.back().objectId == 1275);
        assert(levelEndCinematic->actors.back()
                   .animationStartMilliseconds == 28650);
        assert(levelEndCinematic->nextCinematicId == -1);
        assert(levelEndCinematic->levelEndAfterPlayback);
        assert(!levelEndCinematic->gameEndAfterPlayback);
        const auto gameOverCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1267;
            });
        assert(gameOverCinematic != bootstrap.cinematics().end());
        assert(gameOverCinematic->actors.size() == 4);
        assert(gameOverCinematic->animatedCamera.sample(0).farPlane ==
               20000.0F);
        assert(!gameOverCinematic->levelEndAfterPlayback);
        assert(gameOverCinematic->gameEndAfterPlayback);
        const auto enemyGateCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 20026;
            });
        assert(enemyGateCinematic != bootstrap.cinematics().end());
        assert(enemyGateCinematic->script.commandCount() == 8);
        usm::game::CinematicPlayer enemyGatePlayer;
        assert(enemyGatePlayer.start(enemyGateCinematic->script));
        bool enemyGateSatisfied = false;
        const auto evaluateEnemyGate =
            [&enemyGateSatisfied](const usm::game::CinematicThread&,
                                  const usm::game::CinematicCommand& command) {
                return command.name != "IfEnemyDead" || enemyGateSatisfied;
            };
        assert(enemyGatePlayer.advanceToConditional(0, evaluateEnemyGate));
        assert(enemyGatePlayer.dispatchedCommandCount() == 2);
        assert(!enemyGatePlayer.finished());
        enemyGateSatisfied = true;
        assert(enemyGatePlayer.advanceToConditional(0, evaluateEnemyGate));
        assert(enemyGatePlayer.dispatchedCommandCount() == 8);
        assert(enemyGatePlayer.finished());
        usm::game::LevelEnemyRuntime enemyRuntime;
        assert(enemyRuntime.initialize(bootstrap));
        usm::game::CinematicThread showHealthThread;
        showHealthThread.objectId = bootstrap.player().objectId;
        usm::game::CinematicCommand showHammerHealth;
        showHammerHealth.name = "ShowHealth";
        showHammerHealth.attributes.push_back(
            {"int", "ObjectID", "1139"});
        assert(enemyRuntime.applyCinematicCommand(
            bootstrap, showHealthThread, showHammerHealth));
        assert(enemyRuntime.shownHealthBarEnemy() != nullptr);
        assert(enemyRuntime.shownHealthBarEnemy()->asset->objectId == 1139);
        assert(enemyRuntime.shownHealthBarEnemy()->asset->enemyTypeId == 5);
        showHammerHealth.attributes.front().value = "1199";
        assert(enemyRuntime.applyCinematicCommand(
            bootstrap, showHealthThread, showHammerHealth));
        assert(enemyRuntime.shownHealthBarEnemy()->asset->objectId == 1199);
        assert(enemyRuntime.shownHealthBarEnemy()->asset->enemyTypeId == 16);
        showHammerHealth.attributes.front().value = "not-an-id";
        assert(!enemyRuntime.applyCinematicCommand(
            bootstrap, showHealthThread, showHammerHealth));
        constexpr std::array<std::int32_t, 16> conditionEnemyIds{
            394, 395, 397, 398, 399, 401, 488, 489,
            505, 506, 1139, 1199, 1251, 10339, 10340, 30000,
        };
        for (const std::int32_t enemyId : conditionEnemyIds) {
            assert(enemyRuntime.find(enemyId) != nullptr);
        }
        usm::game::LevelEnemyRuntime killedEnemyRuntime;
        assert(killedEnemyRuntime.initialize(bootstrap));
        usm::game::CinematicThread killEnemyThread;
        killEnemyThread.objectId = 30000;
        assert(killedEnemyRuntime.applyCinematicCommand(
            bootstrap, killEnemyThread,
            usm::game::CinematicCommand{0, -1, "KillObject", {}}));
        assert(killedEnemyRuntime.find(30000)->health == 0.0F);
        assert(killedEnemyRuntime.find(30000)->activeAnimation ==
               "idle_death_on__ground_back");
        assert(killedEnemyRuntime.find(30000)->behavior ==
               usm::game::EnemyBehaviorState::Dead);
        usm::game::CinematicPlayer encounterPlayer;
        assert(encounterPlayer.start(firstEncounterCinematic->script));
        usm::Result encounterCommandResult = usm::Result::success();
        assert(encounterPlayer.advanceTo(
            0, [&bootstrap, &enemyRuntime, &encounterCommandResult](
                   const usm::game::CinematicThread& thread,
                   const usm::game::CinematicCommand& command) {
                if (encounterCommandResult) {
                    encounterCommandResult = enemyRuntime.applyCinematicCommand(
                        bootstrap, thread, command);
                }
            }));
        assert(encounterCommandResult);
        const auto* scriptedKnifeEnemy = enemyRuntime.find(394);
        assert(scriptedKnifeEnemy != nullptr);
        assert(!scriptedKnifeEnemy->aiEnabled);
        assert(scriptedKnifeEnemy->activeAnimation == "idle_knife_at_idle");
        assert(std::abs(scriptedKnifeEnemy->position.x - 13266.816406F) <
               0.01F);
        assert(encounterPlayer.advanceTo(
            1600, [&bootstrap, &enemyRuntime, &encounterCommandResult](
                      const usm::game::CinematicThread& thread,
                      const usm::game::CinematicCommand& command) {
                if (encounterCommandResult) {
                    encounterCommandResult = enemyRuntime.applyCinematicCommand(
                        bootstrap, thread, command);
                }
            }));
        assert(encounterCommandResult);
        assert(enemyRuntime.find(394)->visible);
        const auto beginEncounterCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 71;
            });
        assert(beginEncounterCinematic != bootstrap.cinematics().end());
        usm::game::CinematicPlayer beginEncounterPlayer;
        assert(beginEncounterPlayer.start(beginEncounterCinematic->script));
        assert(beginEncounterPlayer.advanceTo(
            1550, [&bootstrap, &enemyRuntime, &encounterCommandResult](
                      const usm::game::CinematicThread& thread,
                      const usm::game::CinematicCommand& command) {
                if (encounterCommandResult) {
                    encounterCommandResult = enemyRuntime.applyCinematicCommand(
                        bootstrap, thread, command);
                }
            }));
        assert(encounterCommandResult);
        assert(enemyRuntime.find(394)->aiEnabled);
        assert(enemyRuntime.find(395)->aiEnabled);
        assert(enemyRuntime.find(397)->aiEnabled);
        assert(bootstrap.enemyArchetypes().size() == 6);
        assert(bootstrap.enemies().size() == 34);
        const auto firstKnifeEnemy = std::find_if(
            bootstrap.enemies().begin(), bootstrap.enemies().end(),
            [](const usm::game::LevelEnemyAsset& enemy) {
                return enemy.objectId == 394;
            });
        assert(firstKnifeEnemy != bootstrap.enemies().end());
        assert(firstKnifeEnemy->health == 500.0F);
        assert(firstKnifeEnemy->aiEnabled);
        assert(firstKnifeEnemy->lineSpeedCentimetersPerMillisecond == 0.3F);
        assert(firstKnifeEnemy->awarenessRadius == 1500.0F);
        assert(firstKnifeEnemy->initialAnimation == "idle_knife_at_idle");
        assert(bootstrap.enemyArchetypes()[firstKnifeEnemy->archetypeIndex]
                   .animationFile ==
               "../entities/meshes_bin/thug_bat_anim.bdae");
        const auto bigRangeEnemy = std::find_if(
            bootstrap.enemies().begin(), bootstrap.enemies().end(),
            [](const usm::game::LevelEnemyAsset& enemy) {
                return enemy.objectId == 30000;
            });
        assert(bigRangeEnemy != bootstrap.enemies().end());
        assert(bigRangeEnemy->gameType == "RangeThug_big");
        assert(bigRangeEnemy->enemyTypeId == 4);
        assert(bigRangeEnemy->health == 800.0F);
        assert(bigRangeEnemy->initialAnimation == "idlebaz");
        assert(bootstrap.enemyArchetypes()[bigRangeEnemy->archetypeIndex]
                   .animationBank.findClip("idle_death_on__ground_back") !=
               nullptr);
        usm::game::LevelEnemyRuntime bigThugAttackRuntime;
        assert(bigThugAttackRuntime.initialize(bootstrap));
        usm::game::CinematicThread enableBigThugThread;
        enableBigThugThread.objectId = 30000;
        assert(bigThugAttackRuntime.applyCinematicCommand(
            bootstrap, enableBigThugThread,
            usm::game::CinematicCommand{0, -1, "EnableAI", {}}));
        const auto* attackingBigThug = bigThugAttackRuntime.find(30000);
        assert(attackingBigThug != nullptr);
        const auto* bigThugAttackClip =
            bootstrap.enemyArchetypes()[attackingBigThug->asset->archetypeIndex]
                .animationBank.findClip("idlebaz_rush_attack_idlebaz");
        assert(bigThugAttackClip != nullptr);
        const usm::assets::Vector3 bigThugVictim{
            attackingBigThug->position.x + 100.0F,
            attackingBigThug->position.y,
            attackingBigThug->position.z,
        };
        bigThugAttackRuntime.updateGameplay(
            bigThugAttackClip->durationMilliseconds() * 64U / 100U,
            bigThugVictim);
        assert(bigThugAttackRuntime.find(30000)->activeAnimation ==
               "idlebaz_rush_attack_idlebaz");
        const auto bigThugHits = bigThugAttackRuntime.consumePlayerHits();
        assert(bigThugHits.size() == 1);
        assert(bigThugHits.front().attackId == 21);
        assert(bigThugHits.front().damage == 70.0F);
        const auto findEnemyAsset = [&bootstrap](std::int32_t objectId) {
            return std::find_if(
                bootstrap.enemies().begin(), bootstrap.enemies().end(),
                [objectId](const usm::game::LevelEnemyAsset& enemy) {
                    return enemy.objectId == objectId;
                });
        };
        const auto gunEnemy = findEnemyAsset(10344);
        const auto hammerEnemy = findEnemyAsset(1139);
        const auto sandmanBoss = findEnemyAsset(1199);
        assert(gunEnemy != bootstrap.enemies().end());
        assert(gunEnemy->enemyTypeId == 3);
        assert(gunEnemy->initialAnimation == "idle");
        const auto gunFireEvents =
            bootstrap.enemySpecialActions().findEvents(
                3, "idle_shoot_left_idle");
        assert(gunFireEvents.size() == 1);
        assert(gunFireEvents.front()->actionType == 0);
        assert(gunFireEvents.front()->keyFramePercent == 50);
        assert(gunFireEvents.front()->attackId == -1);
        assert(gunFireEvents.front()->soundMapIds ==
               std::vector<std::int16_t>{18});
        usm::game::LevelEnemyRuntime gunAttackRuntime;
        assert(gunAttackRuntime.initialize(bootstrap));
        usm::game::CinematicThread enableGunThread;
        enableGunThread.objectId = 10344;
        assert(gunAttackRuntime.applyCinematicCommand(
            bootstrap, enableGunThread,
            usm::game::CinematicCommand{0, -1, "SetVisible", {}}));
        assert(gunAttackRuntime.applyCinematicCommand(
            bootstrap, enableGunThread,
            usm::game::CinematicCommand{0, -1, "EnableAI", {}}));
        const auto* attackingGunThug = gunAttackRuntime.find(10344);
        assert(attackingGunThug != nullptr);
        const auto* gunFireClip =
            bootstrap.enemyArchetypes()
                [attackingGunThug->asset->archetypeIndex]
                    .animationBank.findClip("idle_shoot_left_idle");
        assert(gunFireClip != nullptr);
        const usm::assets::Vector3 gunVictim{
            attackingGunThug->position.x + 1000.0F,
            attackingGunThug->position.y,
            attackingGunThug->position.z};
        gunAttackRuntime.updateGameplay(
            gunFireClip->durationMilliseconds() / 2, gunVictim);
        assert(gunAttackRuntime.find(10344)->activeAnimation ==
               "idle_shoot_left_idle");
        assert(gunAttackRuntime.gunLines().size() == 1);
        assert(gunAttackRuntime.gunLines().front().sourceObjectId == 10344);
        assert(gunAttackRuntime.gunLines().front().damage == 30.0F);
        const auto gunCues = gunAttackRuntime.consumeSoundCues();
        assert(gunCues.size() == 1);
        assert(gunCues.front().sourceObjectId == 10344);
        gunAttackRuntime.updateGameplay(700, gunVictim);
        const auto gunHits = gunAttackRuntime.consumePlayerHits();
        assert(gunHits.size() == 1);
        assert(gunHits.front().sourceObjectId == 10344);
        assert(gunHits.front().attackId == -1);
        assert(gunHits.front().damage == 30.0F);
        assert(gunAttackRuntime.gunLines().empty());
        assert(hammerEnemy != bootstrap.enemies().end());
        assert(hammerEnemy->enemyTypeId == 5);
        assert(hammerEnemy->health == 1000.0F);
        assert(hammerEnemy->initialAnimation == "idle");
        assert(sandmanBoss != bootstrap.enemies().end());
        assert(sandmanBoss->enemyTypeId == 16);
        assert(sandmanBoss->health == 2500.0F);
        assert(sandmanBoss->initialAnimation == "idle");
        usm::game::LevelEnemyRuntime chaseRuntime;
        assert(chaseRuntime.initialize(bootstrap));
        const auto* chasingKnife = chaseRuntime.find(394);
        assert(chasingKnife != nullptr);
        const usm::assets::Vector3 chaseTarget{
            chasingKnife->position.x + 1000.0F, chasingKnife->position.y,
            chasingKnife->position.z};
        const float chaseStartX = chasingKnife->position.x;
        chaseRuntime.updateGameplay(100, chaseTarget);
        chasingKnife = chaseRuntime.find(394);
        assert(chasingKnife->playerDetected);
        assert(chasingKnife->behavior ==
               usm::game::EnemyBehaviorState::Chasing);
        assert(chasingKnife->activeAnimation == "run");
        assert(std::abs(chasingKnife->position.x - chaseStartX - 30.0F) <
               0.01F);
        const usm::assets::Vector3 meleeTarget{
            chasingKnife->position.x + 100.0F, chasingKnife->position.y,
            chasingKnife->position.z};
        chaseRuntime.updateGameplay(16, meleeTarget);
        chasingKnife = chaseRuntime.find(394);
        assert(chasingKnife->behavior ==
               usm::game::EnemyBehaviorState::AttackRange);
        assert(chasingKnife->activeAnimation == "idle_knife_at_idle");
        usm::game::LevelEnemyRuntime enemyAttackRuntime;
        assert(enemyAttackRuntime.initialize(bootstrap));
        const auto* attackingKnife = enemyAttackRuntime.find(394);
        assert(attackingKnife != nullptr);
        const auto* knifeAttackClip =
            bootstrap.enemyArchetypes()[attackingKnife->asset->archetypeIndex]
                .animationBank.findClip("idle_knife_at_idle");
        assert(knifeAttackClip != nullptr);
        const std::uint32_t firstKnifeImpact =
            knifeAttackClip->durationMilliseconds() * 45U / 100U;
        const std::uint32_t secondKnifeImpact =
            knifeAttackClip->durationMilliseconds() * 75U / 100U;
        assert(firstKnifeImpact > 0);
        assert(secondKnifeImpact > firstKnifeImpact);
        const usm::assets::Vector3 knifeVictim{
            attackingKnife->position.x + 100.0F,
            attackingKnife->position.y,
            attackingKnife->position.z};
        enemyAttackRuntime.updateGameplay(firstKnifeImpact, knifeVictim);
        auto enemyHits = enemyAttackRuntime.consumePlayerHits();
        const auto firstKnifeHit = std::find_if(
            enemyHits.begin(), enemyHits.end(),
            [](const usm::game::EnemyPlayerHit& hit) {
                return hit.sourceObjectId == 394;
            });
        assert(firstKnifeHit != enemyHits.end());
        assert(firstKnifeHit->attackId == 6);
        assert(firstKnifeHit->damage == 25.0F);
        auto enemySoundCues = enemyAttackRuntime.consumeSoundCues();
        assert(enemySoundCues.size() == 1);
        assert(enemySoundCues.front().sourceObjectId == 394);
        assert(enemySoundCues.front().voxSoundId == 178);
        enemyAttackRuntime.updateGameplay(secondKnifeImpact - firstKnifeImpact,
                                          knifeVictim);
        enemyHits = enemyAttackRuntime.consumePlayerHits();
        const auto secondKnifeHit = std::find_if(
            enemyHits.begin(), enemyHits.end(),
            [](const usm::game::EnemyPlayerHit& hit) {
                return hit.sourceObjectId == 394;
            });
        assert(secondKnifeHit != enemyHits.end());
        assert(secondKnifeHit->attackId == 6);
        assert(secondKnifeHit->damage == 25.0F);
        enemySoundCues = enemyAttackRuntime.consumeSoundCues();
        assert(enemySoundCues.size() == 1);
        assert(enemySoundCues.front().voxSoundId == 178);
        usm::game::LevelEnemyRuntime damageRuntime;
        assert(damageRuntime.initialize(bootstrap));
        const auto* damageTarget = damageRuntime.find(394);
        assert(damageTarget != nullptr);
        const usm::assets::Vector3 damagePosition = damageTarget->position;
        const usm::assets::Vector3 attackPosition{
            damagePosition.x - 100.0F, damagePosition.y, damagePosition.z};
        assert(!damageRuntime.applyPlayerMeleeHit(
            attackPosition, {-1.0F, 0.0F, 0.0F}, 200.0F, 100.0F));
        const auto firstHit = damageRuntime.applyPlayerMeleeHit(
            attackPosition, {1.0F, 0.0F, 0.0F}, 200.0F, 100.0F);
        assert(firstHit && *firstHit == 394);
        assert(damageRuntime.find(394)->health == 400.0F);
        assert(damageRuntime.find(394)->behavior ==
               usm::game::EnemyBehaviorState::Hurt);
        assert(damageRuntime.find(394)->activeAnimation == "idle_hurt_idle");
        assert(!damageRuntime.find(394)->animationLoops);
        enemySoundCues = damageRuntime.consumeSoundCues();
        assert(enemySoundCues.size() == 1);
        assert(enemySoundCues.front().voxSoundId == 185);
        for (int hit = 0; hit < 4; ++hit) {
            assert(damageRuntime.applyPlayerMeleeHit(
                attackPosition, {1.0F, 0.0F, 0.0F}, 200.0F, 100.0F));
        }
        damageTarget = damageRuntime.find(394);
        assert(damageTarget->health == 0.0F);
        assert(damageTarget->behavior == usm::game::EnemyBehaviorState::Dead);
        assert(damageTarget->activeAnimation == "idle_onground");
        assert(!damageTarget->animationLoops);
        enemySoundCues = damageRuntime.consumeSoundCues();
        assert(enemySoundCues.size() == 4);
        assert(enemySoundCues[0].voxSoundId == 186);
        assert(enemySoundCues[1].voxSoundId == 187);
        assert(enemySoundCues[2].voxSoundId == 185);
        assert(enemySoundCues[3].voxSoundId == 188);
        usm::game::LevelCollision levelCollision;
        assert(levelCollision.build(bootstrap.rooms()));
        assert(levelCollision.triangleCount() > 100);
        float initialGroundHeight = 0.0F;
        assert(levelCollision.groundHeight(bootstrap.player().position,
                                           100.0F, 500.0F,
                                           initialGroundHeight));
        usm::game::GameplayPlayer groundedPlayer;
        assert(groundedPlayer.initialize(bootstrap.player(), &levelCollision));
        assert(groundedPlayer.maximumHealth() == bootstrap.player().health);
        assert(groundedPlayer.applyDamage(25.0F));
        assert(groundedPlayer.health() ==
               groundedPlayer.maximumHealth() - 25.0F);
        assert(groundedPlayer.applyDamage(groundedPlayer.maximumHealth()));
        assert(groundedPlayer.dead());
        assert(!groundedPlayer.requestPunch());
        usm::game::GameplayPlayer cinematicDamagePlayer;
        assert(cinematicDamagePlayer.initialize(bootstrap.player(),
                                                &levelCollision));
        usm::game::CinematicThread playerDamageThread;
        playerDamageThread.objectId = bootstrap.player().objectId;
        usm::game::CinematicCommand playerDamageCommand;
        playerDamageCommand.name = "GetDamage";
        playerDamageCommand.attributes.push_back(
            {"float", "DamageValue", "200.000000"});
        assert(cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread, playerDamageCommand));
        assert(cinematicDamagePlayer.health() ==
               cinematicDamagePlayer.maximumHealth() - 200.0F);
        playerDamageCommand.attributes.front().value = "invalid";
        assert(!cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread, playerDamageCommand));
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
        usm::game::LevelCinematicRuntime levelCommandRuntime;
        levelCommandRuntime.bind(triggerRuntime, gameplayCamera,
                                 bootstrap.waypoints());
        const auto controlCommand = [](std::string name,
                                       std::string attributeName,
                                       std::string value) {
            usm::game::CinematicCommand command;
            command.name = std::move(name);
            command.attributes.push_back(
                {"string", std::move(attributeName), std::move(value)});
            return command;
        };
        assert(triggerRuntime.isEnabled(534));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("DisableTrigger", "^ID^Trigger", "534")));
        assert(!triggerRuntime.isEnabled(534));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("EnableTrigger", "^ID^Trigger", "534")));
        assert(triggerRuntime.isEnabled(534));
        assert(!levelCommandRuntime.applyCommand(controlCommand(
            "EnableCameraArea", "^ID^CameraArea", "10275")));
        usm::game::CinematicCommand disableCameraArea = controlCommand(
            "EnableCameraArea", "^ID^CameraArea", "10275");
        disableCameraArea.attributes.push_back(
            {"bool", "enable", "false"});
        assert(levelCommandRuntime.applyCommand(disableCameraArea));
        assert(!gameplayCamera.isAreaEnabled(10275));
        usm::game::CinematicCommand enableCameraArea = controlCommand(
            "EnableCameraArea", "^ID^CameraArea", "10275");
        enableCameraArea.attributes.push_back({"bool", "enable", "true"});
        assert(levelCommandRuntime.applyCommand(enableCameraArea));
        assert(gameplayCamera.isAreaEnabled(10275));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("SetCameraArea", "^ID^CameraArea", "10100")));
        assert(gameplayCamera.currentAreaId() == 10100);
        usm::game::CinematicCommand disableControls = controlCommand(
            "InterfaceControl", "ControlEnable", "false");
        disableControls.attributes.push_back(
            {"bool", "BlackEnable", "true"});
        assert(levelCommandRuntime.applyCommand(disableControls));
        assert(!levelCommandRuntime.controlsEnabled());
        assert(levelCommandRuntime.blackOverlayEnabled());
        disableControls.attributes.front().value = "true";
        disableControls.attributes.back().value = "false";
        assert(levelCommandRuntime.applyCommand(disableControls));
        assert(levelCommandRuntime.controlsEnabled());
        assert(!levelCommandRuntime.blackOverlayEnabled());
        assert(levelCommandRuntime.applyCommand(
            controlCommand("StartCinematic", "CinematicID", "1238")));
        const auto cinematicStarts =
            levelCommandRuntime.consumeCinematicStartRequests();
        assert(cinematicStarts.size() == 1 && cinematicStarts.front() == 1238);
        assert(levelCommandRuntime.consumeCinematicStartRequests().empty());
        usm::game::CinematicCommand startSlide = controlCommand(
            "StartSlide", "^SID^WayPoint", "429");
        startSlide.attributes.push_back(
            {"int", "^EID^WayPoint", "430"});
        assert(levelCommandRuntime.applyCommand(startSlide));
        startSlide.attributes.back().value = "999999";
        assert(!levelCommandRuntime.applyCommand(startSlide));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("LevelEnd", "GoToNext", "true")));
        assert(levelCommandRuntime.levelEnded());
        assert(levelCommandRuntime.goToNextLevel());
        assert(levelCommandRuntime.applyCommand(
            usm::game::CinematicCommand{0, -1, "GameEnd", {}}));
        assert(levelCommandRuntime.gameEnded());
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
        assert(gameplayPlayer.initialize(bootstrap.player(), nullptr,
                                         &playerStateConfigs));
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
        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.activeAnimation() == "idle_to_punch_right");
        assert(!gameplayPlayer.requestPunch());
        assert(!gameplayPlayer.consumePunchSoundFrame());
        gameplayPlayer.update({}, gameplayCameraPose, 179);
        assert(!gameplayPlayer.consumePunchImpact());
        gameplayPlayer.update({}, gameplayCameraPose, 1);
        assert(gameplayPlayer.consumePunchImpact());
        assert(!gameplayPlayer.consumePunchImpact());
        assert(!gameplayPlayer.consumePunchSoundFrame());
        gameplayPlayer.update({}, gameplayCameraPose, 119);
        assert(!gameplayPlayer.consumePunchSoundFrame());
        gameplayPlayer.update({}, gameplayCameraPose, 1);
        assert(gameplayPlayer.consumePunchSoundFrame());
        assert(!gameplayPlayer.consumePunchSoundFrame());
        gameplayPlayer.update({}, gameplayCameraPose, 33);
        assert(gameplayPlayer.activeAnimation() == "punch_right_to_idle");
        gameplayPlayer.update({}, gameplayCameraPose, 466);
        assert(gameplayPlayer.activeAnimation() == "idle_stand");
        assert(gameplayPlayer.requestPunch());

        usm::game::GameplayPlayer jumpingPlayer;
        assert(jumpingPlayer.initialize(bootstrap.player(), nullptr,
                                        &playerStateConfigs));
        const float jumpGroundHeight = jumpingPlayer.position().z;
        assert(jumpingPlayer.requestJump());
        assert(jumpingPlayer.activeStateId() == 13);
        assert(jumpingPlayer.activeAnimation() == "jump_ready_to_jump");
        assert(jumpingPlayer.airborne());
        assert(jumpingPlayer.consumeEnteredState() ==
               "k_state_jump_start");
        assert(jumpingPlayer.consumeEnteredState().empty());
        assert(!jumpingPlayer.requestJump());
        assert(!jumpingPlayer.requestPunch());
        jumpingPlayer.update({}, gameplayCameraPose, 150);
        assert(jumpingPlayer.animatedFootHeight() >
               jumpGroundHeight + 100.0F);
        assert(std::abs(jumpingPlayer.position().z - jumpGroundHeight) <
               0.001F);
        assert(std::abs(jumpingPlayer.worldTransform()[14] -
                        jumpGroundHeight) < 0.001F);
        jumpingPlayer.update({}, gameplayCameraPose, 150);
        assert(jumpingPlayer.activeStateId() == 14);
        assert(jumpingPlayer.activeAnimation() == "jump_to_fall");
        assert(jumpingPlayer.animatedFootHeight() >
               jumpGroundHeight + 300.0F);
        jumpingPlayer.update({}, gameplayCameraPose, 150);
        assert(jumpingPlayer.animatedFootHeight() >
               jumpGroundHeight + 100.0F);
        jumpingPlayer.update({}, gameplayCameraPose, 150);
        assert(!jumpingPlayer.airborne());
        assert(jumpingPlayer.activeStateId() == 16);
        assert(jumpingPlayer.activeAnimation() == "fall_to_idle");
        assert(jumpingPlayer.consumeEnteredState() == "k_state_jump_land");
        assert(std::abs(jumpingPlayer.position().z - jumpGroundHeight) <
               0.001F);
        jumpingPlayer.update({}, gameplayCameraPose, 400);
        assert(jumpingPlayer.activeStateId() == 0);
        assert(jumpingPlayer.activeAnimation() == "idle_stand");
        assert(jumpingPlayer.requestJump());

        std::array<usm::game::LevelWayPointAsset, 2>
            playerSlideWaypoints;
        playerSlideWaypoints[0].objectId = 9101;
        playerSlideWaypoints[0].position = {
            bootstrap.player().position.x,
            bootstrap.player().position.y,
            bootstrap.player().position.z + 400.0F,
        };
        playerSlideWaypoints[0].nextWaypointIds[0] = 9102;
        playerSlideWaypoints[1].objectId = 9102;
        playerSlideWaypoints[1].position = {
            bootstrap.player().position.x + 1000.0F,
            bootstrap.player().position.y,
            bootstrap.player().position.z + 400.0F,
        };
        usm::game::LevelSlideAsset playerSlideAsset;
        playerSlideAsset.objectId = 9100;
        playerSlideAsset.waypointIds = {9101, 9102};
        const std::array<usm::game::LevelSlideAsset, 1> playerSlides{
            playerSlideAsset};
        usm::game::GameplayPlayer slidingPlayer;
        assert(slidingPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs, {},
            playerSlides, playerSlideWaypoints));
        assert(slidingPlayer.requestJump());
        slidingPlayer.update({}, gameplayCameraPose, 300);
        assert(slidingPlayer.activeStateId() == 14);
        slidingPlayer.update({}, gameplayCameraPose, 1);
        assert(slidingPlayer.activeStateId() == 23);
        assert(slidingPlayer.activeAnimation() == "fall_to_slide");
        slidingPlayer.update({}, gameplayCameraPose, 401);
        assert(slidingPlayer.activeStateId() == 22);
        assert(slidingPlayer.activeAnimation() == "slide");
        assert(slidingPlayer.consumeEnteredState() == "k_state_jump_start");
        assert(slidingPlayer.consumeEnteredState() ==
               "k_state_trigger_slider_move");
        const float slideStartX = slidingPlayer.position().x;
        slidingPlayer.update({}, gameplayCameraPose, 100);
        assert(slidingPlayer.position().x > slideStartX + 60.0F);
        slidingPlayer.update({}, gameplayCameraPose, 2000);
        assert(slidingPlayer.activeStateId() == 15);

        usm::game::GameplayPlayer swingingPlayer;
        usm::game::LevelWebGrabPointAsset testGrabPoint;
        testGrabPoint.objectId = 9001;
        const auto initialFacing = jumpingPlayer.facing();
        testGrabPoint.position = {
            bootstrap.player().position.x + initialFacing.x * 400.0F,
            bootstrap.player().position.y + initialFacing.y * 400.0F,
            bootstrap.player().position.z + 500.0F,
        };
        testGrabPoint.direction = {initialFacing.y, -initialFacing.x, 0.0F};
        testGrabPoint.length = 600.0F;
        testGrabPoint.visibleLength = 2000.0F;
        testGrabPoint.exitSpeed = 0.45F;
        const std::array<usm::game::LevelWebGrabPointAsset, 1>
            testGrabPoints{testGrabPoint};
        assert(swingingPlayer.initialize(bootstrap.player(), nullptr,
                                         &playerStateConfigs,
                                         testGrabPoints));
        assert(swingingPlayer.requestJump());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_jump_start");
        swingingPlayer.update({}, gameplayCameraPose, 150);
        assert(swingingPlayer.requestWeb());
        assert(swingingPlayer.activeStateId() == 17);
        assert(swingingPlayer.activeAnimation() ==
               "jump_to_throw_web_right");
        assert(swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_web_throw");
        swingingPlayer.update({}, gameplayCameraPose, 267);
        assert(swingingPlayer.activeStateId() == 18);
        assert(swingingPlayer.activeAnimation() ==
               "swing_hang_fwd_right");
        assert(swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_hang");
        assert(swingingPlayer.releaseWeb());
        assert(swingingPlayer.activeStateId() == 19);
        assert(swingingPlayer.activeAnimation() ==
               "swing_hang_right_to_swing_idle_2");
        assert(!swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_idle");
        swingingPlayer.update({}, gameplayCameraPose, 100);
        assert(swingingPlayer.airborne());

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
        assert(bootstrap.rooms().size() == 13);
        assert(bootstrap.mainScene().linkedSceneFiles().size() == 13);
        assert(bootstrap.mainScene().linkedSceneFiles().front() ==
               "levelnew_01_0_Room1.irr");
        assert(bootstrap.mainScene().linkedSceneFiles().back() ==
               "levelnew_01_13_Room13.irr");
        assert(bootstrap.rooms()[4].name == "Room5");
        assert(!bootstrap.rooms()[4].geometry.geometries().empty());
        assert(bootstrap.rooms().back().name == "Room13");
        assert(bootstrap.rooms().back().sceneFile ==
               "levelnew_01_13_Room13.irr");
        assert(bootstrap.cameraAreas().size() == 44);
        const auto finalCameraArea = std::find_if(
            bootstrap.cameraAreas().begin(), bootstrap.cameraAreas().end(),
            [](const usm::game::CameraArea& area) {
                return area.objectId == 10100;
            });
        assert(finalCameraArea != bootstrap.cameraAreas().end());
        assert(finalCameraArea->controlPoints.front().position.x < -16000.0F);
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
        usm::game::LevelTriggerRuntime introTriggerRuntime;
        introTriggerRuntime.bind(bootstrap.triggers());
        usm::game::GameplayCamera introGameplayCamera;
        assert(introGameplayCamera.bind(
            bootstrap.cameraAreas(), bootstrap.player().initialCameraAreaId));
        usm::game::LevelCinematicRuntime introCommandRuntime;
        introCommandRuntime.bind(introTriggerRuntime, introGameplayCamera,
                                 bootstrap.waypoints());
        usm::game::CinematicPlayer introStartPlayer;
        assert(introStartPlayer.start(bootstrap.introStartScript()));
        usm::Result introStartResult = usm::Result::success();
        assert(introStartPlayer.advanceTo(
            introStartPlayer.durationMilliseconds(),
            [&introCommandRuntime, &introStartResult](
                const usm::game::CinematicThread&,
                const usm::game::CinematicCommand& command) {
                if (introStartResult) {
                    introStartResult = introCommandRuntime.applyCommand(command);
                }
            }));
        assert(introStartResult);
        assert(!introTriggerRuntime.isEnabled(1263));
        const auto introStartRequests =
            introCommandRuntime.consumeCinematicStartRequests();
        assert(introStartRequests.size() == 1);
        assert(introStartRequests.front() == 1265);
        std::size_t chainedCinematicCommandCount = 0;
        for (const auto& cinematic : bootstrap.cinematics()) {
            if (!cinematic.scriptAvailable) {
                continue;
            }
            std::uint32_t scriptDuration = 0;
            for (const auto& thread : cinematic.script.threads()) {
                for (const auto& command : thread.commands) {
                    scriptDuration =
                        std::max(scriptDuration, command.timestampMilliseconds);
                }
            }
            for (const auto& thread : cinematic.script.threads()) {
                for (const auto& command : thread.commands) {
                    if (command.name == "StartCinematic") {
                        ++chainedCinematicCommandCount;
                        assert(command.timestampMilliseconds == scriptDuration);
                    }
                    const auto commandResult =
                        introCommandRuntime.applyCommand(command);
                    if (!commandResult) {
                        std::cerr << "Level command " << command.name
                                  << " in cinematic " << cinematic.objectId
                                  << " failed: " << commandResult.message()
                                  << '\n';
                        return 1;
                    }
                }
            }
        }
        assert(chainedCinematicCommandCount == 7);
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
        assert(introSounds.loadedEventCount() == 19);
        assert(introSounds.unresolvedEvents().empty());
        std::size_t playedSoundCount = 0;
        assert(introSounds.dispatch(
            bootstrap.introScript().threads().front().commands.back(),
            [&playedSoundCount](std::string_view,
                                const usm::audio::PcmAudio&, bool) {
                ++playedSoundCount;
                return usm::Result::success();
            }));
        assert(playedSoundCount == 1);

        std::vector<const usm::game::CinematicScript*> gameplaySoundScripts;
        const usm::game::CinematicCommand* gameplayStopCommand = nullptr;
        for (const auto& cinematic : bootstrap.cinematics()) {
            if (!cinematic.scriptAvailable) {
                continue;
            }
            gameplaySoundScripts.push_back(&cinematic.script);
            for (const auto& thread : cinematic.script.threads()) {
                for (const auto& command : thread.commands) {
                    const auto* stop2D = command.findAttribute("Stop2D");
                    const auto* stop = command.findAttribute("Stop");
                    if (command.name == "SoundControl" &&
                        ((stop2D != nullptr && stop2D->value == "true") ||
                         (stop != nullptr && stop->value == "true"))) {
                        gameplayStopCommand = &command;
                    }
                }
            }
        }
        usm::audio::CinematicSoundBank gameplaySounds;
        assert(gameplaySounds.preload(gameplaySoundScripts, soundCatalog));
        assert(gameplaySounds.loadedEventCount() == 60);
        if (!gameplaySounds.unresolvedEvents().empty()) {
            std::cerr << "Unresolved gameplay cinematic sound:";
            for (const auto& event : gameplaySounds.unresolvedEvents()) {
                std::cerr << ' ' << event;
            }
            std::cerr << '\n';
            return 1;
        }
        assert(gameplayStopCommand != nullptr);
        std::size_t stoppedSoundCount = 0;
        assert(gameplaySounds.dispatch(
            *gameplayStopCommand,
            [](std::string_view, const usm::audio::PcmAudio&, bool) {
                return usm::Result::success();
            },
            [&stoppedSoundCount](std::string_view eventName) {
                assert(!eventName.empty());
                ++stoppedSoundCount;
                return usm::Result::success();
            }));
        assert(stoppedSoundCount == 1);

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
