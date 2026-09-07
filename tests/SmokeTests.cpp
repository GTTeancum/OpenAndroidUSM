#include "assets/BresFile.hpp"
#include "assets/BtexTexture.hpp"
#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "assets/ColladaSkinning.hpp"
#include "assets/DdsAtcTexture.hpp"
#include "assets/IrrScene.hpp"
#include "assets/SpriteAtlas.hpp"
#include "assets/TgaTexture.hpp"
#include "audio/OggAudio.hpp"
#include "audio/GameAudioMix.hpp"
#include "audio/EnemyBehaviorSoundBank.hpp"
#include "audio/LevelMusicBank.hpp"
#include "audio/PlayerStateSoundBank.hpp"
#include "audio/CinematicSoundBank.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "audio/VoxSoundTable.hpp"
#include "audio/SpatialSound.hpp"
#include "core/Result.hpp"
#include "diagnostics/AutoplayHarness.hpp"
#include "filesystem/GbmpArchive.hpp"
#include "game/EffectBillboard.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/LevelSlideRuntime.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/GameplayCinematicScheduler.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelCheckPointRuntime.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelDamageRuntime.hpp"
#include "game/LevelDeathRuntime.hpp"
#include "game/DeathConfirmationRuntime.hpp"
#include "game/ExitMenuRuntime.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/LevelEffectRuntime.hpp"
#include "game/LevelDropRuntime.hpp"
#include "game/LevelHintRuntime.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/LevelMusicRuntime.hpp"
#include "game/NativeRandomizer.hpp"
#include "game/LevelObjectRuntime.hpp"
#include "game/LevelRestoreRuntime.hpp"
#include "game/EnemyRangeAttackConfig.hpp"
#include "game/PlayerHudHealthState.hpp"
#include "game/PlayerPhysicsConstants.hpp"
#include "game/PlayerStateConfig.hpp"
#include "game/QuickTimeEventRuntime.hpp"
#include "game/QuickTimeActionRuntime.hpp"
#include "game/LevelTriggerRuntime.hpp"
#include "game/LevelTriggerSoundRuntime.hpp"
#include "game/WebGrabPointRuntime.hpp"
#include "game/WebLineGeometry.hpp"
#include "game/WebSwingRuntime.hpp"
#include "game/CinematicScript.hpp"
#include "game/CinematicPlayer.hpp"
#include "game/CinematicUiRuntime.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <algorithm>
#include <cassert>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <numeric>
#include <set>
#include <string>
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

    {
        // irr::os::Randomizer::rand (0x0043ae48) and the global random()
        // wrappers at 0x003730b0/0x003730cc. Lock the exact shipped sequence
        // before combat scheduling is allowed to depend on it.
        usm::game::NativeRandomizer randomizer;
        assert(randomizer.state() == 0x0f0f0f0f);
        constexpr std::array<std::int32_t, 6> expected{
            632802407, 1669591634, 1237959964,
            1548764745, 139693087, 2539051};
        for (const std::int32_t value : expected) {
            assert(randomizer.next() == value);
        }
        usm::game::NativeRandomizer bounded;
        assert(bounded.bounded(0) == 0);
        assert(bounded.state() == usm::game::NativeRandomizer::kInitialSeed);
        assert(bounded.bounded(100) == 7);
        usm::game::NativeRandomizer ranged;
        assert(ranged.range(1000, 2000) == 1407);
        const std::int32_t stateBeforeInvalidRanges = ranged.state();
        assert(ranged.range(8, 8) == 8);
        assert(ranged.range(9, 8) == 0);
        assert(ranged.state() == stateBeforeInvalidRanges);
    }

    {
        usm::game::WebLineGeometry geometry;
        assert(usm::game::buildWebLineGeometry(
            {0.0F, 0.0F, 0.0F}, {250.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, -1.0F}, geometry));
        // CTexLineSceneNode::setLineSegment's short branch emits one final
        // fractional quad after the two complete 100 cm repeats.
        assert(geometry.vertices.size() == 8);
        assert(geometry.indices.size() == 18);
        assert(std::abs(geometry.vertices[0].position.y + 10.0F) < 1e-4F);
        assert(std::abs(geometry.vertices[1].position.y - 10.0F) < 1e-4F);
        assert(std::abs(geometry.vertices[0].textureV - 0.6F) < 1e-4F);
        assert(std::abs(geometry.vertices[1].textureV - 0.4F) < 1e-4F);
        assert(std::abs(geometry.vertices[6].position.x - 250.0F) < 1e-4F);
        assert(std::abs(geometry.vertices[6].textureU - 2.5F) < 1e-4F);
        const std::array<std::uint16_t, 6> firstIndices{0, 1, 2, 2, 1, 3};
        assert(std::equal(firstIndices.begin(), firstIndices.end(),
                          geometry.indices.begin()));

        assert(usm::game::buildWebLineGeometry(
            {0.0F, 0.0F, 0.0F}, {3500.0F, 0.0F, 0.0F},
            {0.0F, 0.0F, -1.0F}, geometry));
        assert(geometry.vertices.size() == 62);
        assert(geometry.indices.size() == 180);
        assert(std::abs(geometry.vertices.back().position.x - 3500.0F) <
               1e-4F);
        assert(std::abs(geometry.vertices.back().textureU - 30.0F) < 1e-4F);
    }
    assert(static_cast<bool>(success));

    const auto failure = usm::Result::failure("expected failure");
    assert(!static_cast<bool>(failure));
    assert(failure.message() == "expected failure");

    {
        usm::game::CameraPose camera;
        camera.position = {};
        camera.target = {0.0F, 1.0F, 0.0F};
        camera.verticalFieldOfViewDegrees = 90.0F;
        camera.nearPlane = 1.0F;
        camera.farPlane = 100.0F;
        assert(usm::game::isPointInScreen(camera, 2.0F, {19.0F, 10.0F, 9.0F}));
        for (const auto point : {usm::assets::Vector3{21.0F, 10.0F, 0.0F},
                 {-21.0F, 10.0F, 0.0F}, {0.0F, 10.0F, 11.0F},
                 {0.0F, 10.0F, -11.0F}, {0.0F, 0.5F, 0.0F},
                 {0.0F, 101.0F, 0.0F}, {0.0F, -10.0F, 0.0F}}) {
            assert(!usm::game::isPointInScreen(camera, 2.0F, point));
        }
        assert(!usm::game::isPointInScreen(camera, 1.0F, {19.0F, 10.0F, 0.0F}));
    }
    assert(std::abs(usm::audio::GameAudioMix::DefaultMusicGroupVolume -
                    0.9F) < 0.0001F);
    assert(std::abs(
               usm::audio::GameAudioMix::DefaultSoundEffectsGroupVolume -
               0.9F) < 0.0001F);
    assert(std::abs(usm::audio::GameAudioMix::WindowsMusicOutputTrim -
                    0.5011872F) < 0.0001F);
    assert(std::abs(usm::audio::GameAudioMix::DefaultMusicOutputVolume -
                    0.4510685F) < 0.0001F);

    const std::filesystem::path autoplayTestRoot =
        std::filesystem::temp_directory_path() /
        "openandroidusm-autoplay-smoke";
    std::error_code autoplayFilesystemError;
    std::filesystem::remove_all(autoplayTestRoot,
                                autoplayFilesystemError);
    std::filesystem::create_directories(autoplayTestRoot,
                                        autoplayFilesystemError);
    assert(!autoplayFilesystemError);
    const std::filesystem::path autoplayScript =
        autoplayTestRoot / "smoke.usmauto";
    {
        std::ofstream stream(autoplayScript);
        stream << "fixed_step_ms 25\n"
                  "sample_interval_ms 50\n"
                  "capture_interval_ms 0\n"
                  "max_time_ms 1000\n"
                  "start_time_ms 100\n"
                  "render_size 320 180\n"
                  "wait_gameplay 50\n"
                  "move_input 25 0.25 0.75\n"
                  "move_until_wall 4 5 6 50\n"
                  "teleport 1 2 3 0 1 0\n"
                  "move_until_cinematic 4 5 6 974 50\n"
                  "cross_trigger 900 901 100\n"
                  "wait_cinematic_started 50 901\n"
                  "capture smoke-frame\n"
                  "finish\n";
    }
    usm::diagnostics::AutoplayHarness autoplayHarness;
    assert(autoplayHarness.initialize(autoplayScript,
                                      autoplayTestRoot / "output"));
    assert(autoplayHarness.fixedStepMilliseconds() == 25);
    assert(autoplayHarness.startTimeMilliseconds() == 100);
    {
        const auto tapScript = autoplayTestRoot / "qte-input.usmauto";
        {
            std::ofstream stream(tapScript);
            stream << "set_auto_qte 0\nqte_tap\nwait 50\nset_auto_qte 1\nfinish\n";
        }
        usm::diagnostics::AutoplayHarness tapHarness;
        assert(tapHarness.initialize(tapScript, autoplayTestRoot / "qte-input"));
        usm::diagnostics::AutoplaySnapshot snap;
        snap.quickTimeEventActive = true;
        assert(!tapHarness.update(snap).quickTimeEventPressed);
        snap.realTimeMilliseconds += 50;
        assert(tapHarness.update(snap).quickTimeEventPressed);
        snap.realTimeMilliseconds += 50;
        assert(!tapHarness.update(snap).quickTimeEventPressed);
        snap.realTimeMilliseconds += 50;
        assert(!tapHarness.update(snap).quickTimeEventPressed);
        snap.realTimeMilliseconds += 50;
        assert(tapHarness.update(snap).quickTimeEventPressed);
        snap.realTimeMilliseconds += 50;
        (void)tapHarness.update(snap);
        assert(tapHarness.complete() && !tapHarness.failed());
    }
    assert(autoplayHarness.renderWidth() == 320);
    assert(autoplayHarness.renderHeight() == 180);
    usm::game::LevelTriggerAsset autoplayTrigger;
    autoplayTrigger.objectId = 900;
    autoplayTrigger.position = {10.0F, 20.0F, 30.0F};
    autoplayTrigger.sizes = {10.0F, 20.0F, 30.0F};
    autoplayTrigger.enabled = true;
    const std::array autoplayTriggers{autoplayTrigger};
    autoplayHarness.bindTriggers(autoplayTriggers);
    usm::diagnostics::AutoplaySnapshot autoplaySnapshot;
    autoplaySnapshot.gameplayActive = true;
    autoplaySnapshot.controlsEnabled = true;
    autoplaySnapshot.playerHealth = 100.0F;
    autoplaySnapshot.tutorialVisible = true;
    autoplaySnapshot.realTimeMilliseconds = 100;
    autoplaySnapshot.controlsEnabled = false;
    assert(!autoplayHarness.update(autoplaySnapshot).quickTimeEventPressed);
    autoplaySnapshot.realTimeMilliseconds = 125;
    autoplaySnapshot.controlsEnabled = true;
    const auto motionInput = autoplayHarness.update(autoplaySnapshot);
    assert(motionInput.motion.right == 0.25F);
    assert(motionInput.motion.forward == 0.75F);
    autoplaySnapshot.realTimeMilliseconds = 150;
    (void)autoplayHarness.update(autoplaySnapshot);
    autoplaySnapshot.realTimeMilliseconds = 175;
    const auto wallSeekInput = autoplayHarness.update(autoplaySnapshot);
    assert(wallSeekInput.motion.right != 0.0F ||
           wallSeekInput.motion.forward != 0.0F);
    autoplaySnapshot.realTimeMilliseconds = 200;
    autoplaySnapshot.playerOnWall = true;
    (void)autoplayHarness.update(autoplaySnapshot);
    autoplaySnapshot.playerOnWall = false;
    autoplaySnapshot.realTimeMilliseconds = 225;
    const auto teleportInput = autoplayHarness.update(autoplaySnapshot);
    assert(teleportInput.teleport.has_value());
    assert(teleportInput.teleport->position.x == 1.0F);
    autoplaySnapshot.realTimeMilliseconds = 250;
    // Instantaneous cinematics may start and finish between snapshots; the
    // movement driver must honor the notification instead of requiring the
    // cinematic to remain active for another frame.
    autoplayHarness.notifyCinematicStarted(974);
    (void)autoplayHarness.update(autoplaySnapshot);
    autoplaySnapshot.realTimeMilliseconds = 275;
    const auto triggerInsideInput = autoplayHarness.update(autoplaySnapshot);
    assert(triggerInsideInput.teleport.has_value());
    assert(triggerInsideInput.teleport->position.x == 10.0F);
    assert(triggerInsideInput.teleport->position.y == 20.0F);
    assert(triggerInsideInput.teleport->position.z ==
           30.0F - usm::game::kPlayerCollisionHalfHeightCentimeters);
    autoplaySnapshot.realTimeMilliseconds = 300;
    const auto triggerOutsideInput = autoplayHarness.update(autoplaySnapshot);
    assert(triggerOutsideInput.teleport.has_value());
    assert(triggerOutsideInput.teleport->position.x > 10.0F);
    autoplayHarness.notifyCinematicStarted(901);
    autoplaySnapshot.realTimeMilliseconds = 325;
    (void)autoplayHarness.update(autoplaySnapshot);
    autoplaySnapshot.realTimeMilliseconds = 350;
    (void)autoplayHarness.update(autoplaySnapshot);
    autoplaySnapshot.realTimeMilliseconds = 375;
    autoplaySnapshot.controlsEnabled = false;
    const auto captureInput = autoplayHarness.update(autoplaySnapshot);
    assert(captureInput.captureLabels.size() == 1);
    assert(captureInput.captureLabels.front() == "smoke-frame");
    assert(!captureInput.quickTimeEventPressed);
    autoplaySnapshot.realTimeMilliseconds = 400;
    (void)autoplayHarness.update(autoplaySnapshot);
    assert(autoplayHarness.complete());
    autoplayHarness.finish(true, "smoke complete");
    assert(std::filesystem::exists(autoplayTestRoot / "output" /
                                   "summary.txt"));

    const std::filesystem::path controlsAutoplayScript =
        autoplayTestRoot / "controls.usmauto";
    {
        std::ofstream stream(controlsAutoplayScript);
        stream << "capture_interval_ms 0\n"
                  "max_time_ms 100\n"
                  "wait_controls 50\n"
                  "finish\n";
    }
    usm::diagnostics::AutoplayHarness controlsAutoplayHarness;
    assert(controlsAutoplayHarness.initialize(
        controlsAutoplayScript, autoplayTestRoot / "controls-output"));
    usm::diagnostics::AutoplaySnapshot modalTutorialSnapshot;
    modalTutorialSnapshot.gameplayActive = true;
    modalTutorialSnapshot.controlsEnabled = false;
    modalTutorialSnapshot.tutorialVisible = true;
    assert(controlsAutoplayHarness.update(modalTutorialSnapshot)
               .quickTimeEventPressed);

    const auto enemyMeleeWaitScript =
        autoplayTestRoot / "enemy-melee-wait.usmauto";
    {
        std::ofstream stream(enemyMeleeWaitScript);
        stream << "capture_interval_ms 0\nmax_time_ms 1000\n"
                  "wait_enemy_melee_attack 500 394\nspider_sense\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness enemyMeleeWaitHarness;
    assert(enemyMeleeWaitHarness.initialize(
        enemyMeleeWaitScript, autoplayTestRoot / "enemy-melee-wait-output"));
    usm::game::LevelEnemyAsset enemyMeleeWaitAsset;
    enemyMeleeWaitAsset.objectId = 394;
    usm::game::LevelEnemyState enemyMeleeWaitState;
    enemyMeleeWaitState.asset = &enemyMeleeWaitAsset;
    enemyMeleeWaitState.visible = true;
    enemyMeleeWaitState.aiEnabled = true;
    enemyMeleeWaitState.health = 500.0F;
    usm::diagnostics::AutoplaySnapshot enemyMeleeWaitSnapshot;
    enemyMeleeWaitSnapshot.gameplayActive = true;
    enemyMeleeWaitSnapshot.controlsEnabled = true;
    enemyMeleeWaitSnapshot.enemies = {&enemyMeleeWaitState, 1};
    assert(!enemyMeleeWaitHarness.update(enemyMeleeWaitSnapshot)
                .spiderSensePressed);
    enemyMeleeWaitState.meleeAttackRegistered = true;
    enemyMeleeWaitState.meleeAttackActive = true;
    enemyMeleeWaitSnapshot.realTimeMilliseconds = 25;
    assert(!enemyMeleeWaitHarness.update(enemyMeleeWaitSnapshot)
                .spiderSensePressed);
    enemyMeleeWaitSnapshot.realTimeMilliseconds = 50;
    assert(enemyMeleeWaitHarness.update(enemyMeleeWaitSnapshot)
               .spiderSensePressed);
    enemyMeleeWaitSnapshot.realTimeMilliseconds = 75;
    (void)enemyMeleeWaitHarness.update(enemyMeleeWaitSnapshot);
    assert(enemyMeleeWaitHarness.complete() &&
           !enemyMeleeWaitHarness.failed());

    const auto damageJumpAutoplayScript =
        autoplayTestRoot / "damage-jump.usmauto";
    {
        std::ofstream stream(damageJumpAutoplayScript);
        stream << "capture_interval_ms 0\nmax_time_ms 100\n"
                  "jump 0 1\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness damageJumpHarness;
    assert(damageJumpHarness.initialize(
        damageJumpAutoplayScript, autoplayTestRoot / "damage-jump-output"));
    usm::diagnostics::AutoplaySnapshot damageJumpSnapshot;
    damageJumpSnapshot.gameplayActive = true;
    damageJumpSnapshot.controlsEnabled = true;
    damageJumpSnapshot.realTimeMilliseconds = 1;
    damageJumpSnapshot.playerAnimation = "idle_to_hurt_to_idle";
    assert(!damageJumpHarness.update(damageJumpSnapshot).jumpPressed);
    assert(!damageJumpHarness.complete());
    damageJumpSnapshot.realTimeMilliseconds = 2;
    damageJumpSnapshot.playerAnimation = "idle_stand";
    assert(damageJumpHarness.update(damageJumpSnapshot).jumpPressed);
    damageJumpSnapshot.realTimeMilliseconds = 3;
    (void)damageJumpHarness.update(damageJumpSnapshot);
    assert(damageJumpHarness.complete() && !damageJumpHarness.failed());

    const auto heldJumpTransitionScript =
        autoplayTestRoot / "held-jump-transition.usmauto";
    {
        std::ofstream stream(heldJumpTransitionScript);
        stream << "capture_interval_ms 0\nmax_time_ms 100\n"
                  "jump_attack_when_ready 50\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness heldJumpTransitionHarness;
    assert(heldJumpTransitionHarness.initialize(
        heldJumpTransitionScript,
        autoplayTestRoot / "held-jump-transition-output"));
    usm::diagnostics::AutoplaySnapshot heldJumpTransitionSnapshot;
    heldJumpTransitionSnapshot.gameplayActive = true;
    heldJumpTransitionSnapshot.controlsEnabled = true;
    heldJumpTransitionSnapshot.playerJumpAttackTransitionReady = true;
    const auto heldJumpTransitionInput =
        heldJumpTransitionHarness.update(heldJumpTransitionSnapshot);
    assert(heldJumpTransitionInput.jumpHeld);
    assert(!heldJumpTransitionInput.jumpPressed);
    assert(!heldJumpTransitionInput.jumpReleased);

    const auto wallCombatAutoplayScript = autoplayTestRoot / "wall-combat.usmauto";
    {
        std::ofstream stream(wallCombatAutoplayScript);
        stream << "capture_interval_ms 0\nmax_time_ms 1000\n"
                  "attack 900 230 42\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness wallCombatHarness;
    assert(wallCombatHarness.initialize(wallCombatAutoplayScript,
                                        autoplayTestRoot / "wall-combat-output"));
    usm::game::LevelEnemyAsset wallCombatAsset;
    wallCombatAsset.objectId = 42;
    usm::game::LevelEnemyState wallCombatEnemy;
    wallCombatEnemy.asset = &wallCombatAsset;
    wallCombatEnemy.visible = true;
    wallCombatEnemy.health = 800.0F;
    wallCombatEnemy.onWall = true;
    wallCombatEnemy.position = {300.0F, 0.0F, 300.0F};
    usm::diagnostics::AutoplaySnapshot wallCombatSnapshot;
    wallCombatSnapshot.gameplayActive = true;
    wallCombatSnapshot.controlsEnabled = true;
    wallCombatSnapshot.playerOnWall = true;
    wallCombatSnapshot.playerStateId = 1;
    wallCombatSnapshot.playerFacing = {0.0F, 1.0F, 0.0F};
    wallCombatSnapshot.enemies = {&wallCombatEnemy, 1};
    const auto wallApproach = wallCombatHarness.update(wallCombatSnapshot);
    assert(wallApproach.motion.right > 0.7F);
    assert(wallApproach.motion.forward > 0.7F);
    assert(!wallApproach.punchPressed);
    wallCombatEnemy.position = {150.0F, 0.0F, 50.0F};
    wallCombatSnapshot.realTimeMilliseconds = 50;
    wallCombatSnapshot.playerStateId = 51;
    assert(!wallCombatHarness.update(wallCombatSnapshot).punchPressed);
    wallCombatSnapshot.realTimeMilliseconds = 100;
    wallCombatSnapshot.playerStateId = 1;
    wallCombatSnapshot.playerPunchTransitionReady = true;
    const auto wallPunch = wallCombatHarness.update(wallCombatSnapshot);
    assert(wallPunch.punchPressed);
    assert(wallPunch.motion.right == 0.0F && wallPunch.motion.forward == 0.0F);
    wallCombatEnemy.health = 0.0F;
    wallCombatSnapshot.realTimeMilliseconds = 150;
    (void)wallCombatHarness.update(wallCombatSnapshot);
    wallCombatSnapshot.realTimeMilliseconds = 200;
    (void)wallCombatHarness.update(wallCombatSnapshot);
    assert(wallCombatHarness.complete() && !wallCombatHarness.failed());

    const auto obstructedCombatScript =
        autoplayTestRoot / "obstructed-combat.usmauto";
    {
        std::ofstream stream(obstructedCombatScript);
        stream << "capture_interval_ms 0\nmax_time_ms 2000\n"
                  "attack 1500 180 42\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness obstructedCombatHarness;
    assert(obstructedCombatHarness.initialize(
        obstructedCombatScript,
        autoplayTestRoot / "obstructed-combat-output"));
    auto obstructedCombatSnapshot = wallCombatSnapshot;
    obstructedCombatSnapshot.realTimeMilliseconds = 0;
    obstructedCombatSnapshot.playerOnWall = false;
    obstructedCombatSnapshot.playerStateId = 0;
    obstructedCombatSnapshot.playerPosition = {};
    wallCombatEnemy.onWall = false;
    wallCombatEnemy.health = 500.0F;
    wallCombatEnemy.position = {300.0F, 0.0F, 0.0F};
    assert(!obstructedCombatHarness.update(obstructedCombatSnapshot)
                .webPressed);
    obstructedCombatSnapshot.realTimeMilliseconds = 600;
    const auto obstructedJumpRecovery =
        obstructedCombatHarness.update(obstructedCombatSnapshot);
    assert(obstructedJumpRecovery.jumpPressed &&
           obstructedJumpRecovery.jumpHeld);
    assert(!obstructedJumpRecovery.webPressed);
    obstructedCombatSnapshot.realTimeMilliseconds = 1200;
    const auto obstructedWebRecovery =
        obstructedCombatHarness.update(obstructedCombatSnapshot);
    assert(obstructedWebRecovery.webPressed && obstructedWebRecovery.webHeld);
    assert(!obstructedWebRecovery.jumpPressed);
    wallCombatEnemy.health = 0.0F;
    obstructedCombatSnapshot.realTimeMilliseconds = 1250;
    (void)obstructedCombatHarness.update(obstructedCombatSnapshot);
    obstructedCombatSnapshot.realTimeMilliseconds = 1300;
    (void)obstructedCombatHarness.update(obstructedCombatSnapshot);
    assert(obstructedCombatHarness.complete() &&
           !obstructedCombatHarness.failed());

    const auto wallClimbScript = autoplayTestRoot / "wall-climb-input.usmauto";
    {
        std::ofstream stream(wallClimbScript);
        stream << "capture_interval_ms 0\nmax_time_ms 1000\n"
                  "climb_to 300 0 300 20 900\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness wallClimbHarness;
    assert(wallClimbHarness.initialize(wallClimbScript,
                                       autoplayTestRoot / "wall-climb-output"));
    auto wallClimbSnapshot = wallCombatSnapshot;
    wallClimbSnapshot.realTimeMilliseconds = 0;
    wallClimbSnapshot.playerStateId = 51;
    auto wallClimbInput = wallClimbHarness.update(wallClimbSnapshot);
    assert(wallClimbInput.motion.right == 0.0F &&
           wallClimbInput.motion.forward == 0.0F);
    wallClimbSnapshot.realTimeMilliseconds = 50;
    wallClimbSnapshot.playerStateId = 1;
    wallClimbInput = wallClimbHarness.update(wallClimbSnapshot);
    assert(wallClimbInput.motion.right > 0.7F &&
           wallClimbInput.motion.forward > 0.7F);
    wallClimbSnapshot.realTimeMilliseconds = 100;
    wallClimbSnapshot.playerPosition = {300.0F, 0.0F, 0.0F};
    wallClimbInput = wallClimbHarness.update(wallClimbSnapshot);
    assert(!wallClimbHarness.complete() && wallClimbInput.motion.forward == 1.0F);
    wallClimbSnapshot.realTimeMilliseconds = 150;
    wallClimbSnapshot.playerPosition.z = 300.0F;
    (void)wallClimbHarness.update(wallClimbSnapshot);
    wallClimbSnapshot.realTimeMilliseconds = 200;
    (void)wallClimbHarness.update(wallClimbSnapshot);
    assert(wallClimbHarness.complete() && !wallClimbHarness.failed());

    const auto stateTargetScript =
        autoplayTestRoot / "move-to-until-state.usmauto";
    {
        std::ofstream stream(stateTargetScript);
        stream << "capture_interval_ms 0\nmax_time_ms 1000\n"
                  "move_to_until_state 300 0 0 0 900\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness stateTargetHarness;
    assert(stateTargetHarness.initialize(
        stateTargetScript, autoplayTestRoot / "move-to-state-output"));
    auto stateTargetSnapshot = wallCombatSnapshot;
    stateTargetSnapshot.realTimeMilliseconds = 0;
    stateTargetSnapshot.playerPosition = {};
    stateTargetSnapshot.playerStateId = 15;
    const auto stateTargetInput =
        stateTargetHarness.update(stateTargetSnapshot);
    assert(std::hypot(stateTargetInput.motion.right,
                      stateTargetInput.motion.forward) > 0.9F);
    assert(!stateTargetHarness.complete());
    stateTargetSnapshot.realTimeMilliseconds = 50;
    stateTargetSnapshot.playerStateId = 0;
    (void)stateTargetHarness.update(stateTargetSnapshot);
    stateTargetSnapshot.realTimeMilliseconds = 100;
    (void)stateTargetHarness.update(stateTargetSnapshot);
    assert(stateTargetHarness.complete() && !stateTargetHarness.failed());

    const auto areaDamageWaitScript =
        autoplayTestRoot / "area-damage-wait.usmauto";
    {
        std::ofstream stream(areaDamageWaitScript);
        stream << "capture_interval_ms 0\nmax_time_ms 1000\n"
                  "wait_area_damage_state 900 77 waiting\nfinish\n";
    }
    usm::diagnostics::AutoplayHarness areaDamageWaitHarness;
    assert(areaDamageWaitHarness.initialize(
        areaDamageWaitScript, autoplayTestRoot / "area-damage-wait-output"));
    usm::game::LevelObjectAsset areaDamageWaitAsset;
    areaDamageWaitAsset.objectId = 77;
    areaDamageWaitAsset.kind = usm::game::LevelObjectKind::AreaDamage;
    usm::game::LevelObjectState areaDamageWaitObject;
    areaDamageWaitObject.asset = &areaDamageWaitAsset;
    areaDamageWaitObject.areaDamageState = 0;
    usm::diagnostics::AutoplaySnapshot areaDamageWaitSnapshot;
    areaDamageWaitSnapshot.objects = {&areaDamageWaitObject, 1};
    (void)areaDamageWaitHarness.update(areaDamageWaitSnapshot);
    assert(!areaDamageWaitHarness.complete() &&
           !areaDamageWaitHarness.failed());
    areaDamageWaitObject.areaDamageState = 1;
    areaDamageWaitSnapshot.realTimeMilliseconds = 50;
    (void)areaDamageWaitHarness.update(areaDamageWaitSnapshot);
    areaDamageWaitSnapshot.realTimeMilliseconds = 100;
    (void)areaDamageWaitHarness.update(areaDamageWaitSnapshot);
    assert(areaDamageWaitHarness.complete() &&
           !areaDamageWaitHarness.failed());

    usm::diagnostics::AutoplayHarness failedAutoplayHarness;
    assert(failedAutoplayHarness.initialize(
        controlsAutoplayScript, autoplayTestRoot / "failed-output"));
    failedAutoplayHarness.finish(false, "authored level resource missing");
    assert(failedAutoplayHarness.failed());
    assert(failedAutoplayHarness.failureMessage() ==
           "authored level resource missing");
    {
        std::ifstream summary(autoplayTestRoot / "failed-output" /
                              "summary.txt");
        const std::string contents{
            std::istreambuf_iterator<char>(summary),
            std::istreambuf_iterator<char>()};
        assert(contents.find("harness_failed=1") != std::string::npos);
        assert(contents.find(
                   "failure=authored level resource missing") !=
               std::string::npos);
    }

    // A traversal target must not pass while the player is underneath its
    // landing. The explicit 3D command keeps the existing planar command's
    // semantics while requiring the supplied height to be reached as well.
    const auto landingAutoplayScript = autoplayTestRoot / "landing.usmauto";
    {
        std::ofstream stream(landingAutoplayScript);
        stream << "capture_interval_ms 0\n"
                  "max_time_ms 100\n"
                  "move_to_3d 10 20 30 5 50\n"
                  "finish\n";
    }
    usm::diagnostics::AutoplaySnapshot landingSnapshot;
    landingSnapshot.gameplayActive = true;
    landingSnapshot.controlsEnabled = true;
    landingSnapshot.playerPosition = {10.0F, 20.0F, -100.0F};
    usm::diagnostics::AutoplayHarness missedLandingHarness;
    assert(missedLandingHarness.initialize(
        landingAutoplayScript, autoplayTestRoot / "missed-landing-output"));
    const auto holdLandingInput = missedLandingHarness.update(landingSnapshot);
    assert(holdLandingInput.motion.right == 0.0F);
    assert(holdLandingInput.motion.forward == 0.0F);
    assert(!missedLandingHarness.complete());
    landingSnapshot.realTimeMilliseconds = 75;
    (void)missedLandingHarness.update(landingSnapshot);
    assert(missedLandingHarness.failed());
    assert(missedLandingHarness.failureMessage().find("position=") !=
           std::string::npos);
    assert(missedLandingHarness.failureMessage().find("target=") !=
           std::string::npos);

    usm::diagnostics::AutoplayHarness landedHarness;
    assert(landedHarness.initialize(
        landingAutoplayScript, autoplayTestRoot / "landed-output"));
    landingSnapshot.realTimeMilliseconds = 0;
    (void)landedHarness.update(landingSnapshot);
    landingSnapshot.playerPosition.z = 30.0F;
    landingSnapshot.realTimeMilliseconds = 25;
    (void)landedHarness.update(landingSnapshot);
    landingSnapshot.realTimeMilliseconds = 50;
    (void)landedHarness.update(landingSnapshot);
    assert(landedHarness.complete());
    assert(!landedHarness.failed());

    const std::filesystem::path invalidAutoplayScript =
        autoplayTestRoot / "invalid.usmauto";
    {
        std::ofstream stream(invalidAutoplayScript);
        stream << "max_time_ms 100\n"
                  "start_time_ms 100\n"
                  "finish\n";
    }
    usm::diagnostics::AutoplayHarness invalidAutoplayHarness;
    assert(!invalidAutoplayHarness.initialize(
        invalidAutoplayScript, autoplayTestRoot / "invalid-output"));
    std::filesystem::remove_all(autoplayTestRoot,
                                autoplayFilesystemError);

    const usm::audio::SpatialSoundSource centeredSource{
        {0.0F, 0.0F, 0.0F}, 100.0F, 2000.0F, false};
    const auto centeredMix = usm::audio::calculateSpatialSoundMix(
        {}, {1.0F, 0.0F, 0.0F}, centeredSource);
    assert(!centeredMix.culled);
    assert(std::abs(centeredMix.attenuation - 1.0F) < 0.001F);
    assert(std::abs(centeredMix.leftGain - centeredMix.rightGain) < 0.001F);

    const usm::audio::SpatialSoundSource rightSource{
        {1000.0F, 0.0F, 0.0F}, 100.0F, 2000.0F, false};
    const auto rightMix = usm::audio::calculateSpatialSoundMix(
        {}, {1.0F, 0.0F, 0.0F}, rightSource);
    assert(!rightMix.culled);
    assert(rightMix.attenuation > 0.0F && rightMix.attenuation < 1.0F);
    assert(rightMix.rightGain > rightMix.leftGain);

    const usm::audio::SpatialSoundSource culledSource{
        {2500.0F, 0.0F, 0.0F}, 100.0F, 2000.0F, true};
    const auto culledMix = usm::audio::calculateSpatialSoundMix(
        {}, {1.0F, 0.0F, 0.0F}, culledSource);
    assert(culledMix.culled);
    assert(culledMix.attenuation == 0.0F);

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
    assert(router.state().jump.pressed);
    assert(router.state().jump.held);
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, false},
                 InputContext::Gameplay);
    assert(!router.state().jump.pressed);
    assert(router.state().jump.released);
    assert(!router.state().jump.held);

    router.beginFrame();
    router.route({XperiaKeyCode::Square, XperiaScanCode::Square, true},
                 InputContext::Gameplay);
    router.route({XperiaKeyCode::Circle, XperiaScanCode::Circle, true},
                 InputContext::Gameplay);
    assert(router.state().punch.pressed);
    assert(router.state().punch.held);
    assert(router.state().web.pressed);
    assert(router.state().web.held);
    router.beginFrame();
    assert(router.state().punch.pressed);
    assert(router.state().punch.held);
    assert(router.state().web.pressed);
    assert(router.state().web.held);
    router.beginFrame();
    assert(!router.state().punch.pressed);
    assert(router.state().punch.held);
    assert(!router.state().web.pressed);
    assert(router.state().web.held);
    router.route({XperiaKeyCode::Square, XperiaScanCode::Square, false},
                 InputContext::Gameplay);
    router.route({XperiaKeyCode::Circle, XperiaScanCode::Circle, false},
                 InputContext::Gameplay);
    assert(router.state().punch.released);
    assert(router.state().web.released);

    usm::assets::ColladaGeometry collisionFixture;
    collisionFixture.name = "wall_fixture";
    collisionFixture.vertices = {
        {{0.0F, 0.0F, 0.0F}},
        {{1000.0F, 0.0F, 0.0F}},
        {{1000.0F, 1000.0F, 0.0F}},
        {{0.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 0.0F}},
        {{500.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 1000.0F}},
        {{500.0F, 1000.0F, 1000.0F}},
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
    // constructMesh (0x003d95d8) classifies a downward normal as wall,
    // not ground. Room 4's ceiling otherwise catches its falling zombies.
    auto ceilingFixture = collisionFixture;
    ceilingFixture.name = "ceiling_fixture";
    for (auto& vertex : ceilingFixture.vertices) {
        vertex.position.z += 300.0F;
    }
    ceilingFixture.meshBuffers[0].indices = {0, 2, 1, 0, 3, 2};
    const std::array ceilingFixtureSet{collisionFixture, ceilingFixture};
    usm::game::LevelCollision ceilingFixtureWorld;
    assert(ceilingFixtureWorld.build(ceilingFixtureSet));
    assert(ceilingFixtureWorld.groundHeight({250.0F, 500.0F, 500.0F},
                                           0.0F, 1000.0F, fixtureGround));
    assert(std::abs(fixtureGround) < 0.001F);
    const auto ceilingHit = ceilingFixtureWorld.segmentFirstHit(
        {250.0F, 500.0F, 200.0F}, {250.0F, 500.0F, 400.0F});
    assert(ceilingHit && ceilingHit->physicsFlags == usm::game::LevelPhysicsFlags::Wall);
    ceilingFixture.name = "double_ceiling_fixture";
    const std::array doubleCeilingFixtureSet{collisionFixture, ceilingFixture};
    assert(ceilingFixtureWorld.build(doubleCeilingFixtureSet));
    assert(ceilingFixtureWorld.groundHeight({250.0F, 500.0F, 500.0F},
                                           0.0F, 1000.0F, fixtureGround));
    assert(std::abs(fixtureGround - 300.0F) < 0.001F);
    usm::assets::Vector3 wallResolved;
    assert(collisionFixtureWorld.resolveGroundMotion(
        {400.0F, 500.0F, 0.0F}, {600.0F, 500.0F, 0.0F}, wallResolved));
    assert(std::abs(wallResolved.x - 450.0F) < 0.01F);
    assert(collisionFixtureWorld.segmentBlocked({400.0F, 500.0F, 25.0F},
                                                {600.0F, 500.0F, 100.0F}));
    assert(!collisionFixtureWorld.segmentBlocked({600.0F, 500.0F, 100.0F},
                                                 {400.0F, 500.0F, 25.0F}));
    assert(!collisionFixtureWorld.segmentBlocked({100.0F, 500.0F, 25.0F},
                                                 {300.0F, 500.0F, 100.0F}));
    auto doubleWallFixture = collisionFixture;
    doubleWallFixture.name = "double_wall_fixture";
    const std::array doubleWallFixtureSet{doubleWallFixture};
    usm::game::LevelCollision doubleWallFixtureWorld;
    assert(doubleWallFixtureWorld.build(doubleWallFixtureSet));
    assert(doubleWallFixtureWorld.segmentBlocked(
        {600.0F, 500.0F, 100.0F}, {400.0F, 500.0F, 25.0F}));
    usm::game::LevelWallContact wallContact;
    assert(collisionFixtureWorld.climbableWallContact(
        {400.0F, 500.0F, 70.0F}, {520.0F, 500.0F, 70.0F},
        wallContact));
    assert(std::abs(wallContact.position.x - 500.0F) < 0.001F);
    assert(wallContact.normal.x < -0.99F);
    assert(wallContact.physicsFlags == 0x20U);
    assert(!collisionFixtureWorld.climbableWallContact(
        {100.0F, 500.0F, 70.0F}, {300.0F, 500.0F, 70.0F},
        wallContact));

    usm::assets::ColladaGeometry stepFixture;
    stepFixture.name = "step_fixture";
    stepFixture.vertices = {
        {{0.0F, 0.0F, 0.0F}}, {{500.0F, 0.0F, 0.0F}},
        {{500.0F, 1000.0F, 0.0F}}, {{0.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 0.0F}}, {{500.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 20.0F}}, {{500.0F, 1000.0F, 20.0F}},
        {{1000.0F, 0.0F, 20.0F}}, {{1000.0F, 1000.0F, 20.0F}},
    };
    usm::assets::ColladaMeshBuffer stepFixtureBuffer;
    stepFixtureBuffer.indices = {
        0, 1, 2, 0, 2, 3, 4, 6, 7, 4, 7, 5, 6, 8, 9, 6, 9, 7,
    };
    stepFixture.meshBuffers.push_back(stepFixtureBuffer);
    std::array<usm::assets::ColladaGeometry, 1> stepFixtureSet{stepFixture};
    usm::game::LevelCollision stepFixtureWorld;
    assert(stepFixtureWorld.build(stepFixtureSet));
    usm::assets::Vector3 stepResolved;
    assert(stepFixtureWorld.resolveGroundMotion(
        {450.0F, 500.0F, 0.0F}, {550.0F, 500.0F, 0.0F}, stepResolved,
        75.0F, 150.0F));
    assert(std::abs(stepResolved.x - 550.0F) < 0.01F);
    assert(std::abs(stepResolved.z - 20.0F) < 0.01F);

    usm::assets::ColladaGeometry jumpWallFixture;
    jumpWallFixture.name = "jump_wall_fixture";
    jumpWallFixture.vertices = {
        {{500.0F, 0.0F, 0.0F}},
        {{500.0F, 1000.0F, 0.0F}},
        {{500.0F, 0.0F, 1000.0F}},
        {{500.0F, 1000.0F, 1000.0F}},
        {{400.0F, 400.0F, 200.0F}},
        {{600.0F, 400.0F, 200.0F}},
        {{600.0F, 600.0F, 200.0F}},
        {{400.0F, 600.0F, 200.0F}},
    };
    usm::assets::ColladaMeshBuffer jumpWallFixtureBuffer;
    jumpWallFixtureBuffer.indices = {
        0, 2, 3, 0, 3, 1,
        4, 5, 6, 4, 6, 7,
    };
    jumpWallFixture.meshBuffers.push_back(jumpWallFixtureBuffer);
    usm::assets::ColladaGeometry edgeWallFixture;
    edgeWallFixture.name = "edge_wall_fixture";
    edgeWallFixture.vertices = {
        {{400.0F, 400.0F, 300.0F}},
        {{600.0F, 400.0F, 300.0F}},
        {{600.0F, 600.0F, 300.0F}},
        {{400.0F, 600.0F, 300.0F}},
    };
    usm::assets::ColladaMeshBuffer edgeWallFixtureBuffer;
    edgeWallFixtureBuffer.indices = {0, 1, 2, 0, 2, 3};
    edgeWallFixture.meshBuffers.push_back(edgeWallFixtureBuffer);
    const std::array<usm::assets::ColladaGeometry, 2>
        authoredTraversalFixtures{jumpWallFixture, edgeWallFixture};
    usm::game::LevelCollision authoredTraversalWorld;
    assert(authoredTraversalWorld.build(authoredTraversalFixtures));
    usm::assets::Vector3 blockedByJumpWall;
    authoredTraversalWorld.resolveAirMotion(
        {400.0F, 100.0F, 0.0F}, {600.0F, 100.0F, 0.0F},
        blockedByJumpWall);
    assert(std::abs(blockedByJumpWall.x - 450.0F) < 0.01F);
    usm::assets::Vector3 inPlaceBlockedByJumpWall{400.0F, 100.0F, 0.0F};
    authoredTraversalWorld.resolveAirMotion(
        inPlaceBlockedByJumpWall, {600.0F, 100.0F, 0.0F},
        inPlaceBlockedByJumpWall);
    assert(std::abs(inPlaceBlockedByJumpWall.x - 450.0F) < 0.01F);
    usm::assets::Vector3 passedJumpWall;
    authoredTraversalWorld.resolveAirMotion(
        {400.0F, 100.0F, 0.0F}, {600.0F, 100.0F, 0.0F}, passedJumpWall,
        usm::game::LevelPhysicsFlags::JumpWall);
    assert(std::abs(passedJumpWall.x - 600.0F) < 0.01F);
    float jumpWallHeight = -1.0F;
    assert(authoredTraversalWorld.groundHeight(
        {500.0F, 500.0F, 220.0F}, 50.0F, 100.0F,
        jumpWallHeight));
    assert(std::abs(jumpWallHeight - 200.0F) < 0.01F);
    assert(!authoredTraversalWorld.groundHeight(
        {500.0F, 500.0F, 220.0F}, 50.0F, 100.0F,
        jumpWallHeight, usm::game::LevelPhysicsFlags::JumpWall));
    usm::game::LevelWallContact jumpWallContact;
    assert(authoredTraversalWorld.jumpWallContact(
        {500.0F, 500.0F, 100.0F}, jumpWallContact));
    assert(jumpWallContact.physicsFlags ==
           usm::game::LevelPhysicsFlags::JumpWall);
    assert(std::abs(jumpWallContact.position.z - 200.0F) < 0.01F);
    usm::game::LevelWallContact edgeContact;
    assert(authoredTraversalWorld.climbableEdgeContact(
        {500.0F, 500.0F, 200.0F}, edgeContact));
    assert(edgeContact.physicsFlags ==
           usm::game::LevelPhysicsFlags::ClimbableEdge);
    assert(std::abs(edgeContact.position.z - 300.0F) < 0.01F);
    assert(!authoredTraversalWorld.climbableEdgeContact(
        {700.0F, 500.0F, 200.0F}, edgeContact));

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

    // Player::SearchWebGrabPoint (0x0034486c) passes a 3000 cm radius to
    // GetBestWebGrabPoint. CRoom::GetWebGrabPoints (0x0036dcf8) also rejects
    // points outside the camera frustum before Player evaluates facing/LOS.
    std::array<usm::game::LevelWebGrabPointAsset, 2> nativeCandidatePoints{};
    nativeCandidatePoints[0].objectId = 10;
    nativeCandidatePoints[0].roomId = 1;
    nativeCandidatePoints[0].position = {0.0F, 2000.0F, 0.0F};
    nativeCandidatePoints[0].visibleLength = -1.0F;
    nativeCandidatePoints[1].objectId = 11;
    nativeCandidatePoints[1].roomId = 1;
    nativeCandidatePoints[1].position = {0.0F, 3500.0F, 0.0F};
    nativeCandidatePoints[1].visibleLength = -1.0F;
    webGrabSelection.bind(nativeCandidatePoints);
    assert(webGrabSelection.findBest(selectionOrigin, {0.0F, 1.0F, 0.0F})->
               objectId == 10);
    nativeCandidatePoints[0].position = {2000.0F, 0.0F, 0.0F};
    usm::game::CameraPose grabCamera;
    grabCamera.position = {};
    grabCamera.target = {0.0F, 1.0F, 0.0F};
    grabCamera.verticalFieldOfViewDegrees = 90.0F;
    grabCamera.nearPlane = 1.0F;
    grabCamera.farPlane = 10000.0F;
    std::array<bool, 1> visibleGrabRooms{true};
    webGrabSelection.setViewContext(grabCamera, 1.0F, visibleGrabRooms);
    assert(webGrabSelection.findBest(selectionOrigin,
                                     {0.0F, 1.0F, 0.0F}) == nullptr);
    nativeCandidatePoints[0].position = {0.0F, 2000.0F, 0.0F};
    visibleGrabRooms[0] = false;
    assert(webGrabSelection.findBest(selectionOrigin,
                                     {0.0F, 1.0F, 0.0F}) == nullptr);
    visibleGrabRooms[0] = true;
    assert(webGrabSelection.findBest(selectionOrigin,
                                     {0.0F, 1.0F, 0.0F})->objectId == 10);

    std::array<usm::game::LevelWebGrabPointAsset, 1> occludedPoint{};
    occludedPoint[0].objectId = 4;
    occludedPoint[0].position = {600.0F, 500.0F, 100.0F};
    occludedPoint[0].visibleLength = -1.0F;
    webGrabSelection.bind(occludedPoint, &collisionFixtureWorld);
    assert(webGrabSelection.search({400.0F, 500.0F, 0.0F}, selectionFacing) ==
           nullptr);
    occludedPoint[0].position = {600.0F, 100.0F, 100.0F};
    webGrabSelection.bind(occludedPoint, &authoredTraversalWorld);
    assert(webGrabSelection.search({400.0F, 100.0F, 0.0F}, selectionFacing)
               ->objectId == 4);

    usm::game::LevelWebGrabPointAsset swingPoint;
    swingPoint.objectId = 377;
    swingPoint.position = {0.0F, 0.0F, 600.0F};
    swingPoint.direction = {1.0F, 0.0F, 0.0F};
    swingPoint.length = 600.0F;
    swingPoint.verticalAngleDegrees = 80.0F;
    swingPoint.horizontalAngleDegrees = 15.0F;
    swingPoint.exitSpeed = 0.45F;
    usm::game::WebSwingRuntime webSwing;
    const usm::assets::Vector3 swingEntry{-300.0F, 0.0F, 80.0F};
    assert(webSwing.start(swingPoint, swingEntry,
                          {500.0F, 0.0F, 0.0F}));
    assert(webSwing.active());
    const auto swingStartPosition = webSwing.position();
    const auto ropeLength = [&swingPoint](const usm::assets::Vector3& value) {
        const float x = value.x - swingPoint.position.x;
        const float y = value.y - swingPoint.position.y;
        const float z = value.z - swingPoint.position.z;
        return std::sqrt(x * x + y * y + z * z);
    };
    // StartWebSwing leaves the unit at its entry pose; motion 27 converges it
    // onto the authored orbit over FinishPalstanceTime's 0.9-scaled window.
    assert(std::abs(swingStartPosition.x - swingEntry.x) < 0.001F);
    assert(std::abs(swingStartPosition.z - swingEntry.z) < 0.001F);
    webSwing.update(100);
    assert(std::abs(webSwing.position().x - swingStartPosition.x) > 0.01F);
    webSwing.update(2000);
    assert(webSwing.exitAngleReached());
    const auto swingRelease = webSwing.release();
    assert(!webSwing.active());
    assert(!swingRelease.hasTargetWaypoint);
    const float horizontalReleaseSpeed = std::sqrt(
        swingRelease.velocityCentimetersPerSecond.x *
            swingRelease.velocityCentimetersPerSecond.x +
        swingRelease.velocityCentimetersPerSecond.y *
            swingRelease.velocityCentimetersPerSecond.y);
    assert(std::abs(horizontalReleaseSpeed -
                    450.0F / std::sqrt(2.0F)) < 0.01F);
    assert(std::abs(swingRelease.velocityCentimetersPerSecond.z -
                    900.0F / std::sqrt(2.0F)) < 0.01F);

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
        const auto* downtownCalm = voxSounds.find("M_DOWNTOWN_CALM");
        const auto* downtownMixed = voxSounds.find("M_DOWNTOWN_MIXED");
        const auto* sandmanMusic = voxSounds.find("M_BOSS_SANDMAN");
        const auto* loseMusic = voxSounds.find("M_LOSE");
        assert(downtownCalm != nullptr && downtownCalm->id == 4);
        assert(downtownMixed != nullptr && downtownMixed->id == 5);
        assert(sandmanMusic != nullptr && sandmanMusic->id == 22);
        assert(loseMusic != nullptr && loseMusic->id == 1);
        assert(downtownCalm->groupId == 1 &&
               downtownMixed->groupId == 1 && sandmanMusic->groupId == 1);
        assert(downtownCalm->parameter2c == 3 &&
               downtownMixed->parameter2c == 3 &&
               sandmanMusic->parameter2c == 3);
        assert(loseMusic->parameter2c == 4);
        const auto* orbCollect = voxSounds.find("SFX_ORBS_COLLECT");
        assert(orbCollect != nullptr && orbCollect->id == 98);
        const auto* knifeHurt = voxSounds.find("SFX_THUG_KNIFE_HURT_1");
        assert(knifeHurt != nullptr);
        assert(knifeHurt->resourcePath ==
               "sfx/NPC/Thugs/sfx_thug_hurt_1.wav");
        assert(voxSounds.find("SFX_VERTICAL_IMPACT") != nullptr);
        const auto* fireTrap = voxSounds.find("SFX_FIRE_TRAP");
        assert(fireTrap != nullptr);
        assert(fireTrap->id == 146);
        const auto* dropExplosion =
            voxSounds.find("SFX_BATTERY_CELL_EXPLOSION");
        assert(dropExplosion != nullptr && dropExplosion->id == 0x155);
        assert(dropExplosion->resourcePath ==
               "sfx/CUTSCENES/sfx_battery_cell_explosion.wav");
        assert(dropExplosion->minimumDistance == 0.0F);
        assert(dropExplosion->maximumDistance == 2500.0F);
        assert(!dropExplosion->distanceCullingEnabled);

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
        const auto& shortWebJumpState = playerStateConfigs.states()[30];
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
        assert(shortWebJumpState.name == "k_state_jump_web_jump");
        assert(shortWebJumpState.stateClass == 1);
        assert(shortWebJumpState.motionType == 22);
        assert(shortWebJumpState.primaryAnimationId == 97);
        assert(shortWebJumpState.nextStateId == 15);
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
        usm::audio::LevelMusicBank levelMusicBank;
        assert(levelMusicBank.preload(soundCatalog));
        const auto& calmTrack = levelMusicBank.track(
            usm::game::LevelMusicTrack::DowntownCalm);
        const auto& mixedTrack = levelMusicBank.track(
            usm::game::LevelMusicTrack::DowntownMixed);
        assert(calmTrack.frameCount() > 0);
        assert(calmTrack.sampleRate == mixedTrack.sampleRate);
        assert(calmTrack.channelCount == mixedTrack.channelCount);
        assert(calmTrack.frameCount() == mixedTrack.frameCount());
        assert(levelMusicBank
                   .track(usm::game::LevelMusicTrack::BossSandman)
                   .frameCount() > 0);
        assert(levelMusicBank.track(usm::game::LevelMusicTrack::Lose)
                   .frameCount() > 0);

        usm::game::LevelMusicRuntime levelMusicRuntime;
        levelMusicRuntime.reset();
        auto musicTransition = levelMusicRuntime.update({}, false);
        assert(musicTransition.from ==
               usm::game::LevelMusicTrack::DowntownCalm);
        assert(musicTransition.to ==
               usm::game::LevelMusicTrack::DowntownCalm);
        usm::game::LevelEnemyAsset musicEnemyAsset;
        musicEnemyAsset.enemyTypeId = 0;
        usm::game::LevelEnemyState musicEnemy;
        musicEnemy.asset = &musicEnemyAsset;
        musicEnemy.health = 100.0F;
        musicEnemy.visible = true;
        musicEnemy.aiEnabled = true;
        musicEnemy.playerDetected = true;
        musicEnemy.behavior = usm::game::EnemyBehaviorState::Chasing;
        musicTransition = levelMusicRuntime.update({&musicEnemy, 1}, false);
        assert(musicTransition.to ==
               usm::game::LevelMusicTrack::DowntownMixed);
        assert(musicTransition.fadeMilliseconds == 500);
        musicEnemy.playerDetected = false;
        musicEnemy.behavior = usm::game::EnemyBehaviorState::Idle;
        musicTransition = levelMusicRuntime.update({&musicEnemy, 1}, false);
        assert(musicTransition.to ==
               usm::game::LevelMusicTrack::DowntownCalm);
        musicEnemy.playerDetected = true;
        musicEnemy.behavior = usm::game::EnemyBehaviorState::Chasing;
        musicEnemyAsset.enemyTypeId = 16;
        musicTransition = levelMusicRuntime.update({&musicEnemy, 1}, false);
        assert(musicTransition.to ==
               usm::game::LevelMusicTrack::BossSandman);
        musicTransition = levelMusicRuntime.update({&musicEnemy, 1}, true);
        assert(musicTransition.to == usm::game::LevelMusicTrack::Lose);
        assert(!usm::game::LevelMusicRuntime::loops(
            usm::game::LevelMusicTrack::Lose));
        assert(usm::game::LevelMusicRuntime::eventName(
                   usm::game::LevelMusicTrack::BossSandman) ==
               "M_BOSS_SANDMAN");
        assert(soundCatalog.resolve("SFX_WEB_SWING_START") != nullptr);
        assert(soundCatalog.resolve("VFX_PROLOGUE_SPIDY_01") != nullptr);
        assert(soundCatalog.resolve("SFX_CUTSCENE_LV3_SPIDY_ARRIVES") !=
               nullptr);
        assert(soundCatalog.resolve("SFX_THUG_KNIFE_HURT_1") != nullptr);
        assert(soundCatalog.resolve("SFX_VERTICAL_IMPACT") != nullptr);
        usm::audio::PcmAudio catalogAudio;
        assert(soundCatalog.decode("SFX_WEB_SWING_START", catalogAudio));
        assert(catalogAudio.frameCount() > 0);
        assert(soundCatalog.decode("SFX_BATTERY_CELL_EXPLOSION",
                                   catalogAudio));
        assert(catalogAudio.frameCount() > 0);

        usm::audio::PlayerStateSoundBank playerSounds;
        usm::game::NativeRandomizer playerSoundRandomizer;
        std::vector<std::string_view> gameplaySoundStates;
        gameplaySoundStates.reserve(playerStateConfigs.states().size());
        for (const usm::game::PlayerStateDefinition& state :
             playerStateConfigs.states()) {
            gameplaySoundStates.push_back(state.name);
        }
        assert(playerSounds.preload(playerStateConfigs, voxSounds,
                                    soundCatalog, gameplaySoundStates,
                                    &playerSoundRandomizer));
        assert(playerSounds.decodedVariantCount() >= 20);
        std::size_t playerSoundPlayCount = 0;
        std::size_t loopingPlayerSoundCount = 0;
        std::vector<std::int16_t> playedPlayerVoxIds;
        const auto countPlayerSound =
            [&playerSoundPlayCount,
             &loopingPlayerSoundCount,
             &playedPlayerVoxIds](std::int16_t voxSoundId,
                                  std::string_view eventName,
                                  const usm::audio::PcmAudio& clip,
                                       bool loop) {
                assert(voxSoundId >= 0);
                assert(!eventName.empty());
                assert(clip.frameCount() > 0);
                playedPlayerVoxIds.push_back(voxSoundId);
                loopingPlayerSoundCount += loop ? 1U : 0U;
                ++playerSoundPlayCount;
                return usm::Result::success();
            };
        assert(playerSounds.dispatchStateEnter(
            "k_state_idle_to_punch_right", countPlayerSound));
        assert(playerSoundPlayCount == 0);
        assert(playerSounds.dispatchEmitter(1, 0, countPlayerSound));
        // VoxSoundManager::Play2DRandom (0x003dada0) samples the inclusive
        // 60..61 range. The first native generator value is odd.
        assert(playedPlayerVoxIds.back() == 61);
        assert(playerSoundRandomizer.state() == 632802407);
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
        assert(playedPlayerVoxIds.back() == 77);
        assert(playerSounds.dispatchStateFrame(
            "k_state_air_web_bind_ground", countPlayerSound));
        assert(playerSounds.dispatchStateEnter(
            "k_state_air_bind_ground_to_grab", countPlayerSound));
        std::vector<std::int16_t> stoppedPlayerVoxIds;
        const auto stopPlayerSound =
            [&stoppedPlayerVoxIds](std::int16_t voxSoundId,
                                   std::string_view eventName) {
                assert(!eventName.empty());
                stoppedPlayerVoxIds.push_back(voxSoundId);
                return usm::Result::success();
            };
        assert(playerSounds.dispatchStateEnter(
            "k_state_ultimate_wheel", countPlayerSound));
        assert(playedPlayerVoxIds.back() == 83);
        assert(playerSounds.dispatchStateEnter(
            "k_state_ultimate_explode", countPlayerSound, stopPlayerSound));
        assert(stoppedPlayerVoxIds == std::vector<std::int16_t>{83});

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
        const auto assertThugTextureTransform =
            [&entities](std::string_view path,
                        const std::array<float, 6>& expected) {
            std::vector<std::byte> bytes;
            assert(entities.read(path, bytes));
            usm::assets::ColladaMeshFile mesh;
            assert(mesh.load(bytes));
            bool found = false;
            for (const auto& material : mesh.materials()) {
                bool matches = true;
                for (std::size_t component = 0; component < expected.size();
                     ++component) {
                    matches &= std::abs(
                                   material.diffuseTextureTransform[component] -
                                   expected[component]) < 0.00001F;
                }
                found |= matches;
            }
            assert(found);
        };
        assertThugTextureTransform(
            "meshes_bin/thug_bat_mesh.bdae",
            {1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F});
        assertThugTextureTransform(
            "meshes_bin/thug_knife_mesh.bdae",
            {1.0F, 0.0F, 0.0F, 1.0F, -0.498F, 0.0F});
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
        assert(colladaMesh.images()[17].sourcePath == "lightmap.tga");
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
        assert(!buildingMaterial->lightmapImageIndex);
        const auto* backgroundBuilding =
            colladaMesh.findSceneNodeById("Object211502-node");
        assert(backgroundBuilding != nullptr);
        assert(backgroundBuilding->geometryIndices.size() == 1);
        assert(colladaMesh.geometries()[
                   backgroundBuilding->geometryIndices.front()].id ==
               "Object211502-mesh");
        assert(std::abs(backgroundBuilding->position.x - 15610.0996F) < 0.01F);
        assert(std::abs(backgroundBuilding->position.y + 10436.5F) < 0.01F);
        assert(std::abs(backgroundBuilding->position.z - 902.487F) < 0.01F);
        assert(std::abs(backgroundBuilding->rotation.x) < 0.0001F);
        assert(std::abs(backgroundBuilding->rotation.y) < 0.0001F);
        assert(std::abs(backgroundBuilding->rotation.z) < 0.0001F);
        assert(std::abs(backgroundBuilding->rotation.w - 1.0F) < 0.0001F);
        const auto* rotatedLevelNode =
            colladaMesh.findSceneNodeById("Object50-node");
        assert(rotatedLevelNode != nullptr);
        assert(std::abs(rotatedLevelNode->rotation.z + 0.707107F) < 0.0001F);
        assert(std::abs(rotatedLevelNode->rotation.w - 0.707107F) < 0.0001F);
        const auto rotatedLevelGeometry = std::find_if(
            colladaMesh.sceneGeometries().begin(),
            colladaMesh.sceneGeometries().end(),
            [](const usm::assets::ColladaGeometry& geometry) {
                return geometry.name == "Object50";
            });
        assert(rotatedLevelGeometry != colladaMesh.sceneGeometries().end());
        // Native quaternion::getMatrix_transposed + transformVect maps this
        // -90-degree node to these world-space bounds. A column-vector
        // interpretation mirrors the rotation and moves it to the other side
        // of the pivot, which was visible as the misoriented intro buildings.
        assert(std::abs(rotatedLevelGeometry->bounds.minimum.x - 14918.6F) <
               0.1F);
        assert(std::abs(rotatedLevelGeometry->bounds.minimum.y + 8992.28F) <
               0.1F);
        assert(std::abs(rotatedLevelGeometry->bounds.maximum.x - 15059.8F) <
               0.1F);
        assert(std::abs(rotatedLevelGeometry->bounds.maximum.y + 8920.79F) <
               0.1F);
        const auto* pivotedLevelNode =
            colladaMesh.findSceneNodeById("Object211420-node");
        const auto* pivotedLevelChild =
            colladaMesh.findSceneNodeById("Object211420-node_PIVOT");
        assert(pivotedLevelNode != nullptr);
        assert(pivotedLevelChild != nullptr);
        assert(pivotedLevelChild->parentIndex >= 0);
        assert(static_cast<std::size_t>(pivotedLevelChild->parentIndex) <
               colladaMesh.sceneNodes().size());
        assert(&colladaMesh.sceneNodes()[static_cast<std::size_t>(
                   pivotedLevelChild->parentIndex)] == pivotedLevelNode);
        const auto pivotedLevelGeometry = std::find_if(
            colladaMesh.sceneGeometries().begin(),
            colladaMesh.sceneGeometries().end(),
            [](const usm::assets::ColladaGeometry& geometry) {
                return geometry.name == "Object211420_PIVOT";
            });
        assert(pivotedLevelGeometry != colladaMesh.sceneGeometries().end());
        // This child carries a translated pivot beneath a rotated parent.
        // Native mult34 evaluates local * parent; parent * local moves the
        // prop hundreds of world units away even when leaf rotations happen
        // to be identity.
        assert(std::abs(pivotedLevelGeometry->bounds.minimum.x - 14107.3F) <
               0.1F);
        assert(std::abs(pivotedLevelGeometry->bounds.minimum.y + 9960.06F) <
               0.1F);
        assert(std::abs(pivotedLevelGeometry->bounds.maximum.x - 14253.8F) <
               0.1F);
        assert(std::abs(pivotedLevelGeometry->bounds.maximum.y + 9739.0F) <
               0.1F);
        const auto* nonUniformLevelNode =
            colladaMesh.findSceneNodeById("Box01-node");
        assert(nonUniformLevelNode != nullptr);
        assert(std::abs(nonUniformLevelNode->scale.x - 0.846907F) < 0.0001F);
        assert(std::abs(nonUniformLevelNode->scale.y - 0.846907F) < 0.0001F);
        assert(std::abs(nonUniformLevelNode->scale.z - 1.09043F) < 0.0001F);
        const auto nonUniformLevelGeometry = std::find_if(
            colladaMesh.sceneGeometries().begin(),
            colladaMesh.sceneGeometries().end(),
            [](const usm::assets::ColladaGeometry& geometry) {
                return geometry.name == "Box01";
            });
        assert(nonUniformLevelGeometry != colladaMesh.sceneGeometries().end());
        // Relative transforms scale complete matrix rows in the original
        // Irrlicht build. Scaling columns instead changes both the footprint
        // and height of this rotated building instance.
        assert(std::abs(nonUniformLevelGeometry->bounds.minimum.x - 15860.1F) <
               0.1F);
        assert(std::abs(nonUniformLevelGeometry->bounds.minimum.y + 15524.8F) <
               0.1F);
        assert(std::abs(nonUniformLevelGeometry->bounds.maximum.x - 18461.0F) <
               0.1F);
        assert(std::abs(nonUniformLevelGeometry->bounds.maximum.y + 14297.9F) <
               0.1F);
        assert(std::abs(nonUniformLevelGeometry->bounds.maximum.z - 2203.37F) <
               0.1F);
        usm::assets::ColladaAnimationFile emptyLevelAnimation;
        std::vector<usm::assets::ColladaGeometry> evaluatedLevelGeometry;
        const usm::Result evaluatedLevelResult =
            usm::assets::evaluateColladaPose(
                colladaMesh, emptyLevelAnimation, 0,
                evaluatedLevelGeometry);
        if (!evaluatedLevelResult) {
            std::cerr << "Could not evaluate static Room 1 transforms: "
                      << evaluatedLevelResult.message() << '\n';
            return 1;
        }
        if (evaluatedLevelGeometry.size() !=
            colladaMesh.sceneGeometries().size()) {
            std::cerr << "Static and animated transform paths produced "
                         "different Room 1 instance counts\n";
            return 1;
        }
        const auto boundsMatch = [](const usm::assets::AxisAlignedBounds& left,
                                    const usm::assets::AxisAlignedBounds& right) {
            return std::abs(left.minimum.x - right.minimum.x) < 0.01F &&
                   std::abs(left.minimum.y - right.minimum.y) < 0.01F &&
                   std::abs(left.minimum.z - right.minimum.z) < 0.01F &&
                   std::abs(left.maximum.x - right.maximum.x) < 0.01F &&
                   std::abs(left.maximum.y - right.maximum.y) < 0.01F &&
                   std::abs(left.maximum.z - right.maximum.z) < 0.01F;
        };
        for (std::size_t instance = 0;
             instance < evaluatedLevelGeometry.size(); ++instance) {
            if (evaluatedLevelGeometry[instance].name !=
                    colladaMesh.sceneGeometries()[instance].name ||
                !boundsMatch(evaluatedLevelGeometry[instance].bounds,
                             colladaMesh.sceneGeometries()[instance].bounds)) {
                std::cerr << "Static and animated transform paths disagree "
                             "for Room 1 instance "
                          << instance << '\n';
                return 1;
            }
        }
        const std::set<std::string> expectedLightmapMaterials{
            "Material__195", "Material__196", "Material__197",
            "Material__91120", "Material__91121", "Material__91122"};
        std::set<std::string> lightmapMaterials;
        for (const auto& material : colladaMesh.materials()) {
            if (!material.lightmapImageIndex) {
                continue;
            }
            assert(*material.lightmapImageIndex == 17);
            lightmapMaterials.insert(material.id);
        }
        assert(lightmapMaterials == expectedLightmapMaterials);
        std::set<std::uint32_t> roomVertexColors;
        for (const auto& geometry : colladaMesh.geometries()) {
            for (const auto& vertex : geometry.vertices) {
                roomVertexColors.insert(vertex.color);
            }
        }
        // geometry01 carries authored irradiance and occlusion in COLOR0,
        // including full shadow and unoccluded samples.
        assert(roomVertexColors.size() == 1507);
        assert(roomVertexColors.contains(0xff000000U));
        assert(roomVertexColors.contains(0xffffffffU));

        std::size_t meshFileCount = 0;
        std::size_t geometryCount = 0;
        std::size_t meshBufferCount = 0;
        std::size_t staticTransformFileCount = 0;
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
            for (const auto& geometry : parsedMesh.sceneGeometries()) {
                const auto finiteBounds = [](const auto& bounds) {
                    return std::isfinite(bounds.minimum.x) &&
                           std::isfinite(bounds.minimum.y) &&
                           std::isfinite(bounds.minimum.z) &&
                           std::isfinite(bounds.maximum.x) &&
                           std::isfinite(bounds.maximum.y) &&
                           std::isfinite(bounds.maximum.z) &&
                           bounds.minimum.x <= bounds.maximum.x &&
                           bounds.minimum.y <= bounds.maximum.y &&
                           bounds.minimum.z <= bounds.maximum.z;
                };
                if (!finiteBounds(geometry.bounds)) {
                    std::cerr << "Invalid transformed bounds in "
                              << entry.path << " geometry " << geometry.name
                              << '\n';
                    return 1;
                }
            }
            if (!parsedMesh.skins().empty()) {
                continue;
            }
            std::vector<usm::assets::ColladaGeometry> evaluatedGeometry;
            const usm::Result evaluationResult =
                usm::assets::evaluateColladaPose(
                    parsedMesh, emptyLevelAnimation, 0,
                    evaluatedGeometry);
            if (!evaluationResult ||
                evaluatedGeometry.size() !=
                    parsedMesh.sceneGeometries().size()) {
                std::cerr << "Static transform cross-check failed for "
                          << entry.path << '\n';
                return 1;
            }
            for (std::size_t instance = 0;
                 instance < evaluatedGeometry.size(); ++instance) {
                if (evaluatedGeometry[instance].name !=
                        parsedMesh.sceneGeometries()[instance].name ||
                    !boundsMatch(
                        evaluatedGeometry[instance].bounds,
                        parsedMesh.sceneGeometries()[instance].bounds)) {
                    std::cerr << "Transform evaluators disagree in "
                              << entry.path << " instance " << instance
                              << '\n';
                    return 1;
                }
            }
            ++staticTransformFileCount;
        }
        assert(meshFileCount == 113);
        assert(geometryCount == 1178);
        assert(meshBufferCount == 1612);
        assert(staticTransformFileCount != 0);

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
        assert(bootstrap.webLine().textureFile ==
               "textures_bin/web_rope.tga");
        assert(!bootstrap.webLine().texture.mipLevels().empty());
        assert(bootstrap.webLine().texture.containsAlpha());
        // MCHitEffectFile::ReadBasicState (0x0033cacc) loads the exact
        // attack-trail definitions used by Player::UpdateNormalEffect.
        assert(bootstrap.playerHitEffectConfigs().definitions().size() == 31);
        assert(bootstrap.playerHitEffects().size() == 31);
        const auto* rightPunchEffect =
            bootstrap.playerHitEffectConfigs().find(0);
        assert(rightPunchEffect != nullptr);
        assert(rightPunchEffect->name == "fx_punch_right");
        assert(rightPunchEffect->meshFile == "fx_punch_right.bdae");
        assert(rightPunchEffect->boneName == "Bip01_Pelvis");
        assert(rightPunchEffect->snapshotBoneTransform);
        assert(std::abs(rightPunchEffect->lifetimeMilliseconds - 300.0F) <
               0.001F);
        const auto& rightPunchMaterials =
            bootstrap.playerHitEffects()[0].mesh.materials();
        assert(!rightPunchMaterials.empty());
        for (const auto& material : rightPunchMaterials) {
            // SEffect stores FF959595, but CAnimObjEffect's 0x1d/0x1e
            // renderers do not consume AmbientColor. Retain it so the BDAE
            // mapping remains complete and distinct from the later custom
            // ambient-combiner IDs 0x20-0x25.
            assert(std::abs(material.ambientColor[0] - 149.0F / 255.0F) <
                   0.0001F);
            assert(std::abs(material.ambientColor[1] - 149.0F / 255.0F) <
                   0.0001F);
            assert(std::abs(material.ambientColor[2] - 149.0F / 255.0F) <
                   0.0001F);
            assert(material.ambientColor[3] == 1.0F);
            assert(material.materialTypeParameter == 0.0F);
        }
        const auto& rightPunchTextures =
            bootstrap.playerHitEffects()[0].textures;
        assert(rightPunchTextures.size() == 1);
        assert(rightPunchTextures.front().containsAlpha());
        assert(!rightPunchTextures.front().mipLevels().empty());
        const auto& rightPunchPixels =
            rightPunchTextures.front().mipLevels().front().pixels;
        std::uint8_t rightPunchMinimumAlpha = 0xff;
        std::uint8_t rightPunchMaximumAlpha = 0;
        for (std::size_t alpha = 3; alpha < rightPunchPixels.size();
             alpha += 4) {
            rightPunchMinimumAlpha =
                std::min(rightPunchMinimumAlpha, rightPunchPixels[alpha]);
            rightPunchMaximumAlpha =
                std::max(rightPunchMaximumAlpha, rightPunchPixels[alpha]);
        }
        assert(rightPunchMinimumAlpha < rightPunchMaximumAlpha);
        assert(rightPunchMaximumAlpha == 0xff);
        assert(bootstrap.playerHitEffectConfigs().find(-1) == nullptr);
        assert(bootstrap.playerHitEffectConfigs().find(31) == nullptr);
        for (std::size_t effectIndex = 0;
             effectIndex < bootstrap.playerHitEffects().size();
             ++effectIndex) {
            const auto& effect = bootstrap.playerHitEffects()[effectIndex];
            assert(effect.definition.id == effectIndex);
            assert(!effect.mesh.sceneGeometries().empty());
            assert(effect.mesh.images().size() == effect.textures.size());
            for (const auto& material : effect.mesh.materials()) {
                // CMaterial::prepareMaterial (0x0041ca8c) copies SEffect+0x2c
                // to SMaterial+0x4c. Material 0x1e reads this exact value at
                // 0x00397842 for GL_GREATER; all shipped hit-effect records
                // author zero, not the unrelated 0.5 alpha-test constant.
                assert(material.materialTypeParameter == 0.0F);
            }
        }
        assert(bootstrap.playerHitEffects()[24].animation.clips().size() == 2);
        assert(bootstrap.playerHitEffects()[24].animation.clips()[1].name ==
               "explode");
        assert(bootstrap.playerHitEffects()[25].animation.clips()[0].name ==
               "shrink");
        assert(bootstrap.playerHitEffects()[25].animation.clips()[0]
                   .durationMilliseconds() == 566);
        const auto& ultimateWebEffect = bootstrap.playerHitEffects()[24];
        assert(ultimateWebEffect.mesh.morphs().size() == 2);
        const auto& ultimateObjectOneMorph =
            ultimateWebEffect.mesh.morphs()[0];
        assert(ultimateObjectOneMorph.controllerId ==
               "Object01-mesh-morpher");
        assert(ultimateObjectOneMorph.sourceGeometryId == "Object01-mesh");
        assert(ultimateObjectOneMorph.method == 0);
        assert((ultimateObjectOneMorph.targetGeometryIndices ==
                std::vector<std::uint32_t>{6, 7}));
        assert((ultimateObjectOneMorph.weights ==
                std::vector<float>{1.0F, 0.0F}));
        const auto& ultimateObjectFourMorph =
            ultimateWebEffect.mesh.morphs()[1];
        assert(ultimateObjectFourMorph.controllerId ==
               "Object04-mesh-morpher");
        assert(ultimateObjectFourMorph.method == 0);
        assert((ultimateObjectFourMorph.targetGeometryIndices ==
                std::vector<std::uint32_t>{8, 9}));
        assert((ultimateObjectFourMorph.weights ==
                std::vector<float>{1.0F, 0.0F}));
        assert(ultimateWebEffect.mesh.sceneNodes().size() == 7);
        assert(ultimateWebEffect.mesh.sceneNodes()[0]
                   .geometryControllerIds.front() ==
               "Object01-mesh-morpher");
        assert(ultimateWebEffect.mesh.sceneNodes()[2]
                   .geometryControllerIds.front() ==
               "Object04-mesh-morpher");
        const auto findUltimateTrack =
            [&ultimateWebEffect](std::string_view id) {
                return std::find_if(
                    ultimateWebEffect.animation.tracks().begin(),
                    ultimateWebEffect.animation.tracks().end(),
                    [id](const auto& track) { return track.id == id; });
            };
        const auto objectOneWeight =
            findUltimateTrack("Object01-mesh-morpher-weights");
        assert(objectOneWeight != ultimateWebEffect.animation.tracks().end());
        assert(objectOneWeight->property ==
               usm::assets::ColladaAnimationProperty::MorphWeight);
        // ISceneNodeAnimator::forceBind (0x00429870) consumes SChannel+0x0c,
        // not the textual "weights" suffix. This channel really addresses
        // target slot 1 while Object04's two channels address slots 0 and 1.
        assert(objectOneWeight->targetIndex == 1);
        const auto objectFourWeight =
            findUltimateTrack("Object04-mesh-morpher-weights");
        const auto objectFourWeightOne =
            findUltimateTrack("Object04-mesh-morpher-weights1");
        assert(objectFourWeight != ultimateWebEffect.animation.tracks().end());
        assert(objectFourWeightOne !=
               ultimateWebEffect.animation.tracks().end());
        assert(objectFourWeight->targetIndex == 0);
        assert(objectFourWeightOne->targetIndex == 1);
        std::vector<usm::assets::ColladaGeometry> ultimateShrinkStart;
        std::vector<usm::assets::ColladaGeometry> ultimateExplodeEnd;
        assert(usm::assets::evaluateColladaPose(
            ultimateWebEffect.mesh, ultimateWebEffect.animation, 0,
            ultimateShrinkStart));
        assert(usm::assets::evaluateColladaPose(
            ultimateWebEffect.mesh, ultimateWebEffect.animation, 1399,
            ultimateExplodeEnd));
        assert(ultimateShrinkStart.size() == 6);
        assert(ultimateExplodeEnd.size() == 6);
        const auto& shrinkVertex =
            ultimateShrinkStart[0].vertices.front().position;
        const auto& explodeVertex =
            ultimateExplodeEnd[0].vertices.front().position;
        assert(std::abs(shrinkVertex.x - -7.49384F) < 0.001F);
        assert(std::abs(shrinkVertex.y - -56.4381F) < 0.001F);
        assert(std::abs(shrinkVertex.z - -7.66562F) < 0.001F);
        assert(std::abs(explodeVertex.x - -179.866F) < 0.001F);
        assert(std::abs(explodeVertex.y - -553.851F) < 0.001F);
        assert(std::abs(explodeVertex.z - 14.2984F) < 0.001F);
        // GS_Confirmation shipped resources recovered from Create/Render at
        // 0x002dc2ec/0x002dbfa8.
        assert(bootstrap.hud().mainMenuAtlas.modules().size() == 78);
        assert(bootstrap.hud().mainMenuAtlas.frames().size() == 78);
        assert(bootstrap.hud().mainMenuAtlas.animations().size() == 51);
        assert(bootstrap.hud().backgroundSuitAtlas.frames().size() == 2);
        assert(bootstrap.hud().backgroundSuitTexture.image().width == 512);
        assert(bootstrap.hud().backgroundSuitTexture.image().height == 512);
        assert(bootstrap.hud().backgroundSuitTexture.image().pixels.size() ==
               512U * 512U * 4U);
        // CTutorial::RenderMessageInfo (0x0038be54) uses frame 1 for the
        // expandable panel and GetFrameIdByFace (0x0038b98c) maps the cop to
        // frame 5 in tutorial.bsprite.
        assert(bootstrap.hud().tutorialAtlas.frames().size() > 5);
        assert(bootstrap.hud().tutorialAtlas.modulesForFrame(1).size() >= 5);
        assert(!bootstrap.hud().tutorialAtlas.modulesForFrame(5).empty());
        assert(bootstrap.hud().tutorialTexture.image().width != 0);
        assert(bootstrap.hud().tutorialTexture.image().height != 0);
        assert(bootstrap.hud().transportAtlas.frames().size() == 1);
        assert(!bootstrap.hud().transportAtlas.modulesForFrame(0).empty());
        assert(bootstrap.hud().transportTexture.image().width != 0);
        assert(bootstrap.hud().transportTexture.image().height != 0);
        assert(!bootstrap.hud().normalWhiteFontAtlas.frameModules().empty());
        assert(!bootstrap.hud().outlineSmallFontAtlas.frameModules().empty());
        assert(!bootstrap.hud().outlineBigFontAtlas.frameModules().empty());

        usm::game::LevelCheckPointRuntime checkPointRuntime;
        assert(checkPointRuntime.bind(bootstrap.checkPoints(),
                                      bootstrap.waypoints()));
        const auto roomTwoCheckPoint = std::find_if(
            bootstrap.checkPoints().begin(), bootstrap.checkPoints().end(),
            [](const usm::game::LevelCheckPointAsset& checkPoint) {
                return checkPoint.objectId == 755;
            });
        assert(roomTwoCheckPoint != bootstrap.checkPoints().end());
        const usm::assets::Vector3 automaticFacing{0.0F, 1.0F, 0.0F};
        const auto automaticCheckPoint = checkPointRuntime.update(
            roomTwoCheckPoint->position, automaticFacing, 1.0F, 339);
        assert(automaticCheckPoint.has_value());
        assert(automaticCheckPoint->objectId == 755);
        assert(automaticCheckPoint->automatic);
        assert(!checkPointRuntime.update(roomTwoCheckPoint->position,
                                         automaticFacing, 1.0F, 339));
        const auto automaticRestart = checkPointRuntime.restartPlacement();
        assert(automaticRestart.has_value());
        assert(automaticRestart->kind ==
               usm::game::CheckPointPlacementKind::SavedPlayerTransform);
        assert(automaticRestart->cameraAreaId == 339);
        checkPointRuntime.resetForLevelRestart();
        assert(checkPointRuntime.update(roomTwoCheckPoint->position,
                                        automaticFacing, 1.0F, 339));
        assert(checkPointRuntime.save(
            30025, 283, {123.0F, 456.0F, 789.0F},
            {1.0F, 0.0F, 0.0F}));
        const auto linkedRestart = checkPointRuntime.restartPlacement();
        assert(linkedRestart.has_value());
        assert(linkedRestart->kind ==
               usm::game::CheckPointPlacementKind::LinkedWaypoint);
        const auto linkedWayPoint = std::find_if(
            bootstrap.waypoints().begin(), bootstrap.waypoints().end(),
            [](const usm::game::LevelWayPointAsset& wayPoint) {
                return wayPoint.objectId == 30026;
            });
        assert(linkedWayPoint != bootstrap.waypoints().end());
        assert(linkedRestart->playerPosition.x == linkedWayPoint->position.x);
        assert(linkedRestart->playerPosition.y == linkedWayPoint->position.y);
        assert(linkedRestart->playerPosition.z == linkedWayPoint->position.z);
        assert(linkedRestart->faceCameraAfterPlacement);

        usm::game::DeathConfirmationRuntime confirmationRuntime;
        assert(confirmationRuntime.bind(bootstrap.textCatalog()));
        confirmationRuntime.update(false, false, false, false);
        assert(!confirmationRuntime.active());
        confirmationRuntime.update(true, false, false, false);
        assert(confirmationRuntime.active());
        assert(confirmationRuntime.selection() == 0);
        const auto confirmationFrame = confirmationRuntime.frame();
        assert(confirmationFrame.title == u"Confirmation");
        assert(confirmationFrame.message == u"Rhino has escaped. Retry?");
        assert(confirmationFrame.yes == u"YES");
        assert(confirmationFrame.no == u"NO");
        confirmationRuntime.update(true, false, true, false);
        assert(confirmationRuntime.selection() == 1);
        confirmationRuntime.update(true, false, true, false);
        assert(confirmationRuntime.selection() == 0);
        confirmationRuntime.update(true, true, false, false);
        assert(confirmationRuntime.selection() == 1);
        confirmationRuntime.update(true, false, false, true);
        assert(confirmationRuntime.consumeOutcome() ==
               usm::game::DeathConfirmationOutcome::Exit);
        confirmationRuntime.update(true, false, true, true);
        assert(confirmationRuntime.consumeOutcome() ==
               usm::game::DeathConfirmationOutcome::Retry);

        usm::game::ExitMenuRuntime exitMenuRuntime;
        assert(exitMenuRuntime.bind(bootstrap.textCatalog()));
        assert(!exitMenuRuntime.loadingLabel().empty());
        assert(!exitMenuRuntime.loadingSuffix().empty());
        assert(!exitMenuRuntime.active());
        assert(exitMenuRuntime.blackOverlayAlpha() == 0.0F);
        exitMenuRuntime.beginAfterDeath();
        assert(exitMenuRuntime.active());
        assert(exitMenuRuntime.state() == 0);
        assert(exitMenuRuntime.parentVisible());
        assert(!exitMenuRuntime.loadingTextVisible());
        assert(std::abs(exitMenuRuntime.blackOverlayAlpha() -
                        150.0F / 255.0F) < 0.0001F);
        exitMenuRuntime.update(100);
        assert(exitMenuRuntime.state() == 1);
        assert(exitMenuRuntime.loadingTextVisible());
        for (int tick = 1; tick < 15; ++tick) {
            exitMenuRuntime.update(50);
        }
        assert(exitMenuRuntime.state() == 15);
        assert(!exitMenuRuntime.parentVisible());
        assert(exitMenuRuntime.loadingTextVisible());
        assert(exitMenuRuntime.blackOverlayAlpha() == 1.0F);
        exitMenuRuntime.update(50);
        assert(exitMenuRuntime.state() == 16);
        assert(!exitMenuRuntime.loadingTextVisible());
        for (int tick = 16; tick < 20; ++tick) {
            exitMenuRuntime.update(50);
        }
        assert(exitMenuRuntime.mainMenuRequested());
        exitMenuRuntime.reset();
        assert(!exitMenuRuntime.active());
        usm::game::LevelOneBootstrap levelTwo;
        const usm::Result levelTwoResult = levelTwo.load(dataRoot, 2);
        if (!levelTwoResult) {
            std::cerr << "Level-two bootstrap failed: "
                      << levelTwoResult.message() << '\n';
            return 1;
        }
        assert(levelTwo.levelNumber() == 2);
        assert(!levelTwo.hasIntroCinematic());
        assert(levelTwo.player().objectId == 288);
        assert(levelTwo.player().initialCameraAreaId == 10110);
        assert(levelTwo.rooms().size() == 10);
        assert(levelTwo.rooms().front().sceneFile ==
               "levelnew_02_183_Room14.irr");
        assert(levelTwo.rooms().back().sceneFile ==
               "levelnew_02_212_Room23.irr");
        assert(levelTwo.cameraAreas().size() == 49);
        assert(levelTwo.cinematics().size() == 40);
        assert(levelTwo.enemies().size() == 44);
        const auto findLevelTwoCommand =
            [&levelTwo](std::int32_t cinematicId,
                        std::string_view commandName) {
                const auto cinematic = std::find_if(
                    levelTwo.cinematics().begin(),
                    levelTwo.cinematics().end(),
                    [cinematicId](const auto& candidate) {
                        return candidate.objectId == cinematicId;
                    });
                if (cinematic != levelTwo.cinematics().end()) {
                    for (const auto& thread : cinematic->script.threads()) {
                        const auto command = std::find_if(
                            thread.commands.begin(), thread.commands.end(),
                            [commandName](const auto& candidate) {
                                return candidate.name == commandName;
                            });
                        if (command != thread.commands.end()) {
                            return std::pair{&thread, &*command};
                        }
                    }
                }
                return std::pair<
                    const usm::game::CinematicThread*,
                    const usm::game::CinematicCommand*>{nullptr, nullptr};
            };
        const auto levelTwoMolotov = std::find_if(
            levelTwo.enemies().begin(), levelTwo.enemies().end(),
            [](const auto& enemy) { return enemy.objectId == 642; });
        assert(levelTwoMolotov != levelTwo.enemies().end());
        assert(levelTwoMolotov->gameType == "RangeThug_molotov");
        assert(levelTwoMolotov->enemyTypeId == 2);
        assert(levelTwoMolotov->initialAnimation == "idle");
        const auto levelTwoRhino = std::find_if(
            levelTwo.enemies().begin(), levelTwo.enemies().end(),
            [](const auto& enemy) { return enemy.objectId == 20019; });
        assert(levelTwoRhino != levelTwo.enemies().end());
        assert(levelTwoRhino->gameType == "Boss_Rhino");
        assert(levelTwoRhino->enemyTypeId == 6);
        assert(levelTwoRhino->initialAnimation == "idle");
        const auto& levelTwoRhinoArchetype =
            levelTwo.enemyArchetypes()[levelTwoRhino->archetypeIndex];
        assert(levelTwoRhinoArchetype.animationBank.findClip("idle") !=
               nullptr);
        assert(levelTwoRhinoArchetype.animationBank.findClip(
                   "idle_charge_run") != nullptr);
        const auto levelTwoBossRhino = std::find_if(
            levelTwo.enemies().begin(), levelTwo.enemies().end(),
            [](const auto& enemy) { return enemy.objectId == 20055; });
        assert(levelTwoBossRhino != levelTwo.enemies().end());
        assert(levelTwoBossRhino->gameType == "Boss_Rhino");
        assert(levelTwoBossRhino->lineSpeedCentimetersPerMillisecond > 0.0F);
        const auto& molotovProjectile = levelTwo.molotovProjectile();
        assert(molotovProjectile.meshFile ==
               "meshes_bin/w_flamegrenade.bdae");
        assert(!molotovProjectile.mesh.geometries().empty());
        assert(molotovProjectile.mesh.images().size() ==
               molotovProjectile.textures.size());
        const auto* molotovFly =
            molotovProjectile.animationBank.findClip("fly");
        const auto* molotovExplodeReady =
            molotovProjectile.animationBank.findClip("explode_ready");
        assert(molotovFly != nullptr &&
               molotovFly->durationMilliseconds() == 800);
        assert(molotovExplodeReady != nullptr &&
               molotovExplodeReady->durationMilliseconds() == 1333);
        assert(levelTwo.effects().presets.find("molotov_bomb") != nullptr);
        // Native CLevel::LoadNextObject selects the complete SlideCar prefix;
        // Level 2 therefore contains one additional non-bus SlideCar.
        assert(levelTwo.objects().size() == 137);
        assert(levelTwo.triggers().size() == 18);
        const auto levelTwoChaseFinishTrigger = std::find_if(
            levelTwo.triggers().begin(), levelTwo.triggers().end(),
            [](const usm::game::LevelTriggerAsset& trigger) {
                return trigger.objectId == 40013;
            });
        assert(levelTwoChaseFinishTrigger != levelTwo.triggers().end());
        assert(levelTwoChaseFinishTrigger->sizes.y < 0.0F);
        usm::game::LevelTriggerRuntime levelTwoChaseFinishTriggerRuntime;
        levelTwoChaseFinishTriggerRuntime.bind(
            std::span<const usm::game::LevelTriggerAsset>(
                &*levelTwoChaseFinishTrigger, 1));
        const usm::assets::Vector3 chaseFinishCenter{
            levelTwoChaseFinishTrigger->worldTransform[12],
            levelTwoChaseFinishTrigger->worldTransform[13],
            levelTwoChaseFinishTrigger->worldTransform[14]};
        assert(levelTwoChaseFinishTriggerRuntime
                   .update({chaseFinishCenter.x -
                                std::abs(levelTwoChaseFinishTrigger->sizes.x),
                            chaseFinishCenter.y, chaseFinishCenter.z})
                   .empty());
        const auto chaseFinishEvents =
            levelTwoChaseFinishTriggerRuntime.update(chaseFinishCenter);
        assert(chaseFinishEvents.size() == 1);
        assert(chaseFinishEvents.front().triggerId == 40013);
        assert(chaseFinishEvents.front().cinematicId == 20051);
        assert(chaseFinishEvents.front().kind ==
               usm::game::TriggerEventKind::Entered);
        assert(levelTwo.bonuses().size() == 36);
        const auto levelTwoSlopedRoofRestore = std::find_if(
            levelTwo.restoreTriggers().begin(),
            levelTwo.restoreTriggers().end(),
            [](const usm::game::LevelRestoreTriggerAsset& trigger) {
                return trigger.objectId == 40027;
            });
        assert(levelTwoSlopedRoofRestore !=
               levelTwo.restoreTriggers().end());
        assert(levelTwoSlopedRoofRestore->worldTransform[15] == 1.0F);
        usm::game::LevelRestoreRuntime levelTwoSlopedRoofRestoreRuntime;
        assert(levelTwoSlopedRoofRestoreRuntime.bind(
            levelTwo.restoreTriggers(), levelTwo.restorePoints()));
        // Trigger 40027 is rotated to follow the underside of Room 1's
        // sloping rooftop. The conventional-quaternion shortcut tilted it in
        // the opposite direction and swallowed this valid route point.
        levelTwoSlopedRoofRestoreRuntime.update(
            {-15545.2F, 13252.4F, 6555.7F}, 1);
        assert(!levelTwoSlopedRoofRestoreRuntime.active());
        levelTwoSlopedRoofRestoreRuntime.update(
            levelTwoSlopedRoofRestore->position, 1);
        assert(levelTwoSlopedRoofRestoreRuntime.active());
        const std::size_t levelTwoLightmapMaterials =
            std::accumulate(
                levelTwo.rooms().begin(), levelTwo.rooms().end(),
                std::size_t{}, [](std::size_t count, const auto& room) {
                    return count + static_cast<std::size_t>(std::count_if(
                        room.geometry.materials().begin(),
                        room.geometry.materials().end(),
                        [](const auto& material) {
                            return material.lightmapImageIndex.has_value();
                        }));
                });
        assert(levelTwoLightmapMaterials != 0);
        std::vector<const usm::game::CinematicScript*>
            levelTwoSoundScripts;
        const usm::game::CinematicCommand* levelTwoSilentSoundCommand =
            nullptr;
        for (const auto& cinematic : levelTwo.cinematics()) {
            if (!cinematic.scriptAvailable) {
                continue;
            }
            levelTwoSoundScripts.push_back(&cinematic.script);
            for (const auto& thread : cinematic.script.threads()) {
                for (const auto& command : thread.commands) {
                    const auto* event = command.findAttribute("$VoxSounds");
                    if (command.name == "SoundControl" && event != nullptr &&
                        event->value.empty()) {
                        assert(levelTwoSilentSoundCommand == nullptr);
                        levelTwoSilentSoundCommand = &command;
                    }
                }
            }
        }
        assert(levelTwoSilentSoundCommand != nullptr);
        usm::audio::CinematicSoundBank levelTwoSounds;
        assert(levelTwoSounds.preload(levelTwoSoundScripts, soundCatalog));
        std::size_t levelTwoSilentSoundPlayCount = 0;
        assert(levelTwoSounds.dispatch(
            *levelTwoSilentSoundCommand,
            [&levelTwoSilentSoundPlayCount](std::string_view,
                                            const usm::audio::PcmAudio&, bool) {
                ++levelTwoSilentSoundPlayCount;
                return usm::Result::success();
            }));
        assert(levelTwoSilentSoundPlayCount == 0);
        const auto [restoreThread, restoreCommand] =
            findLevelTwoCommand(20053, "Restore");
        assert(restoreThread != nullptr && restoreCommand != nullptr);
        usm::game::GameplayPlayer scriptedRestorePlayer;
        assert(scriptedRestorePlayer.initialize(levelTwo.player(), nullptr,
                                                &playerStateConfigs));
        assert(scriptedRestorePlayer.health() > 0.0F);
        assert(scriptedRestorePlayer.applyCinematicCommand(*restoreThread,
                                                           *restoreCommand));
        assert(scriptedRestorePlayer.health() == -1.0F);
        scriptedRestorePlayer.update({}, {}, 0);
        assert(scriptedRestorePlayer.activeStateId() == 0x80);

        // CCinematicThread::Init (0x00371de0) ignores the serialized object
        // ID for a type-3 thread and binds CLevel's active player.  The
        // shipped bank-up transition deliberately carries enemy ID 1141 in
        // its player thread; it must move Spider-Man without also moving the
        // enemy that happens to share that ID.
        const auto [bankUpPlayerThread, bankUpMoveCommand] =
            findLevelTwoCommand(467, "MoveObject");
        assert(bankUpPlayerThread != nullptr && bankUpMoveCommand != nullptr);
        assert(bankUpPlayerThread->type == 3);
        assert(bankUpPlayerThread->objectId == 1141);
        usm::game::GameplayPlayer bankUpPlayer;
        assert(bankUpPlayer.initialize(levelTwo.player(), nullptr,
                                       &playerStateConfigs));
        assert(bankUpPlayer.applyCinematicCommand(*bankUpPlayerThread,
                                                  *bankUpMoveCommand));
        assert(std::abs(bankUpPlayer.position().x - -19444.759766F) < 0.01F);
        assert(std::abs(bankUpPlayer.position().y - 9991.820313F) < 0.01F);
        assert(std::abs(bankUpPlayer.position().z - 962.339966F) < 0.01F);
        usm::game::LevelEnemyRuntime bankUpEnemies;
        assert(bankUpEnemies.initialize(levelTwo));
        const usm::assets::Vector3 bankUpEnemyPosition =
            bankUpEnemies.find(1141)->position;
        assert(bankUpEnemies.applyCinematicCommand(
            levelTwo, *bankUpPlayerThread, *bankUpMoveCommand));
        assert(bankUpEnemies.find(1141)->position.x == bankUpEnemyPosition.x);
        assert(bankUpEnemies.find(1141)->position.y == bankUpEnemyPosition.y);
        assert(bankUpEnemies.find(1141)->position.z == bankUpEnemyPosition.z);

        usm::game::LevelOneBootstrap levelThree;
        const usm::Result levelThreeResult = levelThree.load(dataRoot, 3);
        if (!levelThreeResult) {
            std::cerr << "Level-three bootstrap failed: "
                      << levelThreeResult.message() << '\n';
            return 1;
        }
        assert(levelThree.levelNumber() == 3);
        assert(!levelThree.hasIntroCinematic());
        assert(levelThree.player().objectId == 30348);
        assert(levelThree.player().initialCameraAreaId == 30039);
        assert(levelThree.player().linkedCinematicId == 31110);
        assert(levelThree.rooms().size() == 10);
        assert(levelThree.rooms().front().sceneFile ==
               "levelnew_03_30000_Room1.irr");
        assert(levelThree.rooms().back().sceneFile ==
               "levelnew_03_30036_Room10.irr");
        assert(levelThree.rooms()[2].collision.geometries().empty());
        assert(levelThree.cameraAreas().size() == 58);
        assert(levelThree.cinematics().size() == 28);
        assert(levelThree.enemies().size() == 46);
        // The 103 previously reconstructed mesh objects are joined by seven
        // authored CPlatForm instances and 92 CElectricPlatForm instances.
        // Both platform classes construct platform_phy.bdae independently of
        // the room node's serialized Collision=false flag.
        assert(levelThree.objects().size() == 202);
        assert(std::count_if(
                   levelThree.objects().begin(), levelThree.objects().end(),
                   [](const auto& object) {
                       return object.kind ==
                              usm::game::LevelObjectKind::Platform;
                   }) == 7);
        assert(std::count_if(
                   levelThree.objects().begin(), levelThree.objects().end(),
                   [](const auto& object) {
                       return object.kind ==
                              usm::game::LevelObjectKind::ElectricPlatform;
                   }) == 92);
        assert(levelThree.triggers().size() == 12);
        assert(levelThree.bonuses().size() == 109);
        assert(levelThree.restoreTriggers().size() == 18);
        assert(levelThree.slides().size() == 7);
        const auto levelThreeRestore31106 = std::find_if(
            levelThree.restoreTriggers().begin(),
            levelThree.restoreTriggers().end(),
            [](const auto& trigger) { return trigger.objectId == 31106; });
        assert(levelThreeRestore31106 != levelThree.restoreTriggers().end());
        assert(levelThreeRestore31106->restorePointId == 31107);
        assert(levelThreeRestore31106->cinematicId == -1);
        assert(!levelThreeRestore31106->fallAfterRestore);
        assert(!levelThreeRestore31106->useLastCheckpoint);

        const auto levelThreeElectro = std::find_if(
            levelThree.enemies().begin(), levelThree.enemies().end(),
            [](const auto& enemy) { return enemy.objectId == 30418; });
        assert(levelThreeElectro != levelThree.enemies().end());
        assert(levelThreeElectro->enemyTypeId == 6);
        const auto levelThreeRuntimeElectro = std::find_if(
            levelThree.enemies().begin(), levelThree.enemies().end(),
            [](const auto& enemy) { return enemy.objectId == 31094; });
        assert(levelThreeRuntimeElectro != levelThree.enemies().end());
        assert(levelThreeRuntimeElectro->gameType == "Boss_Electro");
        assert(levelThreeRuntimeElectro->enemyTypeId == 7);
        const auto& electroArchetype = levelThree.enemyArchetypes()[
            levelThreeElectro->archetypeIndex];
        const auto& runtimeElectroArchetype = levelThree.enemyArchetypes()[
            levelThreeRuntimeElectro->archetypeIndex];
        assert(runtimeElectroArchetype.gameType == "Boss_Electro");
        assert(electroArchetype.gameType == "Boss_Electro");
        assert(electroArchetype.mesh.skins().size() == 1);
        const auto& electroSkin = electroArchetype.mesh.skins().front();
        assert(electroSkin.jointNames.size() == 24);
        assert(electroSkin.vertexInfluences.size() == 436);
        const auto* electroStand =
            electroArchetype.animationBank.findClip("stand");
        assert(electroStand != nullptr);
        assert(electroStand->startMilliseconds == 18100);
        assert(electroStand->endMilliseconds == 19166);
        struct ExpectedElectroClip {
            std::string_view name;
            std::uint32_t durationMilliseconds;
        };
        constexpr std::array expectedElectroClips{
            ExpectedElectroClip{"attack_fall", 833},
            ExpectedElectroClip{"stand_to_weak", 600},
            ExpectedElectroClip{"weak", 2066},
            ExpectedElectroClip{"scream", 4067},
            ExpectedElectroClip{"attack_beam_ready", 733},
            ExpectedElectroClip{"attack_beam", 533},
            ExpectedElectroClip{"attack_beam_to_idle", 367},
            ExpectedElectroClip{"attack_rush_ready", 700},
            ExpectedElectroClip{"attack_rush", 733},
            ExpectedElectroClip{"attack_rush_release", 733},
            ExpectedElectroClip{"attack_rush_to_idle", 366},
        };
        for (const ExpectedElectroClip& expected : expectedElectroClips) {
            const auto* clip =
                electroArchetype.animationBank.findClip(expected.name);
            assert(clip != nullptr);
            assert(clip->durationMilliseconds() ==
                   expected.durationMilliseconds);
        }
        const auto& electroEffects = levelThree.electroEffects();
        assert(electroEffects.wave.meshFile ==
               "meshes_bin/electro_wave.bdae");
        assert(electroEffects.waveBillboard.meshFile ==
               "meshes_bin/electro_wave_billboard.bdae");
        assert(electroEffects.beam.meshFile ==
               "meshes_bin/electro_beam.bdae");
        const auto& landingEffects = levelThree.enemyLandingEffects();
        assert(landingEffects.shockwave.meshFile ==
               "meshes_bin/fx_shockwave.bdae");
        assert(landingEffects.crashWall.meshFile ==
               "meshes_bin/crashwall.bdae");
        assert(!landingEffects.shockwave.mesh.sceneGeometries().empty());
        assert(!landingEffects.crashWall.mesh.sceneGeometries().empty());
        assert(landingEffects.shockwave.mesh.images().size() ==
               landingEffects.shockwave.textures.size());
        assert(landingEffects.crashWall.mesh.images().size() ==
               landingEffects.crashWall.textures.size());
        assert(electroEffects.wave.mesh.images().size() ==
               electroEffects.wave.textures.size());
        assert(electroEffects.waveBillboard.mesh.images().size() ==
               electroEffects.waveBillboard.textures.size());
        assert(electroEffects.beam.mesh.images().size() ==
               electroEffects.beam.textures.size());
        assert(electroEffects.wave.animationBank.clips().size() == 2);
        assert(electroEffects.wave.animationBank.clips()[0].name == "ring");
        assert(electroEffects.wave.animationBank.clips()[0]
                   .durationMilliseconds() == 366);
        assert(electroEffects.wave.animationBank.clips()[1].name == "wave");
        assert(electroEffects.wave.animationBank.clips()[1]
                   .durationMilliseconds() == 833);
        assert(electroEffects.waveBillboard.animationBank.clips().size() ==
               2);
        assert(electroEffects.waveBillboard.animationBank.clips()[0].name ==
               "wave");
        assert(electroEffects.waveBillboard.animationBank.clips()[0]
                   .durationMilliseconds() == 1000);
        assert(electroEffects.beam.animationBank.clips().size() == 3);
        assert(electroEffects.beam.animationBank.clips()[0].name == "fall");
        assert(electroEffects.beam.animationBank.clips()[0]
                   .durationMilliseconds() == 200);
        assert(electroEffects.beam.animationBank.clips()[1].name == "keep");
        assert(electroEffects.beam.animationBank.clips()[1]
                   .durationMilliseconds() == 633);
        assert(electroEffects.beam.animationBank.clips()[2].name ==
               "release");
        assert(electroEffects.beam.animationBank.clips()[2]
                   .durationMilliseconds() == 366);
        std::vector<usm::assets::ColladaGeometry> electroStandPose;
        assert(usm::assets::evaluateColladaPose(
            electroArchetype.mesh, electroArchetype.animationBank,
            electroStand->startMilliseconds, electroStandPose));
        assert(electroStandPose.size() == 1);
        assert(electroStandPose.front().vertices.size() ==
               electroSkin.vertexInfluences.size());
        for (const auto& vertex : electroStandPose.front().vertices) {
            assert(std::isfinite(vertex.position.x));
            assert(std::isfinite(vertex.position.y));
            assert(std::isfinite(vertex.position.z));
        }

        // CBoss::InitAiTask (0x0032b66c) dispatches Electro's six-record
        // graph only for EnemyType 7. Object 30418 is the type-6 cinematic
        // double, while object 31094 is the authored Level 3 runtime boss.
        usm::game::LevelEnemyRuntime electroRuntime;
        assert(electroRuntime.initialize(levelThree));
        assert(electroRuntime.setDiagnosticAiEnabled(30418, true));
        electroRuntime.updateGameplay(
            1, levelThreeElectro->position, nullptr);
        assert(electroRuntime.find(30418)->electroTask ==
               usm::game::ElectroBossTaskState::None);
        assert(electroRuntime.setDiagnosticAiEnabled(30418, false));
        assert(electroRuntime.setDiagnosticAiEnabled(31094, true));
        usm::assets::Vector3 electroVictim =
            levelThreeRuntimeElectro->position;
        std::vector<usm::game::EnemyPlayerHit> electroHits;
        std::vector<usm::game::EnemyProjectileEvent> electroProjectileEvents;
        const auto tickElectro =
            [&electroRuntime, &electroVictim, &electroHits,
             &electroProjectileEvents](std::uint32_t milliseconds) {
                electroRuntime.updateGameplay(milliseconds, electroVictim,
                                               nullptr);
                auto hits = electroRuntime.consumePlayerHits();
                electroHits.insert(electroHits.end(), hits.begin(),
                                   hits.end());
                auto events = electroRuntime.consumeProjectileEvents();
                electroProjectileEvents.insert(
                    electroProjectileEvents.end(), events.begin(),
                    events.end());
                (void)electroRuntime.consumeEffectCues();
            };
        tickElectro(1);
        const auto* electroBossState = electroRuntime.find(31094);
        assert(electroBossState != nullptr);
        assert(electroBossState->electroTask ==
               usm::game::ElectroBossTaskState::RangeAttack);
        assert(electroBossState->electroRangeAttacksRemaining == 3);

        // CSummonObjManage::Launch (0x003670dc) seeds lrand48 with zero,
        // yielding 334 degrees, and creates three summons 120 degrees apart
        // on the native 400 cm ring. The attack_fall special action releases
        // them at 50 percent of its exact 833 ms authored clip.
        tickElectro(415);
        tickElectro(1);
        assert(electroRuntime.thunderclaps().size() == 3);
        for (std::size_t index = 0;
             index < electroRuntime.thunderclaps().size(); ++index) {
            const auto& thunderclap = electroRuntime.thunderclaps()[index];
            assert(thunderclap.sourceObjectId == 31094);
            assert(thunderclap.phase ==
                   usm::game::EnemyThunderclapPhase::Converging);
            const float expectedAngle =
                (334.0F + static_cast<float>(index) * 120.0F) *
                3.14159265358979323846F / 180.0F;
            assert(std::abs(
                       thunderclap.position.x -
                       (electroVictim.x + std::cos(expectedAngle) * 400.0F)) <
                   0.01F);
            assert(std::abs(
                       thunderclap.position.y -
                       (electroVictim.y + std::sin(expectedAngle) * 400.0F)) <
                   0.01F);
        }
        tickElectro(1500);
        for (const auto& thunderclap : electroRuntime.thunderclaps()) {
            assert(std::abs(std::hypot(
                                thunderclap.targetPosition.x -
                                    thunderclap.position.x,
                                thunderclap.targetPosition.y -
                                    thunderclap.position.y) -
                            200.0F) < 0.05F);
        }
        tickElectro(1100);
        tickElectro(1);
        assert(std::count_if(
                   electroHits.begin(), electroHits.end(), [](const auto& hit) {
                       return hit.sourceObjectId == 31094 &&
                              hit.damage == 50.0F;
                   }) == 3);
        assert(std::count_if(
                   electroProjectileEvents.begin(),
                   electroProjectileEvents.end(), [](const auto& event) {
                       return event.sourceObjectId == 31094 &&
                              event.kind ==
                                  usm::game::EnemyProjectileEventKind::
                                      Spawned;
                   }) == 3);

        for (std::uint32_t elapsed = 0;
             elapsed < 20000 &&
             electroRuntime.find(31094)->electroSequenceIndex != 1;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::WeakStart);
        for (std::uint32_t elapsed = 0;
             elapsed < 1000 &&
             electroRuntime.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::Weak;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::Weak);
        for (std::uint32_t elapsed = 0;
             elapsed < 5000 &&
             electroRuntime.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::WeakEnd;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::WeakEnd);
        const std::size_t hitCountBeforeWeak = electroHits.size();
        const std::uint32_t weakReleaseTime =
            runtimeElectroArchetype.animationBank
                .findClip("scream")
                ->durationMilliseconds() *
            30U / 100U;
        assert(electroRuntime.find(31094)->animationTimeMilliseconds <=
               weakReleaseTime);
        tickElectro(
            weakReleaseTime -
            electroRuntime.find(31094)->animationTimeMilliseconds);
        tickElectro(1);
        assert(electroHits.size() == hitCountBeforeWeak + 1);
        assert(electroHits.back().sourceObjectId == 31094);
        assert(electroHits.back().attackId == 23);
        assert(electroHits.back().damage == 70.0F);
        assert(electroRuntime.electroBursts().size() == 1);
        assert(electroRuntime.electroBursts().front().scale == 3.0F);
        assert(std::abs(electroRuntime.electroBursts().front().position.z -
                        (electroRuntime.find(31094)->position.z +
                         electroRuntime.find(31094)->collisionHeight)) <
               0.001F);
        for (std::uint32_t elapsed = 0;
             elapsed < 5000 &&
             electroRuntime.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::RotateReady;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::RotateReady);
        tickElectro(2000);
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::Rotate);
        assert(electroRuntime.electricPosts().size() == 3);
        const auto& firstPost = electroRuntime.electricPosts()[0];
        assert(std::abs(firstPost.position.z -
                        (electroRuntime.find(31094)->position.z +
                         electroRuntime.find(31094)->collisionHeight * 0.4F)) <
               0.001F);
        assert(firstPost.damage == 50.0F);
        for (std::size_t postIndex = 0; postIndex < 3; ++postIndex) {
            const auto& post = electroRuntime.electricPosts()[postIndex];
            const auto& next =
                electroRuntime.electricPosts()[(postIndex + 1) % 3];
            const float separationDot =
                post.facing.x * next.facing.x +
                post.facing.y * next.facing.y;
            assert(std::abs(separationDot + 0.5F) < 0.001F);
        }
        const auto rotateStartFacing = electroRuntime.find(31094)->facing;
        const std::size_t hitCountBeforePosts = electroHits.size();
        tickElectro(1000);
        assert(electroHits.size() == hitCountBeforePosts + 3);
        assert(std::all_of(
            electroRuntime.electricPosts().begin(),
            electroRuntime.electricPosts().end(), [](const auto& post) {
                return post.hitPlayer && post.animationTimeMilliseconds == 1000;
            }));
        const auto rotateOneSecondFacing =
            electroRuntime.find(31094)->facing;
        const float rotateDot =
            rotateStartFacing.x * rotateOneSecondFacing.x +
            rotateStartFacing.y * rotateOneSecondFacing.y;
        const float rotateCross =
            rotateStartFacing.x * rotateOneSecondFacing.y -
            rotateStartFacing.y * rotateOneSecondFacing.x;
        assert(std::abs(rotateDot - std::cos(54.0F *
                                            3.14159265358979323846F /
                                            180.0F)) < 0.001F);
        assert(std::abs(rotateCross - std::sin(54.0F *
                                              3.14159265358979323846F /
                                              180.0F)) < 0.001F);
        for (std::uint32_t elapsed = 0;
             elapsed < 9000 &&
             electroRuntime.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::RotateEnd;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::RotateEnd);
        assert(electroRuntime.electricPosts().empty());

        // Continue through the second weak record to the native dash task.
        electroVictim = electroRuntime.find(31094)->position;
        electroVictim.x += 1000.0F;
        for (std::uint32_t elapsed = 0;
             elapsed < 12000 &&
             electroRuntime.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::DashReady;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::DashReady);
        assert(electroRuntime.find(31094)->electroDashesRemaining == 3);
        for (std::uint32_t elapsed = 0;
             elapsed < 1000 &&
             electroRuntime.find(31094)->electroTask !=
                 usm::game::ElectroBossTaskState::DashRush;
             elapsed += 25) {
            tickElectro(25);
        }
        assert(electroRuntime.find(31094)->electroTask ==
               usm::game::ElectroBossTaskState::DashRush);
        const auto dashStart = electroRuntime.find(31094)->position;
        tickElectro(100);
        const auto dashAfterOneTenth = electroRuntime.find(31094)->position;
        assert(std::abs(std::hypot(dashAfterOneTenth.x - dashStart.x,
                                   dashAfterOneTenth.y - dashStart.y) -
                        250.0F) < 0.05F);
        electroVictim = dashAfterOneTenth;
        const std::size_t hitCountBeforeDash = electroHits.size();
        tickElectro(10);
        assert(electroHits.size() == hitCountBeforeDash + 1);
        assert(electroHits.back().attackId == 24);
        assert(electroHits.back().damage == 70.0F);
        assert(!electroRuntime.electroBursts().empty());
        assert(electroRuntime.electroBursts().back().scale == 1.0F);

        // CBoss::ParseLocalAiMessage (0x0032dea8) clamps both thresholds,
        // proving that a large hit cannot skip phase one or phase two.
        usm::game::LevelEnemyRuntime electroPhaseRuntime;
        assert(electroPhaseRuntime.initialize(levelThree));
        assert(electroPhaseRuntime.setDiagnosticAiEnabled(31094, true));
        const float electroMaximumHealth =
            electroPhaseRuntime.find(31094)->maximumHealth;
        assert(electroPhaseRuntime.applyDiagnosticDamage(
            31094, electroMaximumHealth));
        assert(electroPhaseRuntime.find(31094)->electroPhase == 1);
        assert(std::abs(electroPhaseRuntime.find(31094)->health -
                        electroMaximumHealth * 0.66F) < 0.01F);
        assert(electroPhaseRuntime.applyDiagnosticDamage(
            31094, electroMaximumHealth));
        assert(electroPhaseRuntime.find(31094)->electroPhase == 2);
        assert(std::abs(electroPhaseRuntime.find(31094)->health -
                        electroMaximumHealth * 0.33F) < 0.01F);
        assert(electroPhaseRuntime.applyDiagnosticDamage(
            31094, electroMaximumHealth));
        assert(electroPhaseRuntime.find(31094)->health == 0.0F);
        assert(electroPhaseRuntime.find(31094)->behavior ==
               usm::game::EnemyBehaviorState::Dead);

        const auto findLevelThreeObject = [&levelThree](std::int32_t objectId) {
            return std::find_if(
                levelThree.objects().begin(), levelThree.objects().end(),
                [objectId](const auto& object) {
                    return object.objectId == objectId;
                });
        };
        const auto movingPlatform = findLevelThreeObject(40061);
        assert(movingPlatform != levelThree.objects().end());
        assert(movingPlatform->kind ==
               usm::game::LevelObjectKind::Platform);
        assert(movingPlatform->roomId == 6);
        assert(std::abs(movingPlatform->position.x - 3871.64F) < 0.02F);
        assert(std::abs(movingPlatform->position.y + 5508.37F) < 0.02F);
        assert(std::abs(movingPlatform->position.z - 1882.22F) < 0.02F);
        assert(movingPlatform->platformParkDurationMilliseconds == 0.0F);
        assert(std::abs(
                   movingPlatform->platformLineSpeedCentimetersPerMillisecond -
                   0.18F) < 0.0001F);
        assert(movingPlatform->platformInitiallyActive);
        assert(!movingPlatform->platformActiveForever);
        assert(movingPlatform->platformLinkedWaypointId == 40276);
        assert(movingPlatform->hasCollisionBounds);
        assert(movingPlatform->hasCollision);
        const auto& platformArchetype = levelThree.objectArchetypes()[
            movingPlatform->archetypeIndex];
        assert(platformArchetype.meshFile == "meshes_bin/platform.bdae");
        assert(platformArchetype.physicsMeshFile ==
               "meshes_bin/platform_phy.bdae");
        assert(!platformArchetype.physicsMesh.sceneGeometries().empty());

        usm::game::LevelObjectRuntime platformRuntime;
        assert(platformRuntime.initialize(levelThree));
        const auto* movingPlatformState = platformRuntime.find(40061);
        assert(movingPlatformState != nullptr);
        assert(movingPlatformState->physicsEnabled);
        assert(movingPlatformState->collisionEnabled);
        assert(movingPlatformState->platformMotionActive);
        assert(movingPlatformState->platformMotionState ==
               usm::game::PlatformMotionState::Park);
        assert(movingPlatformState->platformTargetWaypointId == 40276);
        const auto movingPlatformStart = movingPlatformState->position;
        const auto firstMovingPlatformWaypoint = std::find_if(
            levelThree.waypoints().begin(), levelThree.waypoints().end(),
            [](const auto& waypoint) { return waypoint.objectId == 40276; });
        assert(firstMovingPlatformWaypoint != levelThree.waypoints().end());
        usm::game::LevelCollision platformCollision;
        assert(platformCollision.build(levelThree.rooms()));
        assert(platformCollision.updateObjectColliders(
            platformRuntime.states()));
        float platformTop = 0.0F;
        std::int32_t platformSupportId = -1;
        assert(platformCollision.groundHeight(
            {movingPlatformStart.x, movingPlatformStart.y,
             movingPlatformStart.z + 500.0F},
            0.0F, 1000.0F, platformTop, 0U, &platformSupportId));
        assert(platformSupportId == 40061);
        const usm::assets::Vector3 platformSupportedPoint{
            movingPlatformStart.x, movingPlatformStart.y, platformTop};
        const auto platformSupportPose =
            platformRuntime.supportPose(platformSupportId);
        assert(platformSupportPose.has_value());
        platformRuntime.advanceAnimations(50);
        movingPlatformState = platformRuntime.find(40061);
        const float platformDeltaX =
            movingPlatformState->position.x - movingPlatformStart.x;
        const float platformDeltaY =
            movingPlatformState->position.y - movingPlatformStart.y;
        const float platformDeltaZ =
            movingPlatformState->position.z - movingPlatformStart.z;
        const float platformTravel = std::sqrt(
            platformDeltaX * platformDeltaX +
            platformDeltaY * platformDeltaY +
            platformDeltaZ * platformDeltaZ);
        const float platformFirstLegDistance = std::sqrt(
            std::pow(firstMovingPlatformWaypoint->position.x -
                         movingPlatformStart.x,
                     2.0F) +
            std::pow(firstMovingPlatformWaypoint->position.y -
                         movingPlatformStart.y,
                     2.0F) +
            std::pow(firstMovingPlatformWaypoint->position.z -
                         movingPlatformStart.z,
                     2.0F));
        assert(std::abs(platformTravel -
                        platformFirstLegDistance * (50.0F / 2500.0F)) <
               0.02F);
        const auto platformCarry = platformRuntime.supportMotionDelta(
            *platformSupportPose, platformSupportedPoint);
        assert(std::abs(std::sqrt(platformCarry.x * platformCarry.x +
                                  platformCarry.y * platformCarry.y +
                                  platformCarry.z * platformCarry.z) -
                        platformTravel) < 0.02F);

        const auto movingElectricPlatform = findLevelThreeObject(40627);
        assert(movingElectricPlatform != levelThree.objects().end());
        assert(movingElectricPlatform->kind ==
               usm::game::LevelObjectKind::ElectricPlatform);
        assert(movingElectricPlatform->roomId == 7);
        assert(std::abs(movingElectricPlatform->position.x - 6871.068359F) <
               0.01F);
        assert(std::abs(movingElectricPlatform->position.y + 1299.951172F) <
               0.01F);
        assert(std::abs(movingElectricPlatform->position.z - 2071.206787F) <
               0.01F);
        assert(movingElectricPlatform->electricOffDurationMilliseconds ==
               5000.0F);
        assert(movingElectricPlatform->electricOnDurationMilliseconds ==
               2000.0F);
        assert(movingElectricPlatform->electricReadyDurationMilliseconds ==
               2000.0F);
        assert(movingElectricPlatform->electricDelayMilliseconds == 0.0F);
        assert(movingElectricPlatform->electricDamage == 100.0F);
        assert(!movingElectricPlatform->electricInitiallyActive);
        assert(movingElectricPlatform->electricInitialState == 1);
        assert(movingElectricPlatform->platformParkDurationMilliseconds ==
               1000.0F);
        assert(movingElectricPlatform
                   ->platformLineSpeedCentimetersPerMillisecond == 0.3F);
        assert(!movingElectricPlatform->platformInitiallyActive);
        assert(movingElectricPlatform->platformActiveForever);
        assert(movingElectricPlatform->platformLinkedWaypointId == 30802);
        assert(movingElectricPlatform->hasCollisionBounds);
        assert(movingElectricPlatform->hasCollision);
        const auto delayedElectricPlatform = findLevelThreeObject(30831);
        assert(delayedElectricPlatform != levelThree.objects().end());
        assert(delayedElectricPlatform->electricDelayMilliseconds == 4500.0F);
        assert(delayedElectricPlatform->electricDamage == 50.0F);
        assert(delayedElectricPlatform->platformLinkedWaypointId == 30832);
        const auto staticElectricPlatform = findLevelThreeObject(40623);
        assert(staticElectricPlatform != levelThree.objects().end());
        assert(staticElectricPlatform->platformLinkedWaypointId == -1);
        assert(staticElectricPlatform->platformInitiallyActive);
        assert(staticElectricPlatform->electricInitialState == 0);
        assert(staticElectricPlatform->electricDamage == 50.0F);
        const auto& electricArchetype = levelThree.objectArchetypes()[
            movingElectricPlatform->archetypeIndex];
        assert(electricArchetype.meshFile ==
               "meshes_bin/electric_platform_mesh.bdae");
        assert(electricArchetype.additiveTextureNameFragment == "electric_wr");
        std::size_t electricAdditiveMaterialCount = 0;
        for (const usm::assets::ColladaMaterial& material :
             electricArchetype.mesh.materials()) {
            if (!material.diffuseImageIndex) {
                continue;
            }
            const auto& image = electricArchetype.mesh.images()[
                *material.diffuseImageIndex];
            if (image.sourcePath.find(
                    electricArchetype.additiveTextureNameFragment) !=
                std::string::npos) {
                ++electricAdditiveMaterialCount;
                // This additive selection is the post-load ARM override at
                // 0x003853fc/0x00373838, not a serialized BDAE material bit.
                assert(!material.additiveBlend);
            }
        }
        assert(electricAdditiveMaterialCount == 2);
        assert(electricArchetype.physicsMeshFile ==
               "meshes_bin/platform_phy.bdae");
        assert(!electricArchetype.physicsMesh.sceneGeometries().empty());
        assert(electricArchetype.animationBank.findClip("release") != nullptr);
        assert(electricArchetype.animationBank.findClip("off") != nullptr);
        assert(electricArchetype.animationBank.findClip("warning") != nullptr);

        usm::game::LevelObjectRuntime electricRuntime;
        assert(electricRuntime.initialize(levelThree));
        assert(electricRuntime.states().size() == 202);
        const auto* movingElectricState = electricRuntime.find(40627);
        assert(movingElectricState != nullptr);
        assert(movingElectricState->electricSwitchActive);
        assert(movingElectricState->electricState ==
               usm::game::ElectricPlatformState::Off);
        assert(movingElectricState->activeAnimation == "off");
        assert(movingElectricState->electricStateElapsedMilliseconds == 0.0F);
        assert(movingElectricState->platformMotionState ==
               usm::game::PlatformMotionState::Park);
        assert(!movingElectricState->platformMotionActive);
        assert(movingElectricState->platformTargetWaypointId == 30802);
        const auto* delayedElectricState = electricRuntime.find(30831);
        assert(delayedElectricState != nullptr);
        assert(delayedElectricState->electricStateElapsedMilliseconds ==
               4500.0F);
        electricRuntime.advanceAnimations(499);
        assert(electricRuntime.find(30831)->electricState ==
               usm::game::ElectricPlatformState::Off);
        electricRuntime.advanceAnimations(1);
        assert(electricRuntime.find(30831)->electricState ==
               usm::game::ElectricPlatformState::Warning);
        assert(electricRuntime.find(30831)->activeAnimation == "warning");

        usm::game::LevelObjectRuntime electricCycleRuntime;
        assert(electricCycleRuntime.initialize(levelThree));
        electricCycleRuntime.advanceAnimations(4999);
        assert(electricCycleRuntime.find(40627)->electricState ==
               usm::game::ElectricPlatformState::Off);
        electricCycleRuntime.advanceAnimations(1);
        assert(electricCycleRuntime.find(40627)->electricState ==
               usm::game::ElectricPlatformState::Warning);
        electricCycleRuntime.advanceAnimations(1999);
        assert(electricCycleRuntime.find(40627)->electricState ==
               usm::game::ElectricPlatformState::Warning);
        electricCycleRuntime.advanceAnimations(1);
        assert(electricCycleRuntime.find(40627)->electricState ==
               usm::game::ElectricPlatformState::Release);
        electricCycleRuntime.advanceAnimations(1999);
        assert(electricCycleRuntime.find(40627)->electricState ==
               usm::game::ElectricPlatformState::Release);
        electricCycleRuntime.advanceAnimations(1);
        assert(electricCycleRuntime.find(40627)->electricState ==
               usm::game::ElectricPlatformState::Off);

        usm::game::CinematicThread platformThread;
        platformThread.objectId = 40627;
        usm::game::CinematicCommand enablePlatform;
        enablePlatform.name = "EnableAI";
        assert(electricCycleRuntime.applyCinematicCommand(
            levelThree, platformThread, enablePlatform));
        assert(electricCycleRuntime.find(40627)->platformMotionActive);
        assert(electricCycleRuntime.find(40627)->platformMotionState ==
               usm::game::PlatformMotionState::Move);
        electricCycleRuntime.advanceAnimations(100);
        const auto firstPlatformWaypoint = std::find_if(
            levelThree.waypoints().begin(), levelThree.waypoints().end(),
            [](const auto& waypoint) { return waypoint.objectId == 30802; });
        assert(firstPlatformWaypoint != levelThree.waypoints().end());
        assert(std::abs(electricCycleRuntime.find(40627)->position.x -
                        firstPlatformWaypoint->position.x) < 0.01F);
        assert(std::abs(electricCycleRuntime.find(40627)->position.y -
                        firstPlatformWaypoint->position.y) < 0.01F);
        assert(std::abs(electricCycleRuntime.find(40627)->position.z -
                        firstPlatformWaypoint->position.z) < 0.01F);
        assert(electricCycleRuntime.find(40627)->platformMotionState ==
               usm::game::PlatformMotionState::Park);
        assert(electricCycleRuntime.find(40627)->platformTargetWaypointId ==
               30804);

        usm::game::LevelObjectRuntime electricContactRuntime;
        assert(electricContactRuntime.initialize(levelThree));
        electricContactRuntime.updateElectricPlatformContacts(
            staticElectricPlatform->position, 50);
        auto electricDamageEvents =
            electricContactRuntime.consumeElectricPlatformDamageEvents();
        assert(electricDamageEvents.size() == 1);
        assert(electricDamageEvents.front().objectId == 40623);
        assert(electricDamageEvents.front().roomId == 6);
        assert(electricDamageEvents.front().damage == 50.0F);
        assert(electricDamageEvents.front().damageType == 0xcb);
        assert(electricDamageEvents.front().effectId == 0x96);
        assert(electricContactRuntime.electricContactCooldownMilliseconds() ==
               2000);
        electricContactRuntime.updateElectricPlatformContacts(
            staticElectricPlatform->position, 1000);
        assert(electricContactRuntime.consumeElectricPlatformDamageEvents()
                   .empty());
        assert(electricContactRuntime.electricContactCooldownMilliseconds() ==
               1000);
        electricContactRuntime.updateElectricPlatformContacts(
            staticElectricPlatform->position, 1000);
        assert(electricContactRuntime.consumeElectricPlatformDamageEvents()
                   .empty());
        assert(electricContactRuntime.electricContactCooldownMilliseconds() ==
               0);
        electricContactRuntime.updateElectricPlatformContacts(
            staticElectricPlatform->position, 50);
        assert(electricContactRuntime.consumeElectricPlatformDamageEvents()
                   .size() == 1);

        usm::game::LevelObjectRuntime inactiveElectricContactRuntime;
        assert(inactiveElectricContactRuntime.initialize(levelThree));
        inactiveElectricContactRuntime.updateElectricPlatformContacts(
            movingElectricPlatform->position, 50);
        assert(inactiveElectricContactRuntime
                   .consumeElectricPlatformDamageEvents()
                   .empty());

        // Every shipped level must remain directly bootstrappable by the
        // process-local census harness. These later assets exercise native
        // virtual-filesystem rebasing, optional collision/nav nodes,
        // camera-only cinematics, and valid levels with no Bonus nodes.
        struct ExpectedThugPresentation {
            std::string_view gameType;
            std::string_view meshFile;
            std::size_t instanceCount{};
        };
        constexpr std::array expectedThugPresentations{
            ExpectedThugPresentation{
                "MeleeThug_gun",
                "../entities/meshes_bin/thug_gun_mesh.bdae", 16},
            ExpectedThugPresentation{
                "MeleeThugEnemy_bat",
                "../entities/meshes_bin/thug_bat_mesh.bdae", 46},
            ExpectedThugPresentation{
                "MeleeThugEnemy_electrodes",
                "../entities/meshes_bin/thug_electric_mesh.bdae", 18},
            ExpectedThugPresentation{
                "MeleeThugEnemy_knife",
                "../entities/meshes_bin/thug_knife_mesh.bdae", 46},
            ExpectedThugPresentation{
                "RangeThug_big",
                "../entities/meshes_bin/thug_big_mesh.bdae", 11},
            ExpectedThugPresentation{
                "RangeThug_hammer",
                "../entities/meshes_bin/thug_hammer_mesh.bdae", 11},
            ExpectedThugPresentation{
                "RangeThug_molotov",
                "../entities/meshes_bin/thug_molotov_mesh.bdae", 30},
        };
        std::array<std::size_t, expectedThugPresentations.size()>
            thugPresentationCounts{};
        const auto censusThugPresentations =
            [&expectedThugPresentations, &thugPresentationCounts](
                const usm::game::LevelOneBootstrap& level) {
                for (const auto& enemy : level.enemies()) {
                    const auto expected = std::find_if(
                        expectedThugPresentations.begin(),
                        expectedThugPresentations.end(),
                        [&enemy](const auto& presentation) {
                            return presentation.gameType == enemy.gameType;
                        });
                    if (enemy.gameType.find("Thug") == std::string::npos) {
                        assert(expected == expectedThugPresentations.end());
                        continue;
                    }
                    assert(expected != expectedThugPresentations.end());
                    assert(enemy.archetypeIndex <
                           level.enemyArchetypes().size());
                    const auto& archetype =
                        level.enemyArchetypes()[enemy.archetypeIndex];
                    assert(archetype.meshFile == expected->meshFile);
                    ++thugPresentationCounts[static_cast<std::size_t>(
                        std::distance(expectedThugPresentations.begin(),
                                      expected))];
                }
            };
        censusThugPresentations(bootstrap);
        censusThugPresentations(levelTwo);
        censusThugPresentations(levelThree);
        constexpr std::array<std::size_t, 9> laterLevelRoomCounts{
            8, 9, 8, 23, 7, 8, 4, 15, 12};
        for (std::uint32_t levelNumber = 4; levelNumber <= 12;
             ++levelNumber) {
            usm::game::LevelOneBootstrap laterLevel;
            const usm::Result laterLevelResult =
                laterLevel.load(dataRoot, levelNumber);
            if (!laterLevelResult) {
                std::cerr << "Level " << levelNumber
                          << " bootstrap failed: "
                          << laterLevelResult.message() << '\n';
                return 1;
            }
            assert(laterLevel.levelNumber() == levelNumber);
            assert(laterLevel.rooms().size() ==
                   laterLevelRoomCounts[levelNumber - 4]);
            assert(!laterLevel.introSky().geometry.geometries().empty());
            censusThugPresentations(laterLevel);
            const auto assertNativeBoss =
                [&laterLevel](std::int32_t objectId,
                              std::int16_t authoredType) {
                    const auto boss = std::find_if(
                        laterLevel.enemies().begin(),
                        laterLevel.enemies().end(),
                        [objectId](const auto& enemy) {
                            return enemy.objectId == objectId;
                        });
                    assert(boss != laterLevel.enemies().end());
                    assert(boss->enemyTypeId == authoredType);
                };
            if (levelNumber == 4) {
                assertNativeBoss(30773, 7);
                assertNativeBoss(30709, 7);
            } else if (levelNumber == 6) {
                assertNativeBoss(41460, 13);
                assert(std::count_if(
                           laterLevel.objects().begin(),
                           laterLevel.objects().end(), [](const auto& object) {
                               return object.kind ==
                                      usm::game::LevelObjectKind::AreaDamage;
                           }) == 47);
                const auto symbioteBomb = std::find_if(
                    laterLevel.objects().begin(), laterLevel.objects().end(),
                    [](const auto& object) {
                        return object.objectId == 41465;
                    });
                assert(symbioteBomb != laterLevel.objects().end());
                assert(symbioteBomb->kind ==
                       usm::game::LevelObjectKind::AreaDamage);
                assert(symbioteBomb->initialAnimation == "idle");
                assert(symbioteBomb->damage == 30.0F &&
                       symbioteBomb->areaDamageType == 0 &&
                       symbioteBomb->areaDamageIgnorePhysics &&
                       !symbioteBomb->areaDamageActiveForever &&
                       symbioteBomb->hasCollisionBounds);
                {
                    usm::game::LevelObjectRuntime areaDamage;
                    assert(areaDamage.initialize(laterLevel));
                    const auto* bomb = areaDamage.find(41465);
                    assert(bomb != nullptr && bomb->areaDamageState == -1);
                    areaDamage.updateAreaDamage(symbioteBomb->position, 50);
                    assert(bomb->areaDamageState == 0);
                    areaDamage.updateAreaDamage(symbioteBomb->position, 50);
                    const auto events = areaDamage.consumeAreaDamageEvents();
                    assert(events.size() == 1);
                    assert(events.front().objectId == 41465 &&
                           events.front().damage == 30.0F &&
                           events.front().hitType == 0xcc);
                    areaDamage.updateAreaDamage(symbioteBomb->position, 50);
                    assert(areaDamage.consumeAreaDamageEvents().empty());
                    areaDamage.resetTransientForCheckPointLoad();
                    assert(bomb->areaDamageState == -1 &&
                           bomb->areaDamageContactCooldownMilliseconds == 0);
                }
                {
                    usm::game::LevelObjectRuntime bridges;
                    assert(bridges.initialize(laterLevel));
                    constexpr std::array<std::pair<std::int32_t, std::size_t>, 5>
                        nativeBridgeCarCounts{{{40902, 5},
                                               {40897, 5},
                                               {40908, 4},
                                               {40903, 6},
                                               {40907, 4}}};
                    std::size_t slideCarCount = 0;
                    std::size_t linkedSlideCarCount = 0;
                    for (const auto& car : bridges.states()) {
                        if (car.asset == nullptr ||
                            car.asset->kind !=
                                usm::game::LevelObjectKind::SlideCar) {
                            continue;
                        }
                        ++slideCarCount;
                        if (car.slideCarBridgeObjectId >= 0) {
                            ++linkedSlideCarCount;
                            const auto expected = std::find_if(
                                nativeBridgeCarCounts.begin(),
                                nativeBridgeCarCounts.end(),
                                [&car](const auto& value) {
                                    return value.first ==
                                           car.slideCarBridgeObjectId;
                                });
                            assert(expected != nativeBridgeCarCounts.end());
                            const auto* owner =
                                bridges.find(car.slideCarBridgeObjectId);
                            assert(owner != nullptr && owner->asset != nullptr);
                            const float halfHeight = std::abs(
                                owner->asset->collisionLocalMaximum.z -
                                owner->asset->collisionLocalMinimum.z) *
                                0.5F;
                            assert(std::abs(
                                car.position.z - owner->position.z -
                                halfHeight) < 0.1F);
                        }
                    }
                    assert(slideCarCount == 25);
                    assert(linkedSlideCarCount == 24);
                    for (const auto& [bridgeId, expectedCount] :
                         nativeBridgeCarCounts) {
                        assert(std::count_if(
                                   bridges.states().begin(),
                                   bridges.states().end(),
                                   [bridgeId](const auto& car) {
                                       return car.slideCarBridgeObjectId ==
                                              bridgeId;
                                   }) == expectedCount);
                    }
                    const auto* roomOnePoliceCar = bridges.find(41315);
                    assert(roomOnePoliceCar != nullptr &&
                           roomOnePoliceCar->slideCarBridgeObjectId == -1);
                    {
                        usm::game::LevelObjectRuntime carrying;
                        assert(carrying.initialize(laterLevel));
                        const auto* car = carrying.find(41030);
                        const auto* owner = carrying.find(40897);
                        assert(car != nullptr && owner != nullptr &&
                               car->slideCarBridgeObjectId == 40897);
                        usm::game::LevelCollision carryingCollision;
                        assert(carryingCollision.updateObjectColliders(
                            carrying.states()));
                        float carTop = 0.0F;
                        std::int32_t supportObjectId = -1;
                        assert(carryingCollision.groundHeight(
                            {car->position.x, car->position.y,
                             car->position.z + 500.0F},
                            0.0F, 1000.0F, carTop, 0U,
                            &supportObjectId));
                        assert(supportObjectId == 41030);
                        const usm::assets::Vector3 supportedPoint{
                            car->position.x, car->position.y, carTop};
                        const auto supportPose =
                            carrying.supportPose(supportObjectId);
                        assert(supportPose.has_value());
                        carrying.updateBrokenBridges(
                            {car->position.x, car->position.y, carTop}, 50);
                        assert(owner->bridgeState == 2);
                        for (std::uint32_t tick = 0;
                             tick < 200 && owner->bridgeState < 4; ++tick) {
                            carrying.updateBrokenBridges(
                                {0.0F, 0.0F, 0.0F}, 50);
                        }
                        assert(owner->bridgeState == 4);
                        const auto carry = carrying.supportMotionDelta(
                            *supportPose, supportedPoint);
                        assert(std::abs(carry.z) > 0.001F);
                    }
                    std::size_t bridgeCount = 0;
                    for (const auto& asset : laterLevel.objects()) {
                        if (asset.kind != usm::game::LevelObjectKind::BrokenBridge) {
                            continue;
                        }
                        ++bridgeCount;
                        assert(asset.hasCollisionBounds && asset.hasCollision);
                        assert(asset.initialAnimation == "idle");
                        const auto& bank = laterLevel.objectArchetypes()[asset.archetypeIndex].animationBank;
                        assert(bank.findClip("idle") && bank.findClip("shake"));
                        const auto* bridge = bridges.find(asset.objectId);
                        assert(bridge && bridge->visible && bridge->physicsEnabled && bridge->bridgeState == 1);
                        usm::game::LevelCollision bridgeCollision;
                        assert(bridgeCollision.updateObjectColliders(bridges.states()));
                        const float top = asset.position.z + asset.collisionLocalMaximum.z;
                        float support = 0.0F;
                        assert(bridgeCollision.groundHeight({asset.position.x, asset.position.y, top + 10.0F}, 20.0F, 20.0F, support));
                        assert(std::abs(support - top) < 0.1F);
                        bridges.updateBrokenBridges({0.0F, 0.0F, 0.0F}, 5000);
                        assert(bridge->bridgeState == 1);
                        bridges.updateBrokenBridges({asset.position.x, asset.position.y, top - 0.1F}, 50);
                        assert(bridge->bridgeState == (asset.bridgeType == 2 ? 10 : 2));
                        bool sawFirstDrop = false;
                        bool sawSecondDrop = false;
                        bool sawLinkedCarLaunch = false;
                        for (std::uint32_t tick = 0; tick < 2000 && bridge->visible; ++tick) {
                            bridges.updateBrokenBridges({0.0F, 0.0F, 0.0F}, 50);
                            sawFirstDrop = sawFirstDrop || bridge->bridgeState == 4;
                            sawSecondDrop = sawSecondDrop || bridge->bridgeState == 7;
                            sawLinkedCarLaunch = sawLinkedCarLaunch ||
                                std::any_of(
                                    bridges.states().begin(),
                                    bridges.states().end(),
                                    [&asset](const auto& car) {
                                        return car.slideCarBridgeObjectId ==
                                                   asset.objectId &&
                                               car.slideCarState >= 2;
                                    });
                        }
                        assert(!bridge->visible && !bridge->collisionEnabled && bridge->bridgeState == 11);
                        assert(bridge->position.z < asset.position.z - 3900.0F);
                        assert(sawFirstDrop == (asset.bridgeType != 2));
                        assert(sawSecondDrop == (asset.bridgeType == 1));
                        const auto carBearingBridge = std::find_if(
                            nativeBridgeCarCounts.begin(),
                            nativeBridgeCarCounts.end(),
                            [&asset](const auto& value) {
                                return value.first == asset.objectId;
                            });
                        assert(sawLinkedCarLaunch ==
                               (carBearingBridge !=
                                nativeBridgeCarCounts.end() &&
                                asset.bridgeType != 2));
                        assert(!bridges.consumeEvents().empty());
                        bridges.resetTransientForCheckPointLoad();
                        assert(bridge->bridgeState == 1 && bridge->visible && bridge->collisionEnabled);
                        assert(bridge->position.z == asset.position.z);
                        for (const auto& car : bridges.states()) {
                            if (car.slideCarBridgeObjectId == asset.objectId) {
                                assert(car.slideCarState == 0 && car.visible &&
                                       car.collisionEnabled);
                            }
                        }
                    }
                    assert(bridgeCount == 19);
                }
                usm::game::LevelCollision wallEnemyCollision;
                assert(wallEnemyCollision.build(laterLevel.rooms()));
                usm::game::LevelEnemyRuntime wallEnemies;
                assert(wallEnemies.initialize(laterLevel));
                const auto wallReveal = std::find_if(
                    laterLevel.cinematics().begin(),
                    laterLevel.cinematics().end(), [](const auto& value) {
                        return value.objectId == 112;
                    });
                assert(wallReveal != laterLevel.cinematics().end());
                for (const auto& thread : wallReveal->script.threads()) {
                    for (const auto& command : thread.commands) {
                        if (command.name == "SetVisible" ||
                            command.name == "EnableAI") {
                            assert(wallEnemies.applyCinematicCommand(
                                laterLevel, thread, command));
                        }
                    }
                }
                constexpr std::array<std::int32_t, 6> wallIds{
                    41354, 41355, 41356, 41357, 41358, 41359};
                for (const auto id : wallIds) {
                    const auto* enemy = wallEnemies.find(id);
                    assert(enemy != nullptr && enemy->asset->onWall &&
                           enemy->onWall && enemy->visible);
                    assert(!enemy->asset->inAir && !enemy->asset->immobile);
                    const auto* attributes = laterLevel.enemyAttributeConfigs()
                                                 .find(enemy->asset->enemyTypeId);
                    assert(attributes != nullptr && attributes->canMoveOnWall());
                    assert(attributes->wallSpeedCentimetersPerMillisecond == 0.3F);
                    assert(attributes->maximumMeleeAttackDistance == 300.0F);
                }
                const usm::assets::Vector3 streetPlayer{28570.0F, 13642.0F, 1225.0F};
                for (std::uint32_t frame = 0; frame < 100; ++frame) {
                    wallEnemies.updateGameplay(50, streetPlayer, &wallEnemyCollision);
                }
                for (const auto id : {41322, 41324}) {
                    const auto* stagedEnemy = wallEnemies.find(id);
                    assert(stagedEnemy != nullptr && stagedEnemy->grounded);
                    assert(stagedEnemy->position.z > 1300.0F);
                    assert(stagedEnemy->position.z < 1400.0F);
                }
                for (const auto id : wallIds) {
                    const auto* enemy = wallEnemies.find(id);
                    assert(!enemy->grounded);
                    assert(enemy->position.z >= enemy->asset->position.z - 0.01F);
                    assert(enemy->activeAnimation == "wall_idle" ||
                           enemy->activeAnimation == "wall_walk_up");
                    if (id == 41356) {
                        // Authored spawn is beside a hole in the climbable
                        // mesh. CheckWall misses; EnvironmentCheck supplies
                        // the axis once the player climbs (tested below).
                        assert(!enemy->wallAttached);
                        continue;
                    }
                    assert(enemy->wallAttached);
                    assert(std::abs(enemy->wallNormal.y + 1.0F) < 0.01F);
                    // Mesh rests on the authored wall; the capsule is one
                    // radius in front, not sunk into the wall or on the floor.
                    assert(std::abs(enemy->worldTransform[13] -
                        enemy->position.y - enemy->collisionRadius) < 0.01F);
                }
                assert(wallEnemies.find(41357)->position.z >
                       wallEnemies.find(41357)->asset->position.z + 100.0F);
                assert(wallEnemies.find(41355)->position.z ==
                       wallEnemies.find(41355)->asset->position.z);
                wallEnemies.updateGameplay(50,
                    {29471.0F, 14199.0F, 2000.0F}, &wallEnemyCollision,
                    false, {0.0F, 1.0F, 0.0F}, true);
                assert(wallEnemies.find(41356)->wallAttached);
                assert(wallEnemies.find(41356)->wallNormal.y == -1.0F);
                auto* hurtWallEnemy = wallEnemies.find(41355);
                {
                    auto webEnemies = wallEnemies;
                    const auto* target = webEnemies.find(41355);
                    const auto before = target->position;
                    const float health = target->health;
                    assert(webEnemies.canEnterWallWeb(41355));
                    assert(webEnemies.applyWallWebEvent({usm::game::WallWebEventKind::Capture, 41355}));
                    assert(target->wallWebCaptured && !webEnemies.canEnterWallWeb(41355));
                    assert(target->worldTransform[12] == target->position.x &&
                           target->worldTransform[13] == target->position.y);
                    assert(!webEnemies.applyPlayerTargetedHitDetailed(41355, 100.0F));
                    assert(webEnemies.applyWallWebEvent({usm::game::WallWebEventKind::Hold, 41355}));
                    assert(target->activeAnimation == "wall_be_drag");
                    webEnemies.updateGameplay(1000, before, &wallEnemyCollision, false,
                                              {0.0F, 1.0F, 0.0F}, true);
                    assert(target->position.x == before.x && target->position.y == before.y &&
                           target->position.z == before.z && target->health == health);
                    for (const auto name : {"Bip01_Spine1", "Bip01_R_Foot", "Bip01_L_Foot",
                         "Bip01_L_Forearm", "Bip01_Head", "Bip01_R_Forearm"}) {
                        assert(webEnemies.nodeWorldPosition(41355, name));
                    }
                    assert(!webEnemies.nodeWorldPosition(41355, "missing_bone"));
                    assert(webEnemies.applyWallWebEvent({usm::game::WallWebEventKind::Release, 41355, false}));
                    assert(!target->wallWebCaptured && target->health == health);
                    assert(std::abs(target->worldTransform[13] - target->position.y -
                                    target->collisionRadius) < 0.1F);
                    assert(target->activeAnimation == "wall_idle");
                    assert(webEnemies.applyWallWebEvent({usm::game::WallWebEventKind::Capture, 41355}));
                    assert(webEnemies.applyWallWebEvent({usm::game::WallWebEventKind::Release, 41355, true}));
                    assert(target->health == 0.0F && target->activeAnimation == "wall_death");
                    assert(!target->wallWebCaptured && target->physicsActive);
                }
                const float wallStartHeight = hurtWallEnemy->position.z;
                assert(wallEnemies.applyPlayerTargetedHitDetailed(41355, 1.0F));
                assert(hurtWallEnemy->activeAnimation == "wall_hurt");
                assert(wallEnemies.applyPlayerTargetedHitDetailed(41355, 10000.0F));
                assert(hurtWallEnemy->activeAnimation == "wall_death");
                assert(hurtWallEnemy->physicsActive);
                wallEnemies.updateGameplay(100, streetPlayer, &wallEnemyCollision);
                assert(hurtWallEnemy->position.z < wallStartHeight);

                // Native wall attacks select +Z, -Z, local -X, local +X
                // and test in that wall plane, rather than using an XY pie.
                const std::array<usm::assets::Vector3, 4> wallAttackOffsets{{
                    {0.0F, 0.0F, 150.0F}, {0.0F, 0.0F, -150.0F},
                    {150.0F, 0.0F, 0.0F}, {-150.0F, 0.0F, 0.0F}}};
                const std::array<std::string_view, 4> wallAttackNames{
                    "wall_attack_up", "wall_attack_down",
                    "wall_attack_right", "wall_attack_left"};
                for (std::size_t pattern = 0; pattern < 4; ++pattern) {
                    usm::game::LevelEnemyRuntime attacker;
                    assert(attacker.initialize(laterLevel));
                    for (const auto& state : attacker.states()) {
                        if (state.asset->objectId != 41359) {
                            assert(attacker.destroy(state.asset->objectId));
                        }
                    }
                    for (const auto& thread : wallReveal->script.threads()) {
                        for (const auto& command : thread.commands) {
                            if (command.name == "SetVisible") {
                                assert(attacker.applyCinematicCommand(
                                    laterLevel, thread, command));
                            }
                        }
                    }
                    attacker.updateGameplay(50, streetPlayer, &wallEnemyCollision);
                    const auto* wallActor = attacker.find(41359);
                    const auto start = wallActor->position;
                    const auto offset = wallAttackOffsets[pattern];
                    const usm::assets::Vector3 target{
                        start.x + offset.x, start.y, start.z + offset.z};
                    for (std::uint32_t frame = 0;
                         frame < 60 && !wallActor->meleeAttackActive; ++frame) {
                        attacker.updateGameplay(50, target, &wallEnemyCollision,
                            false, {0.0F, 1.0F, 0.0F}, true);
                    }
                    assert(wallActor->meleeAttackActive);
                    assert(wallActor->activeAnimation == wallAttackNames[pattern]);
                    bool hitPlayer = false;
                    for (std::uint32_t frame = 0; frame < 30; ++frame) {
                        attacker.updateGameplay(50, target, &wallEnemyCollision,
                            false, {0.0F, 1.0F, 0.0F}, true);
                        for (const auto& hit : attacker.consumePlayerHits()) {
                            assert(hit.sourceObjectId == 41359);
                            assert(hit.attackId == 33 && hit.damage == 50.0F);
                            hitPlayer = true;
                        }
                    }
                    assert(hitPlayer);
                    // Dropping off the wall releases the shared melee slot.
                    attacker.updateGameplay(50, streetPlayer, &wallEnemyCollision);
                    assert(!wallActor->meleeAttackRegistered);
                    assert(wallActor->activeAnimation == "wall_idle");
                }
            } else if (levelNumber == 7) {
                assertNativeBoss(341, 17);
                const auto secondOpeningGate = std::find_if(
                    laterLevel.cinematics().begin(),
                    laterLevel.cinematics().end(), [](const auto& cinematic) {
                        return cinematic.objectId == 141;
                    });
                assert(secondOpeningGate != laterLevel.cinematics().end());
                const auto boundEnemyThread = std::find_if(
                    secondOpeningGate->script.threads().begin(),
                    secondOpeningGate->script.threads().end(),
                    [](const auto& thread) {
                        return thread.type == 0 && thread.objectId == 130;
                    });
                assert(boundEnemyThread !=
                       secondOpeningGate->script.threads().end());
                const auto boundEnemyCondition = std::find_if(
                    boundEnemyThread->commands.begin(),
                    boundEnemyThread->commands.end(), [](const auto& command) {
                        return command.name == "IfEnemyDead";
                    });
                assert(boundEnemyCondition !=
                       boundEnemyThread->commands.end());
                assert(boundEnemyCondition->findAttribute("IDEnemy") !=
                           nullptr &&
                       boundEnemyCondition->findAttribute("IDEnemy")->value ==
                           "-1");
                usm::game::LevelEnemyRuntime openingGateEnemies;
                assert(openingGateEnemies.initialize(laterLevel));
                assert(!openingGateEnemies.cinematicEnemyDead(
                    *boundEnemyThread, *boundEnemyCondition));
                assert(openingGateEnemies.destroy(130));
                assert(openingGateEnemies.cinematicEnemyDead(
                    *boundEnemyThread, *boundEnemyCondition));
                usm::game::CinematicThread basicEnemyThread;
                basicEnemyThread.type = 1;
                basicEnemyThread.objectId = 133;
                usm::game::CinematicCommand basicEnemyCondition =
                    *boundEnemyCondition;
                basicEnemyCondition.attributes.front().value = "130";
                assert(openingGateEnemies.cinematicEnemyDead(
                    basicEnemyThread, basicEnemyCondition));
                basicEnemyCondition.attributes.front().value = "-1";
                assert(!openingGateEnemies.cinematicEnemyDead(
                    basicEnemyThread, basicEnemyCondition));
                const auto movingRoom = std::find_if(
                    laterLevel.rooms().begin(), laterLevel.rooms().end(),
                    [](const auto& room) { return room.objectId == 61204; });
                assert(movingRoom != laterLevel.rooms().end());
                assert(movingRoom->name == "Room22");
                assert(!movingRoom->motionInitiallyActive);
                assert(movingRoom->lineSpeedCentimetersPerMillisecond ==
                       3.0F);
                assert(movingRoom->linkedWaypointId == 60562);
                const auto roomPathStart = std::find_if(
                    laterLevel.waypoints().begin(),
                    laterLevel.waypoints().end(), [](const auto& waypoint) {
                        return waypoint.objectId == 60562;
                    });
                assert(roomPathStart != laterLevel.waypoints().end());
                assert(roomPathStart->nextWaypointIds[0] > 0);
                const auto roomPathTarget = std::find_if(
                    laterLevel.waypoints().begin(),
                    laterLevel.waypoints().end(),
                    [&roomPathStart](const auto& waypoint) {
                        return waypoint.objectId ==
                               roomPathStart->nextWaypointIds[0];
                    });
                assert(roomPathTarget != laterLevel.waypoints().end());

                const auto findRoomCommand =
                    [&laterLevel](std::int32_t cinematicId,
                                  std::string_view commandName,
                                  std::int32_t roomObjectId)
                    -> std::pair<const usm::game::CinematicThread*,
                                 const usm::game::CinematicCommand*> {
                    const auto cinematic = std::find_if(
                        laterLevel.cinematics().begin(),
                        laterLevel.cinematics().end(),
                        [cinematicId](const auto& candidate) {
                            return candidate.objectId == cinematicId;
                        });
                    assert(cinematic != laterLevel.cinematics().end());
                    for (const auto& thread :
                         cinematic->script.threads()) {
                        for (const auto& command : thread.commands) {
                            if (command.name != commandName) {
                                continue;
                            }
                            const auto* geometry =
                                command.findAttribute("^ID^Geometry");
                            if (geometry != nullptr &&
                                geometry->value ==
                                    std::to_string(roomObjectId)) {
                                return {&thread, &command};
                            }
                        }
                    }
                    return {nullptr, nullptr};
                };
                const auto [activeRoomThread, activeRoomCommand] =
                    findRoomCommand(61175, "ActiveRoom", 61204);
                const auto [deactiveRoomThread, deactiveRoomCommand] =
                    findRoomCommand(390, "DeactiveRoom", 61204);
                const auto [revertRoomThread, revertRoomCommand] =
                    findRoomCommand(390, "RevertRoomsPosition", 61204);
                assert(activeRoomThread != nullptr &&
                       activeRoomCommand != nullptr);
                assert(deactiveRoomThread != nullptr &&
                       deactiveRoomCommand != nullptr);
                assert(revertRoomThread != nullptr &&
                       revertRoomCommand != nullptr);

                usm::game::LevelTriggerRuntime roomTriggerRuntime;
                roomTriggerRuntime.bind(laterLevel.triggers());
                usm::game::GameplayCamera roomCamera;
                assert(roomCamera.bind(
                    laterLevel.cameraAreas(),
                    laterLevel.player().initialCameraAreaId));
                usm::game::LevelCinematicRuntime roomRuntime;
                roomRuntime.bind(roomTriggerRuntime, roomCamera,
                                 laterLevel.waypoints(), laterLevel.rooms());
                const auto* roomState = roomRuntime.findRoomMotion(61204);
                assert(roomState != nullptr && roomState->movingRoom);
                assert(!roomState->active);
                assert(roomState->targetWaypointId ==
                       roomPathStart->nextWaypointIds[0]);
                roomRuntime.advanceRoomMotion(50);
                assert(roomRuntime.findRoomMotion(61204)->position.x == 0.0F);
                assert(roomRuntime.findRoomMotion(61204)->position.y == 0.0F);
                assert(roomRuntime.findRoomMotion(61204)->position.z == 0.0F);

                assert(roomRuntime.applyCommand(*activeRoomThread,
                                                *activeRoomCommand));
                roomRuntime.advanceRoomMotion(50);
                roomState = roomRuntime.findRoomMotion(61204);
                assert(roomState != nullptr && roomState->active);
                const usm::assets::Vector3 pathDelta{
                    roomPathTarget->position.x - roomPathStart->position.x,
                    roomPathTarget->position.y - roomPathStart->position.y,
                    roomPathTarget->position.z - roomPathStart->position.z};
                const float pathLength = std::sqrt(
                    pathDelta.x * pathDelta.x + pathDelta.y * pathDelta.y +
                    pathDelta.z * pathDelta.z);
                assert(pathLength > 0.0F);
                const usm::assets::Vector3 expectedRoomPosition{
                    pathDelta.x / pathLength * 150.0F,
                    pathDelta.y / pathLength * 150.0F,
                    pathDelta.z / pathLength * 150.0F};
                assert(std::abs(roomState->position.x -
                                expectedRoomPosition.x) < 0.01F);
                assert(std::abs(roomState->position.y -
                                expectedRoomPosition.y) < 0.01F);
                assert(std::abs(roomState->position.z -
                                expectedRoomPosition.z) < 0.01F);
                assert(std::abs(roomState->velocity.x -
                                pathDelta.x / pathLength * 3000.0F) < 0.1F);
                assert(std::abs(roomState->velocity.y -
                                pathDelta.y / pathLength * 3000.0F) < 0.1F);
                assert(std::abs(roomState->velocity.z -
                                pathDelta.z / pathLength * 3000.0F) < 0.1F);

                assert(roomRuntime.applyCommand(*deactiveRoomThread,
                                                *deactiveRoomCommand));
                const usm::assets::Vector3 stoppedRoomPosition =
                    roomRuntime.findRoomMotion(61204)->position;
                roomRuntime.advanceRoomMotion(50);
                roomState = roomRuntime.findRoomMotion(61204);
                assert(!roomState->active);
                assert(roomState->position.x == stoppedRoomPosition.x);
                assert(roomState->position.y == stoppedRoomPosition.y);
                assert(roomState->position.z == stoppedRoomPosition.z);
                assert(roomState->velocity.x == 0.0F &&
                       roomState->velocity.y == 0.0F &&
                       roomState->velocity.z == 0.0F);
                assert(roomRuntime.applyCommand(*revertRoomThread,
                                                *revertRoomCommand));
                roomState = roomRuntime.findRoomMotion(61204);
                assert(roomState->position.x == 0.0F &&
                       roomState->position.y == 0.0F &&
                       roomState->position.z == 0.0F);
                assert(roomState->targetWaypointId ==
                       roomPathStart->nextWaypointIds[0]);

                const auto trainThree = std::find_if(
                    laterLevel.objects().begin(), laterLevel.objects().end(),
                    [](const auto& object) { return object.objectId == 60564; });
                const auto trainFour = std::find_if(
                    laterLevel.objects().begin(), laterLevel.objects().end(),
                    [](const auto& object) { return object.objectId == 340; });
                assert(trainThree != laterLevel.objects().end());
                assert(trainFour != laterLevel.objects().end());
                assert(trainThree->kind == usm::game::LevelObjectKind::Train);
                assert(trainThree->trainLineSpeedCentimetersPerMillisecond ==
                       1.0F);
                assert(!trainThree->trainInitiallyActive);
                assert(trainThree->trainPreviousObjectId == 60463);
                assert(trainThree->trainNextObjectId == 340);
                assert(trainFour->trainPreviousObjectId == 60564);
                assert(trainFour->trainNextObjectId == -1);

                const auto trainCinematic = std::find_if(
                    laterLevel.cinematics().begin(),
                    laterLevel.cinematics().end(), [](const auto& cinematic) {
                        return cinematic.objectId == 61175;
                    });
                assert(trainCinematic != laterLevel.cinematics().end());
                const auto trainThread = std::find_if(
                    trainCinematic->script.threads().begin(),
                    trainCinematic->script.threads().end(),
                    [](const auto& thread) { return thread.objectId == 60564; });
                assert(trainThread != trainCinematic->script.threads().end());
                const auto findTrainCommand =
                    [&trainThread](std::string_view name)
                    -> const usm::game::CinematicCommand* {
                    const auto command = std::find_if(
                        trainThread->commands.begin(),
                        trainThread->commands.end(),
                        [name](const auto& candidate) {
                            return candidate.name == name;
                        });
                    return command == trainThread->commands.end()
                               ? nullptr
                               : &*command;
                };
                const auto* cutTrainCommand = findTrainCommand("CutTrain");
                const auto* followTrainCommand =
                    findTrainCommand("FollowWayPoint");
                const auto* enableTrainCommand = findTrainCommand("EnableAI");
                assert(cutTrainCommand != nullptr);
                assert(followTrainCommand != nullptr);
                assert(enableTrainCommand != nullptr);
                const auto* followWaypoint =
                    followTrainCommand->findAttribute("^ID^WayPoint");
                assert(followWaypoint != nullptr &&
                       followWaypoint->value == "328");

                usm::game::LevelObjectRuntime trainRuntime;
                assert(trainRuntime.initialize(laterLevel));
                const auto* trainState = trainRuntime.find(60564);
                assert(trainState != nullptr && !trainState->trainActive);
                assert(trainState->trainPreviousObjectId == 60463);
                assert(trainRuntime.applyCinematicCommand(
                    laterLevel, *trainThread, *cutTrainCommand));
                trainState = trainRuntime.find(60564);
                assert(trainState->trainCut);
                assert(trainState->trainPreviousObjectId == -1);
                assert(trainRuntime.find(60463)->trainNextObjectId == -1);
                assert(trainRuntime.applyCinematicCommand(
                    laterLevel, *trainThread, *followTrainCommand));
                assert(trainRuntime.find(60564)->trainTargetWaypointId == 328);
                assert(trainRuntime.applyCinematicCommand(
                    laterLevel, *trainThread, *enableTrainCommand));
                trainState = trainRuntime.find(60564);
                assert(trainState->trainActive);
                assert(trainState->trainCurrentSpeedCentimetersPerMillisecond ==
                       1.0F);
                const auto trainWaypoint = std::find_if(
                    laterLevel.waypoints().begin(),
                    laterLevel.waypoints().end(), [](const auto& waypoint) {
                        return waypoint.objectId == 328;
                    });
                assert(trainWaypoint != laterLevel.waypoints().end());
                const usm::assets::Vector3 trainStart = trainState->position;
                const usm::assets::Vector3 trainDelta{
                    trainWaypoint->position.x - trainStart.x,
                    trainWaypoint->position.y - trainStart.y,
                    trainWaypoint->position.z - trainStart.z};
                const float trainDistance = std::hypot(
                    trainDelta.x, trainDelta.y, trainDelta.z);
                assert(trainDistance > 50.0F);
                trainRuntime.advanceAnimations(50);
                trainState = trainRuntime.find(60564);
                assert(std::abs(trainState->position.x -
                                (trainStart.x +
                                 trainDelta.x / trainDistance * 50.0F)) <
                       0.01F);
                assert(std::abs(trainState->position.y -
                                (trainStart.y +
                                 trainDelta.y / trainDistance * 50.0F)) <
                       0.01F);
                assert(std::abs(trainState->position.z -
                                (trainStart.z +
                                 trainDelta.z / trainDistance * 50.0F)) <
                       0.01F);
                assert(std::abs(trainState->trainDirection.x -
                                trainDelta.x / trainDistance) < 0.0001F);
                assert(std::abs(trainState->trainDirection.y -
                                trainDelta.y / trainDistance) < 0.0001F);
                assert(std::abs(trainState->trainDirection.z -
                                trainDelta.z / trainDistance) < 0.0001F);
                assert(std::abs(std::sqrt(
                                    trainState->trainRotation.x *
                                        trainState->trainRotation.x +
                                    trainState->trainRotation.y *
                                        trainState->trainRotation.y +
                                    trainState->trainRotation.z *
                                        trainState->trainRotation.z +
                                    trainState->trainRotation.w *
                                        trainState->trainRotation.w) -
                                1.0F) < 0.0001F);
                assert(std::abs(std::hypot(
                                    trainState
                                        ->trainVelocityCentimetersPerSecond.x,
                                    trainState
                                        ->trainVelocityCentimetersPerSecond.y,
                                    trainState
                                        ->trainVelocityCentimetersPerSecond.z) -
                                1000.0F) < 0.1F);

                // CRoom::SetPosition (0x0036d6e4) applies the same room
                // translation to its static physics body. The four shipped
                // moving train shells are presentation-only and have empty
                // collision BDAEs, so exercise that shared translation path
                // with the first non-empty shipped Level 7 room body.
                const auto collisionProofRoom = std::find_if(
                    laterLevel.rooms().begin(), laterLevel.rooms().end(),
                    [](const auto& room) {
                        return !room.collision.sceneGeometries().empty();
                    });
                assert(collisionProofRoom != laterLevel.rooms().end());
                std::array<usm::assets::Vector3, 3> collisionFace{};
                bool collisionFaceFound = false;
                for (const auto& geometry :
                     collisionProofRoom->collision.sceneGeometries()) {
                    for (const auto& buffer : geometry.meshBuffers) {
                        if (buffer.indices.size() < 3) {
                            continue;
                        }
                        const std::array<std::uint16_t, 3> indices{
                            buffer.indices[0], buffer.indices[1],
                            buffer.indices[2]};
                        if (indices[0] >= geometry.vertices.size() ||
                            indices[1] >= geometry.vertices.size() ||
                            indices[2] >= geometry.vertices.size()) {
                            continue;
                        }
                        collisionFace = {
                            geometry.vertices[indices[0]].position,
                            geometry.vertices[indices[1]].position,
                            geometry.vertices[indices[2]].position};
                        const usm::assets::Vector3 firstEdge{
                            collisionFace[1].x - collisionFace[0].x,
                            collisionFace[1].y - collisionFace[0].y,
                            collisionFace[1].z - collisionFace[0].z};
                        const usm::assets::Vector3 secondEdge{
                            collisionFace[2].x - collisionFace[0].x,
                            collisionFace[2].y - collisionFace[0].y,
                            collisionFace[2].z - collisionFace[0].z};
                        const usm::assets::Vector3 cross{
                            firstEdge.y * secondEdge.z -
                                firstEdge.z * secondEdge.y,
                            firstEdge.z * secondEdge.x -
                                firstEdge.x * secondEdge.z,
                            firstEdge.x * secondEdge.y -
                                firstEdge.y * secondEdge.x};
                        collisionFaceFound =
                            std::hypot(cross.x, cross.y, cross.z) > 0.001F;
                        if (collisionFaceFound) {
                            break;
                        }
                    }
                    if (collisionFaceFound) {
                        break;
                    }
                }
                assert(collisionFaceFound);
                const usm::assets::Vector3 firstEdge{
                    collisionFace[1].x - collisionFace[0].x,
                    collisionFace[1].y - collisionFace[0].y,
                    collisionFace[1].z - collisionFace[0].z};
                const usm::assets::Vector3 secondEdge{
                    collisionFace[2].x - collisionFace[0].x,
                    collisionFace[2].y - collisionFace[0].y,
                    collisionFace[2].z - collisionFace[0].z};
                usm::assets::Vector3 faceNormal{
                    firstEdge.y * secondEdge.z -
                        firstEdge.z * secondEdge.y,
                    firstEdge.z * secondEdge.x -
                        firstEdge.x * secondEdge.z,
                    firstEdge.x * secondEdge.y -
                        firstEdge.y * secondEdge.x};
                const float faceNormalLength =
                    std::hypot(faceNormal.x, faceNormal.y, faceNormal.z);
                faceNormal.x /= faceNormalLength;
                faceNormal.y /= faceNormalLength;
                faceNormal.z /= faceNormalLength;
                const usm::assets::Vector3 faceCenter{
                    (collisionFace[0].x + collisionFace[1].x +
                     collisionFace[2].x) /
                        3.0F,
                    (collisionFace[0].y + collisionFace[1].y +
                     collisionFace[2].y) /
                        3.0F,
                    (collisionFace[0].z + collisionFace[1].z +
                     collisionFace[2].z) /
                        3.0F};
                const auto segmentAcrossFace =
                    [&faceNormal](const usm::assets::Vector3& center) {
                    return std::array<usm::assets::Vector3, 2>{
                        usm::assets::Vector3{
                            center.x + faceNormal.x * 25.0F,
                            center.y + faceNormal.y * 25.0F,
                            center.z + faceNormal.z * 25.0F},
                        usm::assets::Vector3{
                            center.x - faceNormal.x * 25.0F,
                            center.y - faceNormal.y * 25.0F,
                            center.z - faceNormal.z * 25.0F}};
                };
                usm::game::LevelCollision movingRoomCollision;
                assert(movingRoomCollision.build(
                    std::span<const usm::game::LevelRoomAsset>(
                        &*collisionProofRoom, 1)));
                auto faceSegment = segmentAcrossFace(faceCenter);
                assert(movingRoomCollision.segmentBlocked(faceSegment[0],
                                                          faceSegment[1]));
                usm::game::RoomMotionState collisionRoomState;
                collisionRoomState.objectId = collisionProofRoom->objectId;
                collisionRoomState.roomId = 1;
                collisionRoomState.position = expectedRoomPosition;
                assert(movingRoomCollision.updateRoomPositions(
                    std::span<const usm::game::RoomMotionState>(
                        &collisionRoomState, 1)));
                const usm::assets::Vector3 movedFaceCenter{
                    faceCenter.x + expectedRoomPosition.x,
                    faceCenter.y + expectedRoomPosition.y,
                    faceCenter.z + expectedRoomPosition.z};
                faceSegment = segmentAcrossFace(movedFaceCenter);
                assert(movingRoomCollision.segmentBlocked(faceSegment[0],
                                                          faceSegment[1]));
            } else if (levelNumber == 8) {
                assertNativeBoss(40524, 15);
                const auto phantomAsset = std::find_if(
                    laterLevel.enemies().begin(), laterLevel.enemies().end(),
                    [](const auto& enemy) {
                        return enemy.objectId == 40524;
                    });
                assert(phantomAsset != laterLevel.enemies().end());
                assert(phantomAsset->gameType == "Robot_Phantom");
                assert(phantomAsset->health == 1500.0F);
                const auto* phantomAttributes =
                    laterLevel.enemyAttributeConfigs().find(15);
                assert(phantomAttributes != nullptr);
                assert(phantomAttributes->rangedAttackTypeMapIndices ==
                       (std::vector<std::int32_t>{17}));
                assert(usm::game::resolveEnemyRangeWeaponType(17) == 30);
                const auto* phantomRangeAttack =
                    laterLevel.enemyRangeAttackConfigs().findByMapId(17);
                assert(phantomRangeAttack != nullptr);
                assert(phantomRangeAttack->name == "RANGE_ATTACK_07");
                assert(phantomRangeAttack
                           ->animationDurationMilliseconds == 1000.0F);
                assert(phantomRangeAttack->damage == 40.0F);
                const auto& phantomBoomerang =
                    laterLevel.boomerangProjectile();
                assert(phantomBoomerang.meshFile ==
                       "meshes_bin/phantom_unit_weapons.bdae");
                assert(!phantomBoomerang.mesh.geometries().empty());
                assert(phantomBoomerang.mesh.images().size() ==
                       phantomBoomerang.textures.size());
                const auto* weaponsAnimation =
                    phantomBoomerang.animationBank.findClip("weapons");
                assert(weaponsAnimation != nullptr);
                assert(weaponsAnimation->durationMilliseconds() > 0);

                const auto assertPhantomAction =
                    [&laterLevel](std::string_view animation,
                                  std::int32_t keyPercent,
                                  std::int32_t attackId,
                                  float damage) {
                        const auto events =
                            laterLevel.enemySpecialActions().findEvents(
                                15, animation);
                        const auto event = std::find_if(
                            events.begin(), events.end(),
                            [keyPercent, attackId](const auto* candidate) {
                                return candidate != nullptr &&
                                       candidate->keyFramePercent ==
                                           keyPercent &&
                                       candidate->attackId == attackId;
                            });
                        assert(event != events.end());
                        const auto* attack =
                            laterLevel.attackConfigs().find(
                                static_cast<std::int16_t>(attackId));
                        assert(attack != nullptr &&
                               attack->damage == damage);
                    };
                assertPhantomAction("rush_attack1", 40, 60, 40.0F);
                assertPhantomAction("rush_attack2", 80, 61, 70.0F);
                assertPhantomAction("conceal_attack", 60, 65, 40.0F);
                assertPhantomAction("conceal_attack", 90, 66, 60.0F);

                // CBoss::_GLOBAL__I_CBoss (0x00329cd4) emits Robot
                // Phantom's exact repeating task order 3,24,5,24. The
                // behavior IDs are recovered from GetBehaviorId at
                // 0x003b95bc (melee 0x12f), 0x003aa5f8 (conceal 0x141),
                // and 0x003a9734 (range 0x134).
                usm::game::LevelEnemyRuntime phantomRuntime;
                assert(phantomRuntime.initialize(laterLevel));
                usm::game::CinematicThread phantomThread;
                phantomThread.objectId = 40524;
                usm::game::CinematicCommand showPhantom;
                showPhantom.name = "SetVisible";
                showPhantom.attributes.push_back(
                    {"bool", "Visible", "true"});
                assert(phantomRuntime.applyCinematicCommand(
                    laterLevel, phantomThread, showPhantom));
                assert(phantomRuntime.setDiagnosticAiEnabled(40524, true));
                usm::assets::Vector3 phantomVictim{
                    phantomAsset->position.x + 200.0F,
                    phantomAsset->position.y,
                    phantomAsset->position.z};
                const usm::assets::Vector3 phantomVictimFacing{
                    1.0F, 0.0F, 0.0F};
                std::vector<usm::game::EnemyPlayerHit> phantomHits;
                std::vector<usm::game::EnemyProjectileEvent>
                    phantomProjectileEvents;
                const auto tickPhantom =
                    [&phantomRuntime, &phantomVictim,
                     &phantomVictimFacing, &phantomHits,
                     &phantomProjectileEvents](std::uint32_t milliseconds) {
                        phantomRuntime.updateGameplay(
                            milliseconds, phantomVictim, nullptr, false,
                            phantomVictimFacing);
                        auto hits = phantomRuntime.consumePlayerHits();
                        phantomHits.insert(phantomHits.end(), hits.begin(),
                                           hits.end());
                        auto events =
                            phantomRuntime.consumeProjectileEvents();
                        phantomProjectileEvents.insert(
                            phantomProjectileEvents.end(), events.begin(),
                            events.end());
                    };
                tickPhantom(1);
                assert(phantomRuntime.find(40524)->robotPhantomTask ==
                       usm::game::RobotPhantomTaskState::ApproachMelee);
                tickPhantom(1);
                assert(phantomRuntime.find(40524)->robotPhantomTask ==
                       usm::game::RobotPhantomTaskState::RushReady);
                for (std::uint32_t elapsed = 0;
                     elapsed < 5000 &&
                     phantomRuntime.find(40524)->robotPhantomTask !=
                         usm::game::RobotPhantomTaskState::ConcealReady;
                     elapsed += 25) {
                    tickPhantom(25);
                }
                assert(phantomRuntime.find(40524)->robotPhantomTask ==
                       usm::game::RobotPhantomTaskState::ConcealReady);
                const auto firstRush = std::find_if(
                    phantomHits.begin(), phantomHits.end(),
                    [](const auto& hit) { return hit.attackId == 60; });
                const auto secondRush = std::find_if(
                    phantomHits.begin(), phantomHits.end(),
                    [](const auto& hit) { return hit.attackId == 61; });
                assert(firstRush != phantomHits.end() &&
                       firstRush->damage == 40.0F);
                assert(secondRush != phantomHits.end() &&
                       secondRush->damage == 70.0F);

                for (std::uint32_t elapsed = 0;
                     elapsed < 2000 &&
                     phantomRuntime.find(40524)->robotPhantomTask !=
                         usm::game::RobotPhantomTaskState::ConcealHidden;
                     elapsed += 25) {
                    tickPhantom(25);
                }
                const auto* concealedPhantom = phantomRuntime.find(40524);
                assert(concealedPhantom->robotPhantomTask ==
                       usm::game::RobotPhantomTaskState::ConcealHidden);
                assert(!concealedPhantom->visible);
                const auto concealStart = concealedPhantom->position;
                tickPhantom(499);
                concealedPhantom = phantomRuntime.find(40524);
                assert(concealedPhantom->robotPhantomTask ==
                       usm::game::RobotPhantomTaskState::ConcealHidden);
                assert(concealedPhantom->position.x == concealStart.x);
                tickPhantom(1);
                concealedPhantom = phantomRuntime.find(40524);
                assert(concealedPhantom->robotPhantomTask ==
                       usm::game::RobotPhantomTaskState::ConcealAttack);
                assert(concealedPhantom->visible);
                assert(std::abs(
                           concealedPhantom->position.x -
                           (phantomVictim.x - 110.0F)) < 0.01F);
                assert(std::abs(concealedPhantom->position.y -
                                phantomVictim.y) < 0.01F);

                for (std::uint32_t elapsed = 0;
                     elapsed < 4000 &&
                     phantomRuntime.find(40524)
                             ->robotPhantomSequenceIndex != 2;
                     elapsed += 25) {
                    tickPhantom(25);
                }
                const auto concealHit = std::find_if(
                    phantomHits.begin(), phantomHits.end(),
                    [](const auto& hit) { return hit.attackId == 65; });
                const auto concealSplash = std::find_if(
                    phantomHits.begin(), phantomHits.end(),
                    [](const auto& hit) { return hit.attackId == 66; });
                assert(concealHit != phantomHits.end() &&
                       concealHit->damage == 40.0F);
                assert(concealSplash != phantomHits.end() &&
                       concealSplash->damage == 60.0F);
                assert(phantomRuntime.find(40524)
                           ->robotPhantomSequenceIndex == 2);

                for (std::uint32_t elapsed = 0;
                     elapsed < 4000 && phantomRuntime.boomerangs().empty();
                     elapsed += 25) {
                    tickPhantom(25);
                }
                assert(phantomRuntime.boomerangs().size() == 1);
                assert(phantomRuntime.boomerangs().front().sourceObjectId ==
                       40524);
                assert(phantomRuntime.boomerangs().front().damage == 40.0F);
                assert(phantomRuntime.boomerangs().front().phase ==
                       usm::game::EnemyBoomerangPhase::Outbound);
                // The constructors load 0x42480000 (50.0 cm) immediately
                // before createFlyableEntityPhysics at 0x003d8650. Move the
                // player center 85 cm perpendicular to the fixed launch line:
                // inside player-radius 50 + boomerang-radius 50, but outside
                // the incorrect player-radius 50 + molotov-radius 20.
                const auto launchedBoomerang =
                    phantomRuntime.boomerangs().front();
                const float horizontalDirectionLength = std::hypot(
                    launchedBoomerang.facing.x,
                    launchedBoomerang.facing.y);
                assert(horizontalDirectionLength > 0.0F);
                phantomVictim.x -= launchedBoomerang.facing.y /
                                   horizontalDirectionLength * 85.0F;
                phantomVictim.y += launchedBoomerang.facing.x /
                                   horizontalDirectionLength * 85.0F;
                for (std::uint32_t elapsed = 0;
                     elapsed < 2000 &&
                     std::none_of(
                         phantomProjectileEvents.begin(),
                         phantomProjectileEvents.end(), [](const auto& event) {
                             return event.kind ==
                                    usm::game::EnemyProjectileEventKind::
                                        PlayerContact;
                         });
                     elapsed += 25) {
                    tickPhantom(25);
                }
                assert(std::any_of(
                    phantomProjectileEvents.begin(),
                    phantomProjectileEvents.end(), [](const auto& event) {
                        return event.kind ==
                               usm::game::EnemyProjectileEventKind::
                                   PlayerContact;
                    }));
                for (std::uint32_t elapsed = 0;
                     elapsed < 10000 &&
                     phantomRuntime.find(40524)
                             ->robotPhantomSequenceIndex != 3;
                     elapsed += 25) {
                    tickPhantom(25);
                }
                assert(phantomRuntime.find(40524)
                           ->robotPhantomSequenceIndex == 3);
                assert(std::any_of(
                    phantomProjectileEvents.begin(),
                    phantomProjectileEvents.end(), [](const auto& event) {
                        return event.kind ==
                               usm::game::EnemyProjectileEventKind::Spawned;
                    }));
                assert(std::any_of(
                    phantomProjectileEvents.begin(),
                    phantomProjectileEvents.end(), [](const auto& event) {
                        return event.kind ==
                               usm::game::EnemyProjectileEventKind::Returned;
                    }));
            } else if (levelNumber == 9) {
                assertNativeBoss(40252, 15);
            } else if (levelNumber == 10) {
                assertNativeBoss(60349, 18);
            } else if (levelNumber == 12) {
                assertNativeBoss(60367, 23);
            }
            if (levelNumber == 5) {
                const auto maleRunAnimations =
                    laterLevel.enemyBehaviorConfigs()
                        .resolveStateAnimationNames(
                            "ENEMY_BEHAVIOR_MOVE_STATE_RUN", 8);
                const auto femaleRunAnimations =
                    laterLevel.enemyBehaviorConfigs()
                        .resolveStateAnimationNames(
                            "ENEMY_BEHAVIOR_MOVE_STATE_RUN", 9);
                const auto femaleAttackAnimations =
                    laterLevel.enemyBehaviorConfigs()
                        .resolveStateAnimationNames(
                            "ENEMY_BEHAVIOR_MELEE_ATTACK_STATE_DO_ATTACK", 9);
                assert(maleRunAnimations.size() == 1);
                assert(maleRunAnimations.front() == "walk");
                assert(femaleRunAnimations.size() == 1);
                assert(femaleRunAnimations.front() == "run");
                assert(femaleAttackAnimations.size() == 1);
                assert(femaleAttackAnimations.front() == "idle_claw_idle");
                for (const std::int32_t symbioteId :
                     {20376, 40825, 40970}) {
                    const auto symbiote = std::find_if(
                        laterLevel.enemies().begin(),
                        laterLevel.enemies().end(),
                        [symbioteId](const auto& enemy) {
                            return enemy.objectId == symbioteId;
                        });
                    assert(symbiote != laterLevel.enemies().end());
                    assert(symbiote->gameType == "SymbioteZombie_Girl");
                    assert(symbiote->enemyTypeId == 9);
                    assert(symbiote->archetypeIndex <
                           laterLevel.enemyArchetypes().size());
                    assert(!symbiote->initialAnimation.empty());
                }
                usm::game::LevelEnemyRuntime symbioteRuntime;
                assert(symbioteRuntime.initialize(laterLevel));
                assert(symbioteRuntime.setDiagnosticAiEnabled(40822, true));
                const auto* openingMale = symbioteRuntime.find(40822);
                assert(openingMale != nullptr);
                symbioteRuntime.updateGameplay(
                    16,
                    {openingMale->position.x + 1000.0F,
                     openingMale->position.y, openingMale->position.z});
                assert(symbioteRuntime.find(40822)->activeAnimation == "walk");
                openingMale = symbioteRuntime.find(40822);
                symbioteRuntime.updateGameplay(
                    16,
                    {openingMale->position.x + 200.0F,
                     openingMale->position.y, openingMale->position.z});
                assert(symbioteRuntime.find(40822)->activeAnimation ==
                       "idle_claw_idle");
                usm::game::LevelCollision levelFiveCollision;
                assert(levelFiveCollision.build(laterLevel.rooms()));
                usm::assets::Vector3 rooftopLipPosition{
                    19700.0F, 3869.81F, 2311.17F};
                const usm::assets::Vector3 rooftopLipEntry{
                    20366.47F, 5005.61F, 2309.71F};
                const usm::assets::Vector3 rooftopLipTarget{
                    20355.57F, 6533.26F, 2304.88F};
                const auto followRooftopRoute =
                    [&](const usm::assets::Vector3& target) {
                        for (int frame = 0; frame < 100; ++frame) {
                            const float differenceX =
                                target.x - rooftopLipPosition.x;
                            const float differenceY =
                                target.y - rooftopLipPosition.y;
                            const float distance =
                                std::sqrt(differenceX * differenceX +
                                          differenceY * differenceY);
                            if (distance <= 180.0F) {
                                break;
                            }
                            const float step = std::min(35.0F, distance);
                            const usm::assets::Vector3 desired{
                                rooftopLipPosition.x +
                                    differenceX / distance * step,
                                rooftopLipPosition.y +
                                    differenceY / distance * step,
                                rooftopLipPosition.z};
                            usm::assets::Vector3 resolved;
                            assert(levelFiveCollision.resolveGroundMotion(
                                rooftopLipPosition, desired, resolved, 75.0F,
                                150.0F, 0U,
                                usm::game::LevelCollisionDepenetration::
                                    TowardAuthoredNormal));
                            rooftopLipPosition = resolved;
                        }
                    };
                followRooftopRoute(rooftopLipEntry);
                followRooftopRoute(rooftopLipTarget);
                const float rooftopLipDifferenceX =
                    rooftopLipTarget.x - rooftopLipPosition.x;
                const float rooftopLipDifferenceY =
                    rooftopLipTarget.y - rooftopLipPosition.y;
                assert(std::sqrt(rooftopLipDifferenceX *
                                     rooftopLipDifferenceX +
                                 rooftopLipDifferenceY *
                                     rooftopLipDifferenceY) <= 180.0F);
                // This is the exact capsule position where autoplay reaches
                // the south edge of the 10 cm walkway riser. It must advance
                // north instead of treating that low face as a full wall.
                usm::assets::Vector3 rooftopRiserPosition{
                    20356.5F, 4873.39F, 2294.3F};
                for (int frame = 0; frame < 50; ++frame) {
                    const float remainingY =
                        rooftopLipTarget.y - rooftopRiserPosition.y;
                    const float step = std::min(35.0F, remainingY);
                    const usm::assets::Vector3 desired{
                        rooftopRiserPosition.x,
                        rooftopRiserPosition.y + step,
                        rooftopRiserPosition.z};
                    usm::assets::Vector3 resolved;
                    assert(levelFiveCollision.resolveGroundMotion(
                        rooftopRiserPosition, desired, resolved, 75.0F,
                        150.0F, 0U,
                        usm::game::LevelCollisionDepenetration::
                            TowardAuthoredNormal));
                    rooftopRiserPosition = resolved;
                }
                assert(rooftopRiserPosition.y > 6000.0F);
                const auto firstWebWall = std::find_if(
                    laterLevel.objects().begin(), laterLevel.objects().end(),
                    [](const auto& object) {
                        return object.objectId == 20327;
                    });
                assert(firstWebWall != laterLevel.objects().end());
                assert(firstWebWall->kind ==
                       usm::game::LevelObjectKind::SpiderWebWall);
                assert(firstWebWall->initialAnimation.empty());
                const auto& webWallArchetype = laterLevel.objectArchetypes()[
                    firstWebWall->archetypeIndex];
                assert(webWallArchetype.mesh.sceneGeometries().size() == 2);
                assert(webWallArchetype.mesh.skins().size() == 1);
                const auto* webWallOpen =
                    webWallArchetype.animationBank.findClip("open");
                assert(webWallOpen != nullptr);
                std::vector<usm::assets::ColladaGeometry> webWallBindPose;
                std::vector<usm::assets::ColladaGeometry> webWallOpenPose;
                assert(usm::assets::evaluateColladaPose(
                    webWallArchetype.mesh, webWallArchetype.animationBank, 0,
                    webWallBindPose));
                assert(usm::assets::evaluateColladaPose(
                    webWallArchetype.mesh, webWallArchetype.animationBank,
                    webWallOpen->startMilliseconds, webWallOpenPose));
                assert(webWallBindPose.size() == 1);
                assert(webWallOpenPose.size() == webWallBindPose.size());
                assert(webWallOpenPose.front().vertices.size() ==
                       webWallBindPose.front().vertices.size());
                const auto lockedSlide = std::find_if(
                    laterLevel.slides().begin(), laterLevel.slides().end(),
                    [](const auto& slide) { return slide.objectId == 40662; });
                assert(lockedSlide != laterLevel.slides().end());
                assert(!lockedSlide->enabled);
                assert(lockedSlide->waypointIds ==
                       (std::vector<std::int32_t>{40663, 40664}));
                const auto unlockCinematic = std::find_if(
                    laterLevel.cinematics().begin(),
                    laterLevel.cinematics().end(),
                    [](const auto& cinematic) {
                        return cinematic.objectId == 40941;
                    });
                assert(unlockCinematic != laterLevel.cinematics().end());
                const usm::game::CinematicCommand* unlockCommand = nullptr;
                for (const auto& thread : unlockCinematic->script.threads()) {
                    const auto command = std::find_if(
                        thread.commands.begin(), thread.commands.end(),
                        [](const auto& candidate) {
                            return candidate.name == "Enable_Slide";
                        });
                    if (command != thread.commands.end()) {
                        unlockCommand = &*command;
                        break;
                    }
                }
                assert(unlockCommand != nullptr);
                usm::game::LevelSlideRuntime levelFiveSlides;
                levelFiveSlides.bind(laterLevel.slides(),
                                     laterLevel.waypoints());
                assert(!*levelFiveSlides.enabled(40662));
                assert(levelFiveSlides.applyCinematicCommand(*unlockCommand));
                assert(*levelFiveSlides.enabled(40662));
                const auto openingCinematic = std::find_if(
                    laterLevel.cinematics().begin(),
                    laterLevel.cinematics().end(),
                    [](const auto& cinematic) {
                        return cinematic.objectId == 17;
                    });
                assert(openingCinematic != laterLevel.cinematics().end());
                assert(openingCinematic->animatedCamera.clipId() == 0);
                assert(openingCinematic->animatedCamera
                           .clipStartMilliseconds() == 0);
                assert(openingCinematic->animatedCamera
                           .clipEndMilliseconds() == 8666);
                assert(openingCinematic->colladaDurationMilliseconds ==
                       8666);
                const auto webRopeActor = std::find_if(
                    openingCinematic->actors.begin(),
                    openingCinematic->actors.end(),
                    [](const auto& actor) { return actor.objectId == 63; });
                assert(webRopeActor != openingCinematic->actors.end());
                assert(webRopeActor->animation.durationMilliseconds() ==
                       30299);
                assert(webRopeActor->animationClipId == 0);
                assert(webRopeActor->animationClipStartMilliseconds == 0);
                assert(webRopeActor->animationClipEndMilliseconds == 8666);
                assert(webRopeActor->animationTimestamp(4000) == 4000);
                assert(webRopeActor->animationTimestamp(9000) == 8666);
            }
            if (levelNumber == 7) {
                const auto visualOnlyRoom = std::find_if(
                    laterLevel.rooms().begin(), laterLevel.rooms().end(),
                    [](const auto& room) { return room.name == "Room7"; });
                assert(visualOnlyRoom != laterLevel.rooms().end());
                assert(visualOnlyRoom->collision.geometries().empty());
                assert(visualOnlyRoom->navigationMesh.geometries().empty());
                const auto roomSixteenSlide = std::find_if(
                    laterLevel.slides().begin(), laterLevel.slides().end(),
                    [](const auto& slide) { return slide.objectId == 198; });
                assert(roomSixteenSlide != laterLevel.slides().end());
                assert(roomSixteenSlide->enabled);
                assert(roomSixteenSlide->waypointIds ==
                       (std::vector<std::int32_t>{199, 200}));
                usm::game::LevelSlideRuntime levelSevenSlides;
                levelSevenSlides.bind(laterLevel.slides(),
                                      laterLevel.waypoints());
                assert(levelSevenSlides.enabled(198).has_value());
                assert(*levelSevenSlides.enabled(198));
                const auto exactStartCatch = levelSevenSlides.findCatch(
                    {28432.4F, 6707.22F, 3037.96F}, true, false);
                assert(exactStartCatch.slide == &*roomSixteenSlide);
                // CSlider finds the closest point in 3D before applying its
                // independent 50 cm horizontal gate. A falling player must
                // therefore cross the first waypoint before dropping far
                // enough for the descending rope's projection to move more
                // than 50 cm ahead.
                assert(levelSevenSlides.findCatch(
                           {28432.4F, 6707.22F, 2645.31F}, true, false)
                           .slide == &*roomSixteenSlide);
            }
            if (levelNumber == 8) {
                assert(!laterLevel.rooms().front().collision.geometries().empty());
                assert(laterLevel.rooms().front()
                           .navigationMesh.geometries().empty());
            }
            if (levelNumber == 9 || levelNumber == 10) {
                assert(laterLevel.bonuses().empty());
            }
            if (levelNumber == 12) {
                assert(!laterLevel.introSky().textures.empty());
                assert(!laterLevel.introSky().textures.front()
                            .mipLevels().empty());
                assert(laterLevel.introSky().textures.front()
                           .mipLevels().front().width == 512);
            }
        }
        for (std::size_t index = 0;
             index < expectedThugPresentations.size(); ++index) {
            assert(thugPresentationCounts[index] ==
                   expectedThugPresentations[index].instanceCount);
        }
        const auto levelThreeBeam = std::find_if(
            levelThree.objects().begin(), levelThree.objects().end(),
            [](const auto& object) { return object.objectId == 30946; });
        assert(levelThreeBeam != levelThree.objects().end());
        assert(levelThreeBeam->initialAnimation == "fall");
        assert(levelThreeBeam->additiveBlend);
        struct ExpectedLevelThreeBeam {
            std::int32_t objectId;
            usm::assets::Vector3 position;
        };
        constexpr std::array expectedLevelThreeBeams{
            ExpectedLevelThreeBeam{30946,
                                   {-2868.02F, -5358.08F, 2927.32F}},
            ExpectedLevelThreeBeam{30949,
                                   {-2899.25F, -5217.29F, 2927.32F}},
            ExpectedLevelThreeBeam{30950,
                                   {-3073.85F, -5512.20F, 2927.32F}},
            ExpectedLevelThreeBeam{30951,
                                   {-2734.88F, -5077.88F, 2844.10F}},
            ExpectedLevelThreeBeam{30969,
                                   {-3115.90F, -5686.42F, 2844.10F}},
        };
        for (const ExpectedLevelThreeBeam& expected :
             expectedLevelThreeBeams) {
            const auto beam = std::find_if(
                levelThree.objects().begin(), levelThree.objects().end(),
                [&expected](const auto& object) {
                    return object.objectId == expected.objectId;
                });
            assert(beam != levelThree.objects().end());
            assert(beam->gameType == "AnimatedObject");
            assert(!beam->visible);
            assert(beam->hasCollision);
            assert(beam->additiveBlend);
            assert(beam->initialAnimation == "fall");
            assert(beam->initialAnimationLoops);
            assert(std::abs(beam->position.x - expected.position.x) < 0.01F);
            assert(std::abs(beam->position.y - expected.position.y) < 0.01F);
            assert(std::abs(beam->position.z - expected.position.z) < 0.01F);
            assert(std::abs(beam->rotation.x) < 0.0001F);
            assert(std::abs(beam->rotation.y) < 0.0001F);
            assert(std::abs(beam->rotation.z) < 0.0001F);
            assert(std::abs(beam->rotation.w - 1.0F) < 0.0001F);
            assert(std::abs(beam->worldTransform[0] - 1.0F) < 0.0001F);
            assert(std::abs(beam->worldTransform[5] - 1.0F) < 0.0001F);
            assert(std::abs(beam->worldTransform[10] - 1.48272F) < 0.0001F);
            assert(std::abs(beam->worldTransform[12] - expected.position.x) <
                   0.01F);
            assert(std::abs(beam->worldTransform[13] - expected.position.y) <
                   0.01F);
            assert(std::abs(beam->worldTransform[14] - expected.position.z) <
                   0.01F);
        }
        usm::game::LevelEnemyRuntime levelThreeOpeningEnemies;
        assert(levelThreeOpeningEnemies.initialize(levelThree));
        assert(!levelThreeOpeningEnemies.find(30314)->visible);
        usm::game::CinematicThread levelThreeBasicThread;
        levelThreeBasicThread.type = 1;
        levelThreeBasicThread.objectId = -1;
        usm::game::CinematicCommand exposeLevelThreeEnemy;
        exposeLevelThreeEnemy.name = "SetVisible";
        exposeLevelThreeEnemy.attributes = {
            {"int", "ObjectID", "30314"},
            {"bool", "Visible", "true"},
        };
        assert(levelThreeOpeningEnemies.applyCinematicCommand(
            levelThree, levelThreeBasicThread, exposeLevelThreeEnemy));
        assert(levelThreeOpeningEnemies.find(30314)->visible);
        usm::game::LevelObjectRuntime levelThreeOpeningObjects;
        assert(levelThreeOpeningObjects.initialize(levelThree));
        usm::game::CinematicThread beamObjectThread;
        beamObjectThread.type = 0;
        beamObjectThread.objectId = 30950;
        usm::game::CinematicCommand crossIdBeamAnimation;
        crossIdBeamAnimation.name = "SetAnim";
        crossIdBeamAnimation.attributes = {
            {"int", "ObjectID", "30946"},
            {"string", "$Anim", "keep"},
            {"bool", "loop", "true"},
            {"float", "speed", "1.000000"},
        };
        assert(levelThreeOpeningObjects.applyCinematicCommand(
            levelThree, beamObjectThread, crossIdBeamAnimation));
        assert(levelThreeOpeningObjects.find(30950)->activeAnimation ==
               "keep");
        assert(levelThreeOpeningObjects.find(30946)->activeAnimation ==
               "fall");
        usm::game::CinematicThread beamBasicThread;
        beamBasicThread.type = 1;
        beamBasicThread.objectId = -1;
        assert(levelThreeOpeningObjects.applyCinematicCommand(
            levelThree, beamBasicThread, crossIdBeamAnimation));
        assert(levelThreeOpeningObjects.find(30946)->activeAnimation ==
               "keep");
        const auto levelThreeOpeningHostage = std::find_if(
            levelThree.objects().begin(), levelThree.objects().end(),
            [](const auto& object) { return object.objectId == 30472; });
        assert(levelThreeOpeningHostage != levelThree.objects().end());
        assert(levelThreeOpeningHostage->kind ==
               usm::game::LevelObjectKind::Hostage);
        assert(!levelThreeOpeningHostage->initialAnimation.empty());
        assert(levelThreeOpeningHostage->initialAnimationLoops);
        assert(std::count_if(
                   levelThree.objects().begin(), levelThree.objects().end(),
                   [](const auto& object) {
                       return object.additiveBlend &&
                              object.gameType == "AnimatedObject";
                   }) >= 5);
        const auto& beamArchetype =
            levelThree.objectArchetypes()[levelThreeBeam->archetypeIndex];
        assert(beamArchetype.meshFile == "meshes_bin/electro_beam.bdae");
        assert(beamArchetype.animationFile ==
               "meshes_bin/electro_beam.bdae");
        assert(beamArchetype.animationBank.tracks().size() == 11);
        assert(beamArchetype.animationBank.clips().size() == 3);
        assert(beamArchetype.mesh.materials().size() == 4);
        for (const auto& material : beamArchetype.mesh.materials()) {
            assert(material.transparentAlphaChannel);
            assert(!material.additiveBlend);
            assert(material.diffuseImageIndex.has_value());
        }
        const auto* fallClip = beamArchetype.animationBank.findClip("fall");
        assert(fallClip != nullptr);
        assert(fallClip->startMilliseconds == 0);
        assert(fallClip->endMilliseconds == 200);
        const auto beamScaleTrack = std::find_if(
            beamArchetype.animationBank.tracks().begin(),
            beamArchetype.animationBank.tracks().end(),
            [](const auto& track) {
                return track.targetNode == "Plane03-node" &&
                       track.property ==
                           usm::assets::ColladaAnimationProperty::Scale;
            });
        assert(beamScaleTrack != beamArchetype.animationBank.tracks().end());
        const auto beamScaleStart = beamScaleTrack->sample(0);
        const auto beamScaleEnd = beamScaleTrack->sample(200);
        assert(beamScaleStart.componentCount == 3);
        assert(beamScaleEnd.componentCount == 3);
        assert(std::isfinite(beamScaleStart.value[0]));
        assert(std::isfinite(beamScaleStart.value[1]));
        assert(std::isfinite(beamScaleStart.value[2]));
        std::size_t beamTextureOffsetTrackCount = 0;
        for (const auto& track : beamArchetype.animationBank.tracks()) {
            if (track.property ==
                usm::assets::ColladaAnimationProperty::TextureOffsetV) {
                ++beamTextureOffsetTrackCount;
                assert(track.componentCount == 1);
                assert(track.targetNode.ends_with("-fx"));
            }
        }
        assert(beamTextureOffsetTrackCount == 8);
        const auto beamOffsetTrack = std::find_if(
            beamArchetype.animationBank.tracks().begin(),
            beamArchetype.animationBank.tracks().end(),
            [](const auto& track) {
                return track.id == "offsetV1" &&
                       track.targetNode == "Material__28-fx" &&
                       track.property ==
                           usm::assets::ColladaAnimationProperty::TextureOffsetV;
            });
        assert(beamOffsetTrack != beamArchetype.animationBank.tracks().end());
        assert(std::abs(beamOffsetTrack->sample(0).value[0] - 0.75F) <
               0.0001F);
        assert(std::abs(beamOffsetTrack->sample(100).value[0]) < 0.0001F);
        std::set<std::string> linkedCommandNames;
        const auto collectCommandNames = [&linkedCommandNames](
                                             const auto& script) {
            for (const auto& thread : script.threads()) {
                for (const auto& command : thread.commands) {
                    linkedCommandNames.insert(command.name);
                }
            }
        };
        collectCommandNames(bootstrap.introScript());
        collectCommandNames(bootstrap.introStartScript());
        for (const auto& cinematic : bootstrap.cinematics()) {
            if (cinematic.scriptAvailable) {
                collectCommandNames(cinematic.script);
            }
        }
        const std::set<std::string> accountedLinkedCommands{
            "ChangeCamera",      "DisableAI",       "DisableTrigger",
            "EnableAI",         "EnableCameraArea", "EnableTrigger",
            "GameEnd",          "GetDamage",       "IfEnemyDead",
            "IfObjectDestroyed", "InterfaceControl", "KillObject",
            "LevelEnd",         "MoveObject",      "MustBeVisibleRoom",
            "Physics",          "PlayDAEAnim",     "PlayDAECamera",
            "PlayEffect",       "Save",            "SetAnim",
            "SetCameraArea",    "SetSlowMotion",   "SetVisible",
            "ShakeCamera",      "ShowHealth",      "ShowMessage",
            "ShowStream",       "SoundControl",    "StartCinematic",
            "StartQTE",         "StartSlide",      "StartTimer",
            "Transport",        "Tutorial",        "Unlock",
        };
        assert(linkedCommandNames == accountedLinkedCommands);
        assert(bootstrap.environmentEffects().size() == 23);
        for (const auto& effect : bootstrap.environmentEffects()) {
            assert(effect.roomId >= 1 && effect.roomId <= 8);
            assert(effect.visible);
            assert(bootstrap.effects().presets.find(effect.effectType) !=
                   nullptr);
        }
        assert(bootstrap.bonuses().size() == 56);
        assert(bootstrap.persistentEffectsInSceneOrder().size() ==
               bootstrap.environmentEffects().size() +
                   bootstrap.bonuses().size());
        std::vector<std::int32_t> authoredPersistentEffectIds;
        for (const auto& room : bootstrap.rooms()) {
            for (const auto& node : room.scene.nodes()) {
                const bool isRegisteredPersistentSource = std::any_of(
                    bootstrap.persistentEffectsInSceneOrder().begin(),
                    bootstrap.persistentEffectsInSceneOrder().end(),
                    [&node](const auto& source) {
                        return source.objectId == node.id;
                    });
                if (isRegisteredPersistentSource) {
                    authoredPersistentEffectIds.push_back(node.id);
                }
            }
        }
        std::vector<std::int32_t> registeredPersistentEffectIds;
        for (const auto& source :
             bootstrap.persistentEffectsInSceneOrder()) {
            registeredPersistentEffectIds.push_back(source.objectId);
            const auto* preset =
                bootstrap.effects().presets.find(source.effectType);
            assert(preset != nullptr);
            // These room-authored sources do not consume native randomness
            // while choosing emitter lifetime or restart time at creation.
            // They can therefore share the scene traversal's single native
            // randomizer without moving unrelated gameplay selections.
            for (const auto& emitter : preset->emitters) {
                assert(emitter.systemMinimumLifetimeMilliseconds ==
                       emitter.systemMaximumLifetimeMilliseconds);
                assert(emitter.restartMinimumMilliseconds ==
                       emitter.restartMaximumMilliseconds);
            }
        }
        assert(registeredPersistentEffectIds == authoredPersistentEffectIds);
        assert(std::count_if(
                   bootstrap.bonuses().begin(), bootstrap.bonuses().end(),
                   [](const usm::game::LevelBonusAsset& bonus) {
                       return bonus.type == usm::game::LevelBonusType::Health;
                   }) == 15);
        assert(std::count_if(
                   bootstrap.bonuses().begin(), bootstrap.bonuses().end(),
                   [](const usm::game::LevelBonusAsset& bonus) {
                       return bonus.type ==
                              usm::game::LevelBonusType::SkillPoint;
                   }) == 41);
        assert(std::none_of(
            bootstrap.bonuses().begin(), bootstrap.bonuses().end(),
            [](const usm::game::LevelBonusAsset& bonus) {
                return bonus.type == usm::game::LevelBonusType::WebPower;
            }));
        assert(bootstrap.hints().size() == 1);
        const usm::game::LevelHintAsset& spiderSenseHint =
            bootstrap.hints().front();
        assert(spiderSenseHint.objectId == 1113);
        assert(spiderSenseHint.linkedObjectId == 288);
        assert(spiderSenseHint.roomId == 2);
        assert(spiderSenseHint.animationIndex == 0);
        assert(spiderSenseHint.spriteFile == "hintbb.bsprite");
        assert(!spiderSenseHint.visible);
        assert(spiderSenseHint.atlas.modules().size() == 20);
        assert(spiderSenseHint.atlas.frames().size() == 26);
        assert(spiderSenseHint.atlas.animations().size() == 9);
        assert(spiderSenseHint.texture.image().width == 256);
        assert(spiderSenseHint.texture.image().height == 256);
        assert(bootstrap.dropAreas().size() == 5);
        assert(bootstrap.dropObjects().size() == 5);
        for (const auto& drop : bootstrap.dropObjects()) {
            assert(drop.roomId == 8);
            assert(drop.delayMilliseconds >= 500);
            assert(drop.damage == 100.0F);
            assert(drop.effectType == "firesmoke_xp");
            assert(std::any_of(
                bootstrap.dropAreas().begin(), bootstrap.dropAreas().end(),
                [&drop](const auto& area) {
                    return area.objectId == drop.ownerAreaId &&
                           area.effectType == "explode_new";
                }));
        }
        assert(bootstrap.damageVolumes().size() == 4);
        assert((std::vector<std::int32_t>{
                    bootstrap.damageVolumes()[0].objectId,
                    bootstrap.damageVolumes()[1].objectId,
                    bootstrap.damageVolumes()[2].objectId,
                    bootstrap.damageVolumes()[3].objectId} ==
                std::vector<std::int32_t>{703, 754, 836, 837}));
        for (const auto& damage : bootstrap.damageVolumes()) {
            assert(damage.enabled);
            assert(damage.damage == 30.0F);
            assert(damage.damageType == 0);
        }
        assert(bootstrap.restoreTriggers().size() == 11);
        assert(bootstrap.restorePoints().size() == 11);
        assert(bootstrap.restoreTriggers().front().objectId == 539);
        assert(bootstrap.restoreTriggers().front().damage == 200.0F);
        assert(bootstrap.restoreTriggers()[1].objectId == 1176);
        assert(bootstrap.restoreTriggers()[1].damage == 50.0F);
        const auto roomSevenRestore = std::find_if(
            bootstrap.restoreTriggers().begin(),
            bootstrap.restoreTriggers().end(),
            [](const usm::game::LevelRestoreTriggerAsset& trigger) {
                return trigger.objectId == 554;
            });
        assert(roomSevenRestore != bootstrap.restoreTriggers().end());
        usm::game::LevelRestoreRuntime roomSevenRestoreRuntime;
        assert(roomSevenRestoreRuntime.bind(bootstrap.restoreTriggers(),
                                            bootstrap.restorePoints()));
        // The point is inside volume 554's long oriented extents, but native
        // obbox::test_obb rejects it in the world-space broad phase because
        // it is more than 1000 cm from the trigger center. This keeps the
        // authored Slide 1039 route clear.
        roomSevenRestoreRuntime.update(
            {-6252.81F, 6795.33F, 1873.50F}, 1);
        assert(!roomSevenRestoreRuntime.active());
        roomSevenRestoreRuntime.update(roomSevenRestore->position, 1);
        assert(roomSevenRestoreRuntime.active());
        const std::array<usm::game::LevelRestorePointAsset, 1>
            testRestorePoints{{{20, 1, {500.0F, 0.0F, 0.0F},
                                {0.0F, 1.0F, 0.0F}}}};
        const std::array<usm::game::LevelRestoreTriggerAsset, 1>
            testRestoreTriggers{{{10, 1, 20, {}, {},
                                  {1.0F, 1.0F, 1.0F},
                                  {200.0F, 200.0F, 200.0F}, 200.0F}}};
        usm::game::LevelRestoreRuntime restoreRuntime;
        assert(restoreRuntime.bind(testRestoreTriggers, testRestorePoints));
        usm::game::CinematicThread restoreBasicThread;
        restoreBasicThread.type = 1;
        usm::game::CinematicCommand disableRestore;
        disableRestore.name = "EnableTriggerRestore";
        disableRestore.attributes = {{"int", "^ID^TriggerRestore", "10"},
                                     {"bool", "enable", "false"}};
        assert(restoreRuntime.applyCinematicCommand(restoreBasicThread,
                                                    disableRestore));
        assert(restoreRuntime.enabled(10).has_value());
        assert(!*restoreRuntime.enabled(10));
        restoreRuntime.update({}, 1);
        assert(!restoreRuntime.active());
        disableRestore.attributes.back().value = "true";
        assert(restoreRuntime.applyCinematicCommand(restoreBasicThread,
                                                    disableRestore));
        assert(*restoreRuntime.enabled(10));
        disableRestore.attributes.front().value = "999";
        assert(restoreRuntime.applyCinematicCommand(restoreBasicThread,
                                                    disableRestore));
        assert(!restoreRuntime.enabled(999).has_value());
        disableRestore.attributes.front().value = "10";
        usm::game::CinematicThread restoreObjectThread;
        restoreObjectThread.type = 3;
        assert(!restoreRuntime.applyCinematicCommand(restoreObjectThread,
                                                     disableRestore));
        // Native CTriggerRestore tests the player's position point, not an
        // expanded player capsule, against the authored half extents.
        restoreRuntime.update({100.1F, 0.0F, 0.0F}, 1);
        assert(!restoreRuntime.active());
        restoreRuntime.update({0.0F, 0.0F, 100.1F}, 1);
        assert(!restoreRuntime.active());
        restoreRuntime.update({}, 1);
        assert(restoreRuntime.active());
        assert(restoreRuntime.blackOverlayAlpha() == 0.0F);
        restoreRuntime.update({}, 640);
        assert(restoreRuntime.blackOverlayAlpha() > 0.49F);
        assert(restoreRuntime.blackOverlayAlpha() < 0.51F);
        restoreRuntime.update({}, 639);
        assert(restoreRuntime.consumeEvents().empty());
        restoreRuntime.update({}, 1);
        auto restoreEvents = restoreRuntime.consumeEvents();
        assert(restoreEvents.size() == 1);
        assert(restoreEvents.front().trigger->objectId == 10);
        assert(restoreEvents.front().restorePoint->objectId == 20);
        assert(restoreRuntime.blackOverlayAlpha() == 1.0F);
        restoreRuntime.update({}, 511);
        assert(restoreRuntime.active());
        restoreRuntime.update({}, 1);
        assert(!restoreRuntime.active());
        assert(restoreRuntime.blackOverlayAlpha() == 0.0F);
        // Player::CanEnableTriggerRestore (0x0033ff14) rejects a dead
        // player, and CLevel::Update (0x003820bc) freezes an already active
        // restore while its own death screen is active.
        usm::game::LevelRestoreRuntime deadRestoreRuntime;
        assert(deadRestoreRuntime.bind(testRestoreTriggers,
                                       testRestorePoints));
        deadRestoreRuntime.update({}, 1, true, false);
        assert(!deadRestoreRuntime.active());
        deadRestoreRuntime.update({}, 1, false);
        assert(!deadRestoreRuntime.active());
        deadRestoreRuntime.update({}, 1, true);
        assert(deadRestoreRuntime.active());
        // Idle eligibility is not an update freeze after a restore starts.
        deadRestoreRuntime.update({}, 1280, true, false);
        assert(deadRestoreRuntime.blackOverlayAlpha() == 1.0F);
        deadRestoreRuntime.update({}, 10000, false);
        assert(deadRestoreRuntime.active());
        assert(deadRestoreRuntime.blackOverlayAlpha() == 1.0F);

        usm::game::LevelDeathRuntime deathRuntime;
        deathRuntime.update(true, 50);
        assert(deathRuntime.active());
        assert(deathRuntime.elapsedMilliseconds() == 0);
        assert(deathRuntime.blackOverlayAlpha() == 0.0F);
        deathRuntime.update(true, 1350);
        assert(deathRuntime.blackOverlayAlpha() == 0.0F);
        deathRuntime.update(true, 50);
        assert(deathRuntime.elapsedMilliseconds() == 1400);
        assert(deathRuntime.blackOverlayAlpha() > 0.56F);
        assert(deathRuntime.blackOverlayAlpha() < 0.58F);
        deathRuntime.update(true, 600);
        assert(deathRuntime.confirmationReady());
        assert(deathRuntime.blackOverlayAlpha() == 1.0F);
        deathRuntime.update(false, 1);
        assert(!deathRuntime.active());
        assert(!deathRuntime.confirmationReady());
        assert(deathRuntime.blackOverlayAlpha() == 0.0F);
        // CProgressBar failure uses CBlackScreen mode 1 with a native
        // ten-millisecond duration instead of the player's 2000 ms death
        // fade (CProgressBar::Update 0x0031a858).
        deathRuntime.update(true, 50, 10);
        assert(deathRuntime.active());
        assert(!deathRuntime.confirmationReady());
        deathRuntime.update(true, 10, 10);
        assert(deathRuntime.confirmationReady());
        assert(deathRuntime.blackOverlayAlpha() == 1.0F);
        deathRuntime.update(false, 1);
        usm::game::LevelDamageAsset testDamage;
        testDamage.objectId = 10;
        testDamage.position = {};
        testDamage.sizes = {200.0F, 100.0F, 200.0F};
        testDamage.damage = 30.0F;
        testDamage.enabled = true;
        usm::game::LevelDamageRuntime effectDamageRuntime;
        effectDamageRuntime.bind({&testDamage, 1});
        effectDamageRuntime.update({}, 1);
        auto damageEvents = effectDamageRuntime.consumeEvents();
        assert(damageEvents.size() == 1);
        assert(damageEvents.front().objectId == 10);
        assert(damageEvents.front().damage == 30.0F);
        assert(effectDamageRuntime.cooldownRemainingMilliseconds() == 1000);
        effectDamageRuntime.update({}, 999);
        assert(effectDamageRuntime.consumeEvents().empty());
        effectDamageRuntime.update({}, 1);
        damageEvents = effectDamageRuntime.consumeEvents();
        assert(damageEvents.size() == 1);
        effectDamageRuntime.update({1000.0F, 0.0F, 0.0F}, 1000);
        assert(effectDamageRuntime.consumeEvents().empty());

        const std::array<usm::game::LevelDropAreaAsset, 1> testDropAreas{{
            {1, 8, {0.0F, 0.0F, 0.0F}, {200.0F, 200.0F, 200.0F},
             "explode_new"},
        }};
        const std::array<usm::game::LevelDropObjectAsset, 1> testDropObjects{{
            {2, 1, 8, 800, 100.0F, {0.0F, 0.0F, 300.0F},
             {50.0F, 50.0F, 50.0F}, "firesmoke_xp"},
        }};
        usm::game::LevelDropRuntime dropRuntime;
        assert(dropRuntime.initialize(testDropAreas, testDropObjects));
        dropRuntime.update({}, 1);
        auto dropEvents = dropRuntime.consumeEvents();
        assert(dropEvents.size() == 1);
        assert(dropEvents.front().kind ==
               usm::game::LevelDropEventKind::Activated);
        assert(dropEvents.front().effectType == "explode_new");
        assert(dropRuntime.states().front().visible);
        assert(dropRuntime.states().front().phase ==
               usm::game::LevelDropPhase::Delay);
        dropRuntime.update({}, 799);
        dropEvents = dropRuntime.consumeEvents();
        assert(dropEvents.size() == 1);
        assert(dropEvents.front().kind ==
               usm::game::LevelDropEventKind::BeganFalling);
        assert(dropEvents.front().effectType == "firesmoke_xp");
        assert(dropRuntime.states().front().physicsEnabled);
        dropRuntime.update({}, 100);
        const float firstDropZ = dropRuntime.states().front().position.z;
        dropRuntime.update({}, 100);
        assert(dropRuntime.states().front().position.z < firstDropZ);
        dropRuntime.update({}, 100);
        dropEvents = dropRuntime.consumeEvents();
        assert(std::any_of(
            dropEvents.begin(), dropEvents.end(), [](const auto& event) {
                return event.kind ==
                           usm::game::LevelDropEventKind::HitPlayer &&
                       event.damage == 100.0F;
            }));

        usm::game::LevelHintRuntime hintRuntime;
        assert(hintRuntime.initialize(bootstrap.hints()));
        const auto* hintState = hintRuntime.find(1113);
        assert(hintState != nullptr && !hintState->visible);
        assert(hintState->frameIndex == 6);
        usm::game::CinematicThread hintThread;
        hintThread.objectId = 1113;
        usm::game::CinematicCommand showHint;
        showHint.name = "SetVisible";
        showHint.attributes.push_back({"bool", "Visible", "true"});
        assert(hintRuntime.applyCinematicCommand(hintThread, showHint));
        hintRuntime.update(99, [](std::int32_t objectId) {
            assert(objectId == 288);
            return usm::assets::Vector3{10.0F, 20.0F, 30.0F};
        });
        hintState = hintRuntime.find(1113);
        assert(hintState != nullptr && hintState->visible);
        assert(hintState->frameIndex == 6);
        assert(hintState->position.x == 10.0F);
        assert(hintState->position.y == 20.0F);
        assert(hintState->position.z == 250.0F);
        hintRuntime.update(1, {});
        hintState = hintRuntime.find(1113);
        assert(hintState != nullptr && hintState->frameIndex == 13);
        showHint.attributes.front().value = "false";
        assert(hintRuntime.applyCinematicCommand(hintThread, showHint));
        hintRuntime.update(500, {});
        hintState = hintRuntime.find(1113);
        assert(hintState != nullptr && !hintState->visible);
        assert(hintState->animationTimeMilliseconds == 100);
        assert(bootstrap.triggerSounds().size() == 2);
        assert(bootstrap.triggerSounds()[0].objectId == 40006);
        assert(bootstrap.triggerSounds()[0].eventName == "SFX_FIRE_TRAP");
        assert(bootstrap.triggerSounds()[0].roomId == 2);
        assert(!bootstrap.triggerSounds()[0].axisAlignedBox);
        assert(bootstrap.triggerSounds()[1].objectId == 40007);
        assert(bootstrap.triggerSounds()[1].roomId == 8);
        usm::game::LevelTriggerSoundRuntime triggerSoundRuntime;
        triggerSoundRuntime.bind(bootstrap.triggerSounds());
        auto triggerSoundEvents = triggerSoundRuntime.update(
            bootstrap.triggerSounds()[0].position);
        assert(triggerSoundEvents.size() == 1);
        assert(triggerSoundEvents[0].triggerId == 40006);
        assert(triggerSoundEvents[0].kind ==
               usm::game::TriggerSoundEventKind::Started);
        triggerSoundEvents = triggerSoundRuntime.update(
            {100000.0F, 100000.0F, 100000.0F});
        assert(triggerSoundEvents.size() == 1);
        assert(triggerSoundEvents[0].triggerId == 40006);
        assert(triggerSoundEvents[0].kind ==
               usm::game::TriggerSoundEventKind::Stopped);
        assert(bootstrap.mainScene().nodes().size() == 154);
        std::size_t animatedObjectNodeCount = 0;
        std::size_t environmentAnimatedObjectCount = 0;
        std::size_t cinematicActorObjectCount = 0;
        std::size_t comicNodeCount = 0;
        const auto verifyPresentationObjectCoverage =
            [&](const usm::assets::IrrScene& scene) {
            for (const auto& node : scene.nodes()) {
                if (node.gameType != "AnimatedObject" &&
                    node.gameType != "Comic") {
                    continue;
                }
                const auto object = std::find_if(
                    bootstrap.objects().begin(), bootstrap.objects().end(),
                    [&node](const auto& candidate) {
                        return candidate.objectId == node.id;
                    });
                if (node.gameType == "Comic") {
                    ++comicNodeCount;
                    assert(object != bootstrap.objects().end());
                    assert(object->kind == usm::game::LevelObjectKind::Comic);
                    continue;
                }

                ++animatedObjectNodeCount;
                const bool isIntroActor = std::any_of(
                    bootstrap.introActors().begin(),
                    bootstrap.introActors().end(),
                    [&node](const auto& actor) {
                        return actor.objectId == node.id;
                    });
                const bool isGameplayCinematicActor = std::any_of(
                    bootstrap.cinematics().begin(),
                    bootstrap.cinematics().end(),
                    [&node](const auto& cinematic) {
                        return std::any_of(
                            cinematic.actors.begin(), cinematic.actors.end(),
                            [&node](const auto& actor) {
                                return actor.objectId == node.id;
                            });
                    });
                if (isIntroActor || isGameplayCinematicActor) {
                    ++cinematicActorObjectCount;
                    assert(object == bootstrap.objects().end());
                } else {
                    ++environmentAnimatedObjectCount;
                    assert(object != bootstrap.objects().end());
                    assert(object->kind ==
                           usm::game::LevelObjectKind::Animated);
                }
            }
        };
        verifyPresentationObjectCoverage(bootstrap.mainScene());
        for (const auto& room : bootstrap.rooms()) {
            verifyPresentationObjectCoverage(room.scene);
        }
        assert(animatedObjectNodeCount == 28);
        assert(environmentAnimatedObjectCount == 15);
        assert(cinematicActorObjectCount == 13);
        assert(comicNodeCount == 15);
        assert(bootstrap.textCatalog().level().size() == 18);
        assert(bootstrap.textCatalog().tutorial().size() == 17);
        assert(bootstrap.textCatalog().main().size() > 0x24b);
        assert(bootstrap.textCatalog().main().at(0x24a) != nullptr);
        assert(bootstrap.textCatalog().main().at(0x24b) != nullptr);
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
        assert(cinematicUi.applyCommand(
            usm::game::CinematicCommand{0, -1, "PlayDAECamera", {}}));
        assert(cinematicUi.letterboxVisible());
        cinematicUi.setColladaMovieUi(false);
        assert(!cinematicUi.letterboxVisible());
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
        assert(cinematicUi.frame().tutorialPanelVisible);
        assert(cinematicUi.frame().tutorialButton == -1);
        assert(cinematicUi.frame().text ==
               u"Press [X] for a NORMAL ATTACK!");
        cinematicUi.update(2999, false);
        assert(cinematicUi.tutorialVisible());
        cinematicUi.update(1, false);
        assert(!cinematicUi.tutorialVisible());
        const usm::game::CinematicUiFrame quickTimePrompt =
            cinematicUi.frame(true, 0.5F);
        assert(quickTimePrompt.text == u"Press [A]");
        assert(quickTimePrompt.textVisible);
        assert(quickTimePrompt.tutorialPanelVisible);
        assert(quickTimePrompt.quickTimeEventVisible);
        const auto modalTutorialCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 40027;
            });
        assert(modalTutorialCinematic != bootstrap.cinematics().end());
        const auto modalTutorialCommand = std::find_if(
            modalTutorialCinematic->script.threads().back().commands.begin(),
            modalTutorialCinematic->script.threads().back().commands.end(),
            [](const usm::game::CinematicCommand& command) {
                return command.name == "Tutorial";
            });
        assert(modalTutorialCommand !=
               modalTutorialCinematic->script.threads().back().commands.end());
        assert(cinematicUi.applyCommand(*modalTutorialCommand));
        assert(cinematicUi.tutorialVisible());
        assert(cinematicUi.modalTutorialVisible());
        cinematicUi.update(1000, false);
        assert(cinematicUi.tutorialVisible());
        cinematicUi.update(0, true);
        assert(!cinematicUi.tutorialVisible());
        assert(!cinematicUi.modalTutorialVisible());
        const auto webSwingTutorialCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1115;
            });
        assert(webSwingTutorialCinematic != bootstrap.cinematics().end());
        const auto webSwingTutorialCommand = std::find_if(
            webSwingTutorialCinematic->script.threads().back().commands.begin(),
            webSwingTutorialCinematic->script.threads().back().commands.end(),
            [](const usm::game::CinematicCommand& command) {
                return command.name == "Tutorial";
            });
        assert(webSwingTutorialCommand != webSwingTutorialCinematic->script
                                              .threads()
                                              .back()
                                              .commands.end());
        assert(webSwingTutorialCommand->findAttribute("Timer") == nullptr);
        assert(cinematicUi.applyCommand(*webSwingTutorialCommand));
        assert(cinematicUi.tutorialVisible());
        assert(cinematicUi.modalTutorialVisible());
        assert(cinematicUi.frame().dimBackground);
        assert(cinematicUi.frame().text.find(u"[A]") !=
               std::u16string::npos);
        cinematicUi.update(1000, false);
        assert(cinematicUi.tutorialVisible());
        cinematicUi.update(0, true);
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
        const usm::game::CinematicUiFrame openingMessageFrame =
            cinematicUi.frame();
        assert(openingMessageFrame.text == *openingCaption);
        assert(openingMessageFrame.messagePanelVisible);
        const auto* openingMessageFace =
            openingMessageCommand->findAttribute("$MessageFace");
        assert(openingMessageFace != nullptr);
        assert(openingMessageFrame.messageFace ==
               std::stoi(openingMessageFace->value));
        assert(openingMessageFrame.messageDurationMilliseconds == 3850);
        assert(openingMessageFrame.messageElapsedMilliseconds == 0);
        cinematicUi.update(1000, false);
        assert(cinematicUi.frame().messageElapsedMilliseconds == 1000);
        cinematicUi.update(2850, false);
        assert(!cinematicUi.messageVisible());
        assert(cinematicUi.showComicCover(7));
        assert(cinematicUi.messageVisible());
        assert(cinematicUi.frame().informationPanel ==
               usm::game::InformationPanel::Expanded);
        assert(!cinematicUi.frame().messagePanelVisible);
        assert(!cinematicUi.frame().tutorialPanelVisible);
        assert(!cinematicUi.modalTutorialVisible());
        assert(cinematicUi.frame().messageFace == -1);
        assert(cinematicUi.frame().text.find(u'^') ==
               std::u16string::npos);
        assert(cinematicUi.frame().text.find(u" 7") ==
               std::u16string::npos);
        cinematicUi.update(4999, false);
        assert(cinematicUi.messageVisible());
        cinematicUi.update(1, false);
        assert(!cinematicUi.messageVisible());
        assert(cinematicUi.showComicCover(8));
        assert(cinematicUi.frame().informationPanel ==
               usm::game::InformationPanel::Compact);
        cinematicUi.update(2999, false);
        assert(cinematicUi.messageVisible());
        cinematicUi.update(1, false);
        assert(!cinematicUi.messageVisible());
        assert(cinematicUi.frame().informationPanel ==
               usm::game::InformationPanel::None);
        assert(cinematicUi.applyCommand(*openingMessageCommand));
        assert(cinematicUi.frame().messagePanelVisible);
        assert(cinematicUi.frame().informationPanel ==
               usm::game::InformationPanel::None);
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
        assert(initialCameraNode->userAttributes.at("mustInVisibleRoom") ==
               "5,7,8,6,2");
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
        const auto* explodePreset =
            bootstrap.effects().presets.find("explode_new");
        assert(explodePreset != nullptr);
        assert(explodePreset->emitters.size() == 4);
        const auto findEmitter = [](const usm::game::EffectPreset& preset,
                                    std::string_view name) {
            const auto match = std::find_if(
                preset.emitters.begin(), preset.emitters.end(),
                [name](const auto& emitter) { return emitter.name == name; });
            return match == preset.emitters.end() ? nullptr : &*match;
        };
        const auto* explosionFire = findEmitter(*explodePreset, "fire");
        const auto* explosionSparks = findEmitter(*explodePreset, "sparks");
        const auto* explosionRock = findEmitter(*explodePreset, "rock");
        assert(explosionFire != nullptr && explosionFire->hasRotation);
        assert(explosionFire->rotationPivot.x == 0.0F);
        assert(explosionFire->rotationSpeedDegreesPerSecond.x == 5.0F);
        assert(explosionFire->rotationSpeedDegreesPerSecond.y == 5.0F);
        assert(explosionFire->rotationSpeedDegreesPerSecond.z == 5.0F);
        assert(explosionSparks != nullptr && explosionSparks->hasGravity);
        assert(explosionSparks->gravity.z == -0.5F);
        assert(explosionSparks->gravityStartPercent == 0);
        assert(explosionSparks->gravityEndPercent == 100);
        assert(explosionRock != nullptr && explosionRock->hasSpin);
        assert(explosionRock->hasGravity);
        assert(explosionRock->spinMinimumDegrees == 0);
        assert(explosionRock->spinMaximumDegrees == 360);
        assert(bootstrap.effects().presets.find("rock_splash") != nullptr);
        assert(bootstrap.effects().presets.find("cartoon_hit_splash_big") !=
               nullptr);
        assert(bootstrap.effects().presets.find("cartoon_hit_splash") !=
               nullptr);
        const auto* blackWebSplashPreset =
            bootstrap.effects().presets.find("super_web_splash_black");
        assert(blackWebSplashPreset != nullptr);
        assert(blackWebSplashPreset->emitters.size() == 1);
        const auto& blackWebSmoke = blackWebSplashPreset->emitters.front();
        assert(blackWebSmoke.attractionAffectors.size() == 1);
        assert(blackWebSmoke.attractionAffectors.front().point.x == 0.0F);
        assert(blackWebSmoke.attractionAffectors.front().point.y == 0.0F);
        assert(blackWebSmoke.attractionAffectors.front().point.z == 0.0F);
        assert(blackWebSmoke.attractionAffectors.front().speed == 800.0F);
        assert(blackWebSmoke.attractionAffectors.front().attract);
        assert(blackWebSmoke.attractionAffectors.front().affectX);
        assert(blackWebSmoke.attractionAffectors.front().affectY);
        assert(blackWebSmoke.attractionAffectors.front().affectZ);
        assert(blackWebSmoke.affectorOrder.size() == 4);
        assert(blackWebSmoke.affectorOrder[0].kind ==
               usm::game::EffectAffectorKind::FadeOut);
        assert(blackWebSmoke.affectorOrder[1].kind ==
               usm::game::EffectAffectorKind::Spin);
        assert(blackWebSmoke.affectorOrder[2].kind ==
               usm::game::EffectAffectorKind::Attract);
        assert(blackWebSmoke.affectorOrder[3].kind ==
               usm::game::EffectAffectorKind::Size);
        constexpr std::string_view attractionFixture =
            "<effect name=\"attraction_test\"><ps><attributes>"
            "<string name=\"Name\" value=\"pull\"/>"
            "<vector3d name=\"Position\" value=\"0,0,0\"/>"
            "<vector3d name=\"Scale\" value=\"1,1,1\"/>"
            "<int name=\"SysMinLifeTime\" value=\"0\"/>"
            "<int name=\"SysMaxLifeTime\" value=\"0\"/>"
            "<int name=\"StartDelay\" value=\"-1001\"/>"
            "<vector3d name=\"Box\" value=\"10,0,0\"/>"
            "<vector3d name=\"Direction\" value=\"0,0,0\"/>"
            "<int name=\"MinParticlesPerSecond\" value=\"1\"/>"
            "<int name=\"MaxParticlesPerSecond\" value=\"1\"/>"
            "<float name=\"ParticleWidth\" value=\"10\"/>"
            "<float name=\"ParticleHeight\" value=\"10\"/>"
            "<color name=\"MinStartColor\" value=\"ffffffff\"/>"
            "<color name=\"MaxStartColor\" value=\"ffffffff\"/>"
            "<int name=\"MinLifeTime\" value=\"1000\"/>"
            "<int name=\"MaxLifeTime\" value=\"1000\"/>"
            "<enum name=\"Affector\" value=\"Attract\"/>"
            "<vector3d name=\"Point\" value=\"0,0,0\"/>"
            "<float name=\"Speed\" value=\"100\"/>"
            "<bool name=\"Attract\" value=\"true\"/>"
            "<bool name=\"AffectX\" value=\"true\"/>"
            "<bool name=\"AffectY\" value=\"true\"/>"
            "<bool name=\"AffectZ\" value=\"true\"/>"
            "<int name=\"FrameID\" value=\"0\"/>"
            "</attributes></ps></effect>";
        usm::game::EffectPresetDatabase attractionPresets;
        assert(attractionPresets.load(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(attractionFixture.data()),
            attractionFixture.size())));
        usm::game::LevelEffectRuntime attractionRuntime;
        assert(attractionRuntime.initialize(attractionPresets));
        assert(attractionRuntime.playEffect("attraction_test", {}));
        attractionRuntime.update(0);
        assert(attractionRuntime.particles().size() == 1);
        const float attractionStartX =
            attractionRuntime.particles().front().position.x;
        assert(attractionStartX > 0.0F);
        attractionRuntime.update(100);
        assert(std::abs(
                   attractionRuntime.particles().front().position.x -
                   attractionStartX + 10.0F) <
               0.001F);
        usm::game::LevelEffectRuntime roomVisibilityRuntime;
        assert(roomVisibilityRuntime.initialize(attractionPresets));
        assert(roomVisibilityRuntime.addPersistentEffect(
            "attraction_test", {}, 1, true, 1001));
        const std::array visibleRoom{true};
        const std::array hiddenRoom{false};
        roomVisibilityRuntime.update(0, visibleRoom);
        roomVisibilityRuntime.update(100, visibleRoom);
        assert(roomVisibilityRuntime.particles().size() == 1);
        const float beforeRoomHide =
            roomVisibilityRuntime.particles().front().position.x;
        roomVisibilityRuntime.update(500, hiddenRoom);
        assert(roomVisibilityRuntime.particles().front().position.x ==
               beforeRoomHide);
        // ISceneNode::OnAnimate (0x004093f4) does not recurse through a
        // hidden room. On return, doParticleSystem records and rejects the
        // accumulated >150 ms delta (0x0039f36c-0x0039f38e).
        roomVisibilityRuntime.update(16, visibleRoom);
        assert(roomVisibilityRuntime.particles().front().position.x ==
               beforeRoomHide);
        roomVisibilityRuntime.update(100, visibleRoom);
        assert(std::abs(roomVisibilityRuntime.particles().front().position.x -
                        beforeRoomHide) >
               1.0F);
        assert(bootstrap.effects().presets.find("bonus_green") != nullptr);
        assert(bootstrap.effects().presets.find("bonus_red") != nullptr);
        const auto* ambientFirePreset =
            bootstrap.effects().presets.find("big_firesomke");
        assert(ambientFirePreset != nullptr);
        assert(ambientFirePreset->emitters.front().colorAffectors.size() == 3);
        assert(ambientFirePreset->emitters.front()
                   .colorAffectors.front()
                   .startPercent == 0);
        assert(ambientFirePreset->emitters.front()
                   .colorAffectors.back()
                   .startPercent == 80);
        const auto* ambientSmoke = findEmitter(*ambientFirePreset, "smoke");
        assert(ambientSmoke != nullptr);
        assert(ambientSmoke->particleWidth == 100.0F);
        assert(ambientSmoke->particleHeight == 100.0F);
        assert(ambientSmoke->sizeAffectors.size() == 1);
        assert(ambientSmoke->sizeAffectors.front().targetWidth == 230.0F);
        assert(ambientSmoke->sizeAffectors.front().targetHeight == 230.0F);
        assert(ambientSmoke->scale.x == 5.0F);
        assert(ambientSmoke->scale.y == 5.0F);
        assert(ambientSmoke->sizeVariationPercent == 0);
        assert(ambientSmoke->globalParticles);
        assert(ambientSmoke->directionalRotation);
        assert(ambientSmoke->projectDirection);
        assert(ambientSmoke->restartMinimumMilliseconds == -1);
        assert(ambientSmoke->restartMaximumMilliseconds == -1);
        assert(ambientSmoke->maximumAngleDegreesXY == 0);
        assert(ambientSmoke->maximumAngleDegreesYZ == 0);
        assert(ambientSmoke->maximumAngleDegreesXZ == 20);
        usm::game::LevelEffectRuntime ambientScaleRuntime;
        assert(ambientScaleRuntime.initialize(bootstrap.effects().presets));
        assert(ambientScaleRuntime.playEffect("big_firesomke", {}, 1));
        ambientScaleRuntime.update(0);
        ambientScaleRuntime.update(150);
        const auto ambientSmokeParticle = std::find_if(
            ambientScaleRuntime.particles().begin(),
            ambientScaleRuntime.particles().end(),
            [](const auto& particle) { return particle.frameId == 0; });
        assert(ambientSmokeParticle != ambientScaleRuntime.particles().end());
        assert(ambientSmokeParticle->width >= 100.0F &&
               ambientSmokeParticle->width < 101.0F);
        assert(ambientSmokeParticle->height >= 100.0F &&
               ambientSmokeParticle->height < 101.0F);
        const auto* bigFirePreset =
            bootstrap.effects().presets.find("bigfire_xp");
        assert(bigFirePreset != nullptr);
        const auto* bigFireSmoke = findEmitter(*bigFirePreset, "smoke");
        assert(bigFireSmoke != nullptr);
        assert(bigFireSmoke->sizeAffectors.size() == 2);
        // The scene serializes the long growth stage before its 0-10%
        // initializer. Native addAffector preserves that order; it does not
        // sort stages by percentage.
        assert(bigFireSmoke->sizeAffectors[0].targetWidth == 200.0F);
        assert(bigFireSmoke->sizeAffectors[0].variationPercent == 50);
        assert(bigFireSmoke->sizeAffectors[0].startPercent == 10);
        assert(bigFireSmoke->sizeAffectors[0].endPercent == 100);
        assert(bigFireSmoke->sizeAffectors[1].targetWidth == 50.0F);
        assert(bigFireSmoke->sizeAffectors[1].variationPercent == 0);
        assert(bigFireSmoke->sizeAffectors[1].startPercent == 0);
        assert(bigFireSmoke->sizeAffectors[1].endPercent == 10);
        usm::game::LevelEffectRuntime stagedSizeRuntime;
        assert(stagedSizeRuntime.initialize(bootstrap.effects().presets));
        assert(stagedSizeRuntime.playEffect("bigfire_xp", {}, 1));
        stagedSizeRuntime.update(0);
        stagedSizeRuntime.update(150);
        const auto stagedSmokeWidth = [&stagedSizeRuntime]() {
            const auto particle = std::find_if(
                stagedSizeRuntime.particles().begin(),
                stagedSizeRuntime.particles().end(),
                [](const auto& state) { return state.frameId == 0; });
            assert(particle != stagedSizeRuntime.particles().end());
            return particle->width;
        };
        stagedSizeRuntime.update(149);
        assert(std::abs(stagedSmokeWidth() - 50.0F) < 0.01F);
        for (std::size_t step = 0; step < 3; ++step) {
            stagedSizeRuntime.update(150);
        }
        assert(stagedSmokeWidth() > 60.0F);
        assert(stagedSmokeWidth() < 140.0F);
        assert(bootstrap.effects().atlas.modules().size() == 16);
        assert(bootstrap.effects().atlas.frames().size() == 16);
        assert(bootstrap.effects().texture.image().width == 256);
        assert(bootstrap.effects().texture.image().height == 256);
        usm::game::NativeRandomizer effectRandomizer;
        usm::game::LevelEffectRuntime effectRuntime;
        assert(effectRuntime.initialize(bootstrap.effects().presets,
                                        &effectRandomizer));
        usm::game::LevelBonusRuntime bonusRuntime;
        assert(bonusRuntime.initialize(bootstrap.bonuses()));
        assert(bonusRuntime.visibleBonusCount() == 56);
        const auto initialBonusCheckPoint =
            bonusRuntime.saveCheckPointState();
        const usm::game::LevelBonusAsset& collectedBonus =
            bootstrap.bonuses().front();
        const usm::assets::Vector3 collectionPosition{
            collectedBonus.position.x, collectedBonus.position.y,
            collectedBonus.position.z - 100.0F};
        bonusRuntime.update(collectionPosition, 1);
        const auto collectedBonusIds =
            bonusRuntime.consumeCollectedBonusIds();
        assert(collectedBonusIds.size() == 1);
        assert(collectedBonusIds.front() == collectedBonus.objectId);
        assert(bonusRuntime.visibleBonusCount() == 55);
        const auto collectedBonusCheckPoint =
            bonusRuntime.saveCheckPointState();
        assert(bonusRuntime.orbs().size() == 1);
        assert(bonusRuntime.orbs().front().trailHalfWidth >
               bonusRuntime.orbs().front().headHalfWidth);
        bonusRuntime.update(collectionPosition, 2100);
        const auto bonusGrants = bonusRuntime.consumeGrants();
        assert(bonusGrants.size() == 1);
        assert(bonusGrants.front().objectId == collectedBonus.objectId);
        assert(bonusGrants.front().amount ==
               (collectedBonus.type == usm::game::LevelBonusType::Health
                    ? 80
                    : 5));
        assert(bonusRuntime.orbs().empty());
        if (collectedBonus.type == usm::game::LevelBonusType::SkillPoint) {
            assert(bonusRuntime.showSkillPointTotal());
            bonusRuntime.update(collectionPosition, 1000);
            assert(bonusRuntime.skillPointPopup().visible);
            assert(bonusRuntime.skillPointPopup().amount == 5);
            bonusRuntime.update(collectionPosition, 2000);
            assert(!bonusRuntime.skillPointPopup().visible);
        }
        assert(bonusRuntime.loadCheckPointState(initialBonusCheckPoint));
        assert(bonusRuntime.visibleBonusCount() == 56);
        assert(bonusRuntime.loadCheckPointState(collectedBonusCheckPoint));
        assert(bonusRuntime.visibleBonusCount() == 55);
        usm::game::LevelEffectRuntime environmentEffectRuntime;
        assert(environmentEffectRuntime.initialize(
            bootstrap.effects().presets));
        for (const auto& effect : bootstrap.environmentEffects()) {
            assert(environmentEffectRuntime.addPersistentEffect(
                effect.effectType, effect.position, effect.roomId,
                effect.visible, effect.objectId));
        }
        assert(environmentEffectRuntime.setPersistentEffectVisible(
            bootstrap.environmentEffects().front().objectId, false));
        const auto environmentEffectCheckPoint =
            environmentEffectRuntime.saveCheckPointState();
        environmentEffectRuntime.update(0);
        for (std::size_t step = 0; step < 8; ++step) {
            environmentEffectRuntime.update(125);
        }
        assert(!environmentEffectRuntime.particles().empty());
        assert(std::any_of(
            environmentEffectRuntime.particles().begin(),
            environmentEffectRuntime.particles().end(),
            [](const auto& particle) {
                return (particle.color >> 24U) != 0;
            }));
        assert(std::all_of(
            environmentEffectRuntime.particles().begin(),
            environmentEffectRuntime.particles().end(),
            [](const auto& particle) {
                return particle.roomId >= 1 && particle.roomId <= 8;
            }));
        assert(environmentEffectRuntime.setPersistentEffectVisible(
            bootstrap.environmentEffects().front().objectId, true));
        assert(environmentEffectRuntime.playEffect(
            "cartoon_hit_splash_big", {}, 1));
        environmentEffectRuntime.update(1);
        assert(environmentEffectRuntime.loadCheckPointState(
            environmentEffectCheckPoint));
        assert(environmentEffectRuntime.particles().empty());
        const auto reloadedEffectCheckPoint =
            environmentEffectRuntime.saveCheckPointState();
        assert(reloadedEffectCheckPoint.persistentEffects.size() ==
               environmentEffectCheckPoint.persistentEffects.size());
        assert(reloadedEffectCheckPoint.persistentEffects.front().visible ==
               environmentEffectCheckPoint.persistentEffects.front().visible);
        usm::game::CinematicCommand playHitEffect;
        playHitEffect.name = "PlayEffect";
        playHitEffect.attributes = {
            {"string", "$EffectType", "cartoon_hit_splash_big"},
            {"vector3d", "abspos", "10.0, 20.0, 30.0"},
        };
        assert(effectRuntime.applyCinematicCommand(playHitEffect));
        // A zero-delay native emitter is renderable on the same frame it is
        // thrown; a zero-time flush must instantiate it without aging it.
        effectRuntime.update(0);
        assert(effectRuntime.particles().size() == 10);
        // Exact call-order regression for CFpsParticleBoxEmitter::emitt
        // (0x0039ccd8): one random rate plus seven samples for each of the
        // two `main` particles, then one random rate plus eight samples for
        // each of the eight `main2` particles.
        assert(effectRandomizer.state() == 66400094);
        assert(effectRuntime.particles().front().frameId == 2);
        assert(effectRuntime.particles().front().width > 0.0F);
        assert(std::all_of(
            effectRuntime.particles().begin(), effectRuntime.particles().end(),
            [](const auto& particle) {
                return particle.directionalRotation &&
                       !particle.hasSpinAffector;
            }));
        assert(std::any_of(
            effectRuntime.particles().begin(), effectRuntime.particles().end(),
            [](const auto& particle) { return particle.projectDirection; }));
        assert(std::any_of(
            effectRuntime.particles().begin(), effectRuntime.particles().end(),
            [](const auto& particle) { return !particle.projectDirection; }));
        std::vector<usm::assets::Vector3> hitStartPositions;
        for (const auto& particle : effectRuntime.particles()) {
            hitStartPositions.push_back(particle.position);
        }
        effectRuntime.update(50);
        assert(effectRuntime.particles().size() == hitStartPositions.size());
        bool hitParticleMoved = false;
        for (std::size_t index = 0; index < hitStartPositions.size(); ++index) {
            const auto& current = effectRuntime.particles()[index].position;
            const auto& start = hitStartPositions[index];
            const float dx = current.x - start.x;
            const float dy = current.y - start.y;
            const float dz = current.z - start.z;
            hitParticleMoved |=
                std::sqrt(dx * dx + dy * dy + dz * dz) > 95.0F;
            const auto& previous =
                effectRuntime.particles()[index].previousPosition;
            assert(std::abs(previous.x - start.x) < 0.001F);
            assert(std::abs(previous.y - start.y) < 0.001F);
            assert(std::abs(previous.z - start.z) < 0.001F);
        }
        assert(hitParticleMoved);

        const usm::assets::Vector3 billboardRight{1.0F, 0.0F, 0.0F};
        const usm::assets::Vector3 billboardUp{0.0F, 0.0F, 1.0F};
        const usm::assets::Vector3 billboardForward{0.0F, 1.0F, 0.0F};
        const auto nearVector = [](const usm::assets::Vector3& actual,
                                   const usm::assets::Vector3& expected) {
            return std::abs(actual.x - expected.x) < 0.0001F &&
                   std::abs(actual.y - expected.y) < 0.0001F &&
                   std::abs(actual.z - expected.z) < 0.0001F;
        };
        usm::game::EffectParticleState projectedParticle;
        projectedParticle.position = {10.0F, 0.0F, 0.0F};
        projectedParticle.emitterDirection = billboardUp;
        projectedParticle.directionalRotation = true;
        projectedParticle.projectDirection = true;
        auto axes = usm::game::effectBillboardAxes(
            projectedParticle, billboardRight, billboardUp,
            billboardForward);
        assert(nearVector(axes.right, {0.0F, 0.0F, -1.0F}));
        assert(nearVector(axes.up, {1.0F, 0.0F, 0.0F}));

        projectedParticle.projectDirection = false;
        axes = usm::game::effectBillboardAxes(
            projectedParticle, billboardRight, billboardUp,
            billboardForward);
        assert(nearVector(axes.right, {0.0F, 0.0F, -1.0F}));
        assert(nearVector(axes.up, {1.0F, 0.0F, 0.0F}));

        // render's affector-type-6 branch suppresses directional alignment.
        projectedParticle.projectDirection = true;
        projectedParticle.hasSpinAffector = true;
        axes = usm::game::effectBillboardAxes(
            projectedParticle, billboardRight, billboardUp,
            billboardForward);
        assert(nearVector(axes.right, billboardRight));
        assert(nearVector(axes.up, billboardUp));
        effectRuntime.update(125);
        effectRuntime.update(125);
        assert(effectRuntime.particles().empty());
        assert(effectRuntime.initialize(bootstrap.effects().presets));
        usm::game::CinematicCommand playExplosion;
        playExplosion.name = "PlayEffect";
        playExplosion.attributes = {
            {"string", "$EffectType", "explode_new"},
            {"vector3d", "abspos", "10.0, 20.0, 30.0"},
        };
        assert(effectRuntime.applyCinematicCommand(playExplosion));
        effectRuntime.update(0);
        effectRuntime.update(101);
        const auto firstParticleWithFrame =
            [&](std::int32_t frameId) -> const usm::game::EffectParticleState* {
            const auto match = std::find_if(
                effectRuntime.particles().begin(),
                effectRuntime.particles().end(),
                [frameId](const auto& particle) {
                    return particle.frameId == frameId;
                });
            return match == effectRuntime.particles().end() ? nullptr : &*match;
        };
        const auto* initialSpark = firstParticleWithFrame(5);
        const auto* initialRock = firstParticleWithFrame(3);
        assert(initialSpark != nullptr && initialRock != nullptr);
        const float initialSparkZ = initialSpark->position.z;
        const float initialRockRotation = initialRock->rotationDegrees;
        effectRuntime.update(100);
        const float firstSparkStep =
            firstParticleWithFrame(5)->position.z - initialSparkZ;
        effectRuntime.update(100);
        const float secondSparkStep =
            firstParticleWithFrame(5)->position.z - initialSparkZ -
            firstSparkStep;
        assert(secondSparkStep < firstSparkStep);
        assert(std::abs(firstParticleWithFrame(3)->rotationDegrees -
                        initialRockRotation) >
               0.01F);
        assert(effectRuntime.initialize(bootstrap.effects().presets));
        assert(effectRuntime.playEffect("explode_new", {1.0F, 2.0F, 3.0F},
                                        8));
        effectRuntime.update(0);
        effectRuntime.update(101);
        assert(!effectRuntime.particles().empty());
        assert(std::all_of(
            effectRuntime.particles().begin(), effectRuntime.particles().end(),
            [](const auto& particle) { return particle.roomId == 8; }));
        playHitEffect.attributes.front().value = "missing_effect";
        assert(!effectRuntime.applyCinematicCommand(playHitEffect));
        std::size_t authoredEffectCommandCount = 0;
        std::vector<std::string> authoredEffectTypes;
        const auto validateEffectCommands =
            [&](const usm::game::CinematicScript& script) {
                for (const auto& thread : script.threads()) {
                    for (const auto& command : thread.commands) {
                        if (command.name != "PlayEffect") {
                            continue;
                        }
                        ++authoredEffectCommandCount;
                        const auto* type =
                            command.findAttribute("$EffectType");
                        assert(type != nullptr);
                        assert(command.findAttribute("abspos") != nullptr);
                        assert(bootstrap.effects().presets.find(type->value) !=
                               nullptr);
                        if (std::find(authoredEffectTypes.begin(),
                                      authoredEffectTypes.end(),
                                      type->value) ==
                            authoredEffectTypes.end()) {
                            authoredEffectTypes.push_back(type->value);
                        }
                    }
                }
            };
        validateEffectCommands(bootstrap.introScript());
        validateEffectCommands(bootstrap.introStartScript());
        for (const auto& cinematic : bootstrap.cinematics()) {
            if (cinematic.scriptAvailable) {
                validateEffectCommands(cinematic.script);
            }
        }
        assert(authoredEffectCommandCount > 0);
        std::sort(authoredEffectTypes.begin(), authoredEffectTypes.end());
        assert((authoredEffectTypes == std::vector<std::string>{
                                           "cartoon_hit_splash_big",
                                           "explode_new", "rock_splash"}));
        assert(bootstrap.objects().size() == 136);
        assert(!bootstrap.objectArchetypes().empty());
        usm::game::LevelObjectRuntime objectRuntime;
        assert(objectRuntime.initialize(bootstrap));
        assert(objectRuntime.states().size() == bootstrap.objects().size());
        const auto roomTwoWebWallAsset = std::find_if(
            bootstrap.objects().begin(), bootstrap.objects().end(),
            [](const auto& object) { return object.objectId == 1116; });
        assert(roomTwoWebWallAsset != bootstrap.objects().end());
        assert(roomTwoWebWallAsset->kind ==
               usm::game::LevelObjectKind::SpiderWebWall);
        assert(roomTwoWebWallAsset->hasCollision);
        assert(roomTwoWebWallAsset->hasCollisionBounds);
        const auto& roomTwoWebWallArchetype =
            bootstrap.objectArchetypes()[
                roomTwoWebWallAsset->archetypeIndex];
        const auto roomTwoWebWallBbox = std::find_if(
            roomTwoWebWallArchetype.mesh.sceneGeometries().begin(),
            roomTwoWebWallArchetype.mesh.sceneGeometries().end(),
            [](const auto& geometry) { return geometry.name == "bbox"; });
        assert(roomTwoWebWallBbox !=
               roomTwoWebWallArchetype.mesh.sceneGeometries().end());
        assert(roomTwoWebWallBbox->meshBuffers.size() == 1);
        assert(roomTwoWebWallBbox->meshBuffers.front().indices.size() == 72);
        assert(objectRuntime.find(1116)->archetype ==
               &roomTwoWebWallArchetype);
        assert(objectRuntime.find(1116)->physicsEnabled);
        assert(objectRuntime.find(1116)->collisionEnabled);

        // CSpiderWebWall::Init (0x0031ee04) creates collision from the
        // authored `bbox` child even though the IRR node has Collision=false,
        // then setFlagsAll (0x003d8cc8) assigns 0x06 to every triangle. Keep
        // the exact transformed 24-triangle mesh, and remove it when the
        // watcher hides the wall after its close animation.
        usm::game::LevelObjectRuntime isolatedWebWallRuntime;
        assert(isolatedWebWallRuntime.initialize(bootstrap));
        for (const auto& state : isolatedWebWallRuntime.states()) {
            if (state.asset == nullptr) {
                continue;
            }
            const auto kind = state.asset->kind;
            if (kind == usm::game::LevelObjectKind::SpiderWebWall ||
                kind == usm::game::LevelObjectKind::SlideCar ||
                kind == usm::game::LevelObjectKind::BrokenBridge ||
                kind == usm::game::LevelObjectKind::Platform ||
                kind == usm::game::LevelObjectKind::ElectricPlatform) {
                assert(isolatedWebWallRuntime.setRuntimeState(
                    state.asset->objectId, state.position, false,
                    state.physicsEnabled));
            }
        }
        assert(isolatedWebWallRuntime.setRuntimeState(
            1116, roomTwoWebWallAsset->position, true, true));
        usm::assets::ColladaGeometry webWallCollisionSeed;
        webWallCollisionSeed.name = "collision_seed";
        webWallCollisionSeed.vertices = {
            {{100000.0F, 100000.0F, 100000.0F}},
            {{100100.0F, 100000.0F, 100000.0F}},
            {{100000.0F, 100100.0F, 100000.0F}},
        };
        usm::assets::ColladaMeshBuffer webWallCollisionSeedBuffer;
        webWallCollisionSeedBuffer.indices = {0, 1, 2};
        webWallCollisionSeed.meshBuffers.push_back(
            webWallCollisionSeedBuffer);
        const std::array webWallCollisionSeedSet{webWallCollisionSeed};
        usm::game::LevelCollision webWallCollision;
        assert(webWallCollision.build(webWallCollisionSeedSet));
        const std::size_t webWallSeedTriangleCount =
            webWallCollision.triangleCount();
        assert(webWallCollision.updateObjectColliders(
            isolatedWebWallRuntime.states()));
        assert(webWallCollision.triangleCount() ==
               webWallSeedTriangleCount + 24U);
        const auto& webWallMatrix =
            isolatedWebWallRuntime.find(1116)->worldTransform;
        const usm::assets::Vector3 webWallLocalCenter{
            (roomTwoWebWallAsset->collisionLocalMinimum.x +
             roomTwoWebWallAsset->collisionLocalMaximum.x) * 0.5F,
            (roomTwoWebWallAsset->collisionLocalMinimum.y +
             roomTwoWebWallAsset->collisionLocalMaximum.y) * 0.5F,
            (roomTwoWebWallAsset->collisionLocalMinimum.z +
             roomTwoWebWallAsset->collisionLocalMaximum.z) * 0.5F};
        const usm::assets::Vector3 webWallWorldCenter{
            webWallLocalCenter.x * webWallMatrix[0] +
                webWallLocalCenter.y * webWallMatrix[4] +
                webWallLocalCenter.z * webWallMatrix[8] +
                webWallMatrix[12],
            webWallLocalCenter.x * webWallMatrix[1] +
                webWallLocalCenter.y * webWallMatrix[5] +
                webWallLocalCenter.z * webWallMatrix[9] +
                webWallMatrix[13],
            webWallLocalCenter.x * webWallMatrix[2] +
                webWallLocalCenter.y * webWallMatrix[6] +
                webWallLocalCenter.z * webWallMatrix[10] +
                webWallMatrix[14]};
        usm::assets::Vector3 webWallNormal{
            webWallMatrix[1] * webWallMatrix[6] -
                webWallMatrix[2] * webWallMatrix[5],
            webWallMatrix[2] * webWallMatrix[4] -
                webWallMatrix[0] * webWallMatrix[6],
            webWallMatrix[0] * webWallMatrix[5] -
                webWallMatrix[1] * webWallMatrix[4]};
        const float webWallNormalLength = std::sqrt(
            webWallNormal.x * webWallNormal.x +
            webWallNormal.y * webWallNormal.y +
            webWallNormal.z * webWallNormal.z);
        assert(webWallNormalLength > 0.0F);
        webWallNormal.x /= webWallNormalLength;
        webWallNormal.y /= webWallNormalLength;
        webWallNormal.z /= webWallNormalLength;
        const usm::assets::Vector3 webWallSegmentStart{
            webWallWorldCenter.x - webWallNormal.x * 100.0F,
            webWallWorldCenter.y - webWallNormal.y * 100.0F,
            webWallWorldCenter.z - webWallNormal.z * 100.0F};
        const usm::assets::Vector3 webWallSegmentEnd{
            webWallWorldCenter.x + webWallNormal.x * 100.0F,
            webWallWorldCenter.y + webWallNormal.y * 100.0F,
            webWallWorldCenter.z + webWallNormal.z * 100.0F};
        const auto visibleWebWallHit = webWallCollision.segmentFirstHit(
            webWallSegmentStart, webWallSegmentEnd);
        assert(visibleWebWallHit.has_value());
        assert(visibleWebWallHit->objectId == 1116);
        assert(visibleWebWallHit->physicsFlags ==
               (usm::game::LevelPhysicsFlags::Wall |
                usm::game::LevelPhysicsFlags::DoubleSided));
        assert(isolatedWebWallRuntime.setRuntimeState(
            1116, roomTwoWebWallAsset->position, false, true));
        assert(webWallCollision.updateObjectColliders(
            isolatedWebWallRuntime.states()));
        assert(webWallCollision.triangleCount() ==
               webWallSeedTriangleCount);
        assert(!webWallCollision.segmentFirstHit(webWallSegmentStart,
                                                  webWallSegmentEnd));
        const auto roomTwoHostageAsset = std::find_if(
            bootstrap.objects().begin(), bootstrap.objects().end(),
            [](const auto& object) { return object.objectId == 30018; });
        assert(roomTwoHostageAsset != bootstrap.objects().end());
        assert(roomTwoHostageAsset->kind ==
               usm::game::LevelObjectKind::Hostage);
        assert(roomTwoHostageAsset->roomId == 2);
        assert(roomTwoHostageAsset->hostageAnimations ==
               (std::array<std::string, 4>{
                   "tied", "tied_to_idle", "idle_to_thanks",
                   "idle_watching_idle"}));
        assert(roomTwoHostageAsset->hostageEnableRadius == 200.0F);
        assert(roomTwoHostageAsset->hostageHealthOrbCount == 10);
        assert(roomTwoHostageAsset->hostageSkillPointOrbCount == 0);
        assert(roomTwoHostageAsset->hostageHintHeight == 180.0F);
        assert(roomTwoHostageAsset->hostageButtonHeight == -101.0F);
        assert(roomTwoHostageAsset->hostageIsWoman);

        usm::game::LevelBonusRuntime hostageBonuses;
        assert(hostageBonuses.initialize(bootstrap.bonuses()));
        const std::size_t hostageInitialBonusCount =
            hostageBonuses.states().size();
        usm::game::LevelHostageRuntime hostageRuntime;
        assert(hostageRuntime.initialize(bootstrap, &objectRuntime));
        assert(hostageRuntime.states().size() == 4);
        assert(objectRuntime.find(30018)->activeAnimation == "tied");
        assert(objectRuntime.find(30018)->animationLoops);
        usm::game::GameplayPlayer hostagePlayer;
        assert(hostagePlayer.initialize(bootstrap.player(), nullptr,
                                        &playerStateConfigs));
        hostagePlayer.restoreAt(roomTwoHostageAsset->position,
                                {1.0F, 0.0F, 0.0F});
        assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                     hostageBonuses, 0, false));
        assert(hostageRuntime.contextPromptVisible());
        assert(hostageRuntime.canStartRescue(hostagePlayer));
        assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                     hostageBonuses, 0, true));
        assert(hostagePlayer.activeStateId() == 27);
        assert(hostagePlayer.activeAnimation() == "stand_to_unhitch");
        hostagePlayer.update({}, {}, 1000);
        assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                     hostageBonuses, 0, false));
        assert(hostageRuntime.quickTimeActive());
        assert(hostagePlayer.activeStateId() == 28);
        assert(hostagePlayer.activeAnimation() == "unhitch_to_unhitch");
        for (std::int32_t action = 0; action < 8; ++action) {
            assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                         hostageBonuses, 1, true));
        }
        assert(!hostageRuntime.quickTimeActive());
        assert(hostagePlayer.activeStateId() == 29);
        assert(hostagePlayer.activeAnimation() == "unhitch_to_stand");
        const auto hostageCuttingCues = hostageRuntime.consumeSoundCues();
        assert(hostageCuttingCues.size() == 2);
        assert(hostageCuttingCues.front().voxSoundId == 0x18b);
        assert(hostageCuttingCues.front().action ==
               usm::game::HostageSoundAction::StartLoop);
        assert(hostageCuttingCues.back().action ==
               usm::game::HostageSoundAction::StopLoop);
        hostagePlayer.update({}, {}, 1000);
        assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                     hostageBonuses, 0, false));
        assert(hostageRuntime.find(30018)->phase ==
               usm::game::HostageRescuePhase::Release);
        assert(hostageBonuses.states().size() ==
               hostageInitialBonusCount +
                   static_cast<std::size_t>(
                       roomTwoHostageAsset->hostageHealthOrbCount +
                       roomTwoHostageAsset->hostageSkillPointOrbCount));
        objectRuntime.advanceAnimations(100000);
        assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                     hostageBonuses, 0, false));
        assert(hostageRuntime.find(30018)->phase ==
               usm::game::HostageRescuePhase::Thank);
        objectRuntime.advanceAnimations(100000);
        assert(hostageRuntime.update(hostagePlayer, objectRuntime,
                                     hostageBonuses, 0, false));
        assert(hostageRuntime.find(30018)->phase ==
               usm::game::HostageRescuePhase::Freed);
        assert(objectRuntime.find(30018)->activeAnimation ==
               "idle_watching_idle");
        assert(objectRuntime.find(30018)->animationLoops);
        const auto hostageThankCues = hostageRuntime.consumeSoundCues();
        assert(hostageThankCues.size() == 1);
        assert(hostageThankCues.front().voxSoundId == 0xa3);
        hostageBonuses.update(roomTwoHostageAsset->position, 3000);
        assert(hostageBonuses.consumeGrants().size() ==
               static_cast<std::size_t>(
                   roomTwoHostageAsset->hostageHealthOrbCount +
                   roomTwoHostageAsset->hostageSkillPointOrbCount));

        usm::game::LevelObjectRuntime failedHostageObjects;
        assert(failedHostageObjects.initialize(bootstrap));
        usm::game::LevelBonusRuntime failedHostageBonuses;
        assert(failedHostageBonuses.initialize(bootstrap.bonuses()));
        usm::game::LevelHostageRuntime failedHostageRuntime;
        assert(failedHostageRuntime.initialize(bootstrap,
                                               &failedHostageObjects));
        usm::game::GameplayPlayer failedHostagePlayer;
        assert(failedHostagePlayer.initialize(bootstrap.player(), nullptr,
                                              &playerStateConfigs));
        failedHostagePlayer.restoreAt(roomTwoHostageAsset->position,
                                      {1.0F, 0.0F, 0.0F});
        assert(failedHostageRuntime.update(
            failedHostagePlayer, failedHostageObjects,
            failedHostageBonuses, 0, false));
        assert(failedHostageRuntime.update(
            failedHostagePlayer, failedHostageObjects,
            failedHostageBonuses, 0, true));
        failedHostagePlayer.update({}, {}, 1000);
        assert(failedHostageRuntime.update(
            failedHostagePlayer, failedHostageObjects,
            failedHostageBonuses, 0, false));
        assert(failedHostageRuntime.quickTimeActive());
        assert(failedHostageRuntime.update(
            failedHostagePlayer, failedHostageObjects,
            failedHostageBonuses, 4000, false));
        assert(failedHostageRuntime.find(30018)->phase ==
               usm::game::HostageRescuePhase::Tied);
        assert(failedHostagePlayer.activeStateId() == 0);
        assert(failedHostageObjects.find(30018)->activeAnimation == "tied");
        assert(objectRuntime.find(20032) != nullptr);
        assert(objectRuntime.find(1151) != nullptr);
        assert(objectRuntime.find(40010) != nullptr);
        const auto slideCarAsset = std::find_if(
            bootstrap.objects().begin(), bootstrap.objects().end(),
            [](const auto& object) { return object.objectId == 1210; });
        assert(slideCarAsset != bootstrap.objects().end());
        assert(slideCarAsset->kind ==
               usm::game::LevelObjectKind::SlideCar);
        assert(slideCarAsset->hasCollisionBounds);
        assert(slideCarAsset->hasCollision);
        assert(slideCarAsset->collisionLocalMinimum.x < -150.0F);
        assert(slideCarAsset->collisionLocalMinimum.x > -160.0F);
        assert(slideCarAsset->collisionLocalMinimum.y < -530.0F);
        assert(slideCarAsset->collisionLocalMinimum.y > -545.0F);
        assert(slideCarAsset->collisionLocalMaximum.z > 315.0F);
        assert(slideCarAsset->collisionLocalMaximum.z < 325.0F);
        assert(objectRuntime.find(1210) != nullptr);
        assert(objectRuntime.find(1210)->physicsEnabled);
        assert(objectRuntime.find(1210)->collisionEnabled);
        usm::game::LevelObjectRuntime isolatedSlideCarRuntime;
        assert(isolatedSlideCarRuntime.initialize(bootstrap));
        for (const auto& state : isolatedSlideCarRuntime.states()) {
            if (state.asset == nullptr) {
                continue;
            }
            const auto kind = state.asset->kind;
            if (kind == usm::game::LevelObjectKind::SpiderWebWall ||
                kind == usm::game::LevelObjectKind::SlideCar ||
                kind == usm::game::LevelObjectKind::BrokenBridge ||
                kind == usm::game::LevelObjectKind::Platform ||
                kind == usm::game::LevelObjectKind::ElectricPlatform) {
                assert(isolatedSlideCarRuntime.setRuntimeState(
                    state.asset->objectId, state.position, false,
                    state.physicsEnabled));
            }
        }
        assert(isolatedSlideCarRuntime.setRuntimeState(
            1210, slideCarAsset->position, true, true));
        usm::game::LevelCollision slideCarCollision;
        assert(slideCarCollision.build(bootstrap.rooms()));
        const std::size_t roomTriangleCount =
            slideCarCollision.triangleCount();
        assert(slideCarCollision.updateObjectColliders(
            isolatedSlideCarRuntime.states()));
        assert(slideCarCollision.triangleCount() ==
               roomTriangleCount + 12U);
        const auto* slideCarState = isolatedSlideCarRuntime.find(1210);
        const usm::assets::Vector3 slideCarLocalTop{
            (slideCarAsset->collisionLocalMinimum.x +
             slideCarAsset->collisionLocalMaximum.x) * 0.5F,
            (slideCarAsset->collisionLocalMinimum.y +
             slideCarAsset->collisionLocalMaximum.y) * 0.5F,
            slideCarAsset->collisionLocalMaximum.z};
        const auto& slideCarMatrix = slideCarState->worldTransform;
        const usm::assets::Vector3 slideCarWorldTop{
            slideCarLocalTop.x * slideCarMatrix[0] +
                slideCarLocalTop.y * slideCarMatrix[4] +
                slideCarLocalTop.z * slideCarMatrix[8] +
                slideCarMatrix[12],
            slideCarLocalTop.x * slideCarMatrix[1] +
                slideCarLocalTop.y * slideCarMatrix[5] +
                slideCarLocalTop.z * slideCarMatrix[9] +
                slideCarMatrix[13],
            slideCarLocalTop.x * slideCarMatrix[2] +
                slideCarLocalTop.y * slideCarMatrix[6] +
                slideCarLocalTop.z * slideCarMatrix[10] +
                slideCarMatrix[14]};
        float slideCarGroundHeight = 0.0F;
        usm::assets::Vector3 slideCarGroundProbe = slideCarWorldTop;
        slideCarGroundProbe.z += 20.0F;
        assert(slideCarCollision.groundHeight(
            slideCarGroundProbe, 25.0F, 1000.0F,
            slideCarGroundHeight));
        assert(std::abs(slideCarGroundHeight - slideCarWorldTop.z) <
               0.1F);
        const auto comicAsset = std::find_if(
            bootstrap.objects().begin(), bootstrap.objects().end(),
            [](const auto& object) { return object.objectId == 40010; });
        assert(comicAsset != bootstrap.objects().end());
        assert(comicAsset->comicIndex >= 0);
        assert(comicAsset->comicCollectionMinimum.x <
               comicAsset->position.x);
        assert(comicAsset->comicCollectionMaximum.x >
               comicAsset->position.x);
        objectRuntime.updateComicCollections(comicAsset->position);
        assert(objectRuntime.isComicCollected(40010));
        assert(!objectRuntime.find(40010)->visible);
        auto comicEvents = objectRuntime.consumeEvents();
        assert(comicEvents.size() == 1);
        assert(comicEvents.front().kind ==
               usm::game::LevelObjectEventKind::ComicCollected);
        assert(comicEvents.front().voxSoundId == 0x63);
        assert(comicEvents.front().collectibleIndex ==
               comicAsset->comicIndex);
        assert(objectRuntime.find(472) != nullptr);
        assert(objectRuntime.find(472)->activeAnimation == "water");
        assert(objectRuntime.find(40032) != nullptr);
        assert(objectRuntime.find(1257) == nullptr);
        const auto destroyableAsset = std::find_if(
            bootstrap.objects().begin(), bootstrap.objects().end(),
            [](const auto& object) { return object.objectId == 278; });
        assert(destroyableAsset != bootstrap.objects().end());
        assert(destroyableAsset->kind ==
               usm::game::LevelObjectKind::Destroyable);
        assert(destroyableAsset->health == 10.0F);
        assert(destroyableAsset->damageRadius == 600.0F);
        assert(destroyableAsset->damage == 10.0F);
        assert(destroyableAsset->attackable);
        assert(destroyableAsset->collisionAfterDestruction);
        assert(destroyableAsset->hitVoxSoundId == 0x6b);
        assert(destroyableAsset->collisionRadius > 1.0F);
        assert(destroyableAsset->collisionHeight > 1.0F);

        // CLevel::GetTargetedDestroyableList (0x0037df54),
        // Player::SearchTargetByAttackRange (0x003430c8), and
        // Player::SearchTargetByEyeHorizon (0x00343b70) place live,
        // attackable scenery in the same player target decision as enemies.
        usm::game::LevelObjectRuntime targetObjectRuntime;
        assert(targetObjectRuntime.initialize(bootstrap));
        for (const auto& object : bootstrap.objects()) {
            if (object.kind == usm::game::LevelObjectKind::Destroyable &&
                object.attackable) {
                assert(targetObjectRuntime.setRuntimeState(
                    object.objectId, object.position, false, true));
            }
        }
        const auto secondTargetObject = std::find_if(
            bootstrap.objects().begin(), bootstrap.objects().end(),
            [destroyableAsset](const auto& object) {
                return object.kind ==
                           usm::game::LevelObjectKind::Destroyable &&
                       object.attackable &&
                       object.objectId != destroyableAsset->objectId;
            });
        assert(secondTargetObject != bootstrap.objects().end());
        assert(targetObjectRuntime.setRuntimeState(
            destroyableAsset->objectId, {100.0F, 0.0F, 0.0F}, true, true));
        assert(targetObjectRuntime.setRuntimeState(
            secondTargetObject->objectId, {-200.0F, 0.0F, 0.0F}, true,
            true));
        const usm::assets::Vector3 targetSearchOrigin{};
        assert(targetObjectRuntime.findPlayerAttackRangeTarget(
                   targetSearchOrigin, 100.0F) == nullptr);
        const auto* strictRangeObject =
            targetObjectRuntime.findPlayerAttackRangeTarget(
                targetSearchOrigin, 100.1F);
        assert(strictRangeObject != nullptr &&
               strictRangeObject->asset->objectId ==
                   destroyableAsset->objectId);
        const auto* directionalObject =
            targetObjectRuntime.findPlayerEyeAttackTarget(
                targetSearchOrigin, {1.0F, 0.0F, 0.0F}, 1.0F);
        assert(directionalObject != nullptr &&
               directionalObject->asset->objectId ==
                   destroyableAsset->objectId);
        usm::assets::ColladaGeometry objectTargetOccluder;
        objectTargetOccluder.name = "double_destroyable_target_occluder";
        objectTargetOccluder.vertices = {
            {{1.0F, -1000.0F, -1000.0F}},
            {{1.0F, 1000.0F, -1000.0F}},
            {{1.0F, -1000.0F, 1000.0F}},
            {{1.0F, 1000.0F, 1000.0F}},
        };
        usm::assets::ColladaMeshBuffer objectTargetOccluderBuffer;
        objectTargetOccluderBuffer.indices = {0, 2, 3, 0, 3, 1};
        objectTargetOccluder.meshBuffers.push_back(
            objectTargetOccluderBuffer);
        const std::array objectTargetOccluderSet{objectTargetOccluder};
        usm::game::LevelCollision objectTargetOcclusionWorld;
        assert(objectTargetOcclusionWorld.build(objectTargetOccluderSet));
        assert(objectTargetOcclusionWorld.segmentBlocked(
            {100.0F, 0.0F, destroyableAsset->collisionHeight},
            {0.0F, 0.0F,
             usm::game::kPlayerCollisionHeightCentimeters},
            0xffff7f18U));
        assert(targetObjectRuntime.findPlayerEyeAttackTarget(
                   targetSearchOrigin, {1.0F, 0.0F, 0.0F}, 1.0F,
                   &objectTargetOcclusionWorld) == nullptr);
        assert(targetObjectRuntime.destroy(destroyableAsset->objectId));
        const auto* nextLiveObject =
            targetObjectRuntime.findPlayerAttackRangeTarget(
                targetSearchOrigin, 201.0F);
        assert(nextLiveObject != nullptr &&
               nextLiveObject->asset->objectId ==
                   secondTargetObject->objectId);

        const auto destroyedObject = objectRuntime.applyPlayerMeleeHit(
            destroyableAsset->position, {1.0F, 0.0F, 0.0F}, 10.0F,
            destroyableAsset->health, -1.0F);
        assert(destroyedObject == 278);
        assert(objectRuntime.isDestroyed(278));
        assert(objectRuntime.find(278)->health == 0.0F);
        assert(objectRuntime.find(278)->destructionPhase !=
               usm::game::LevelObjectDestructionPhase::Intact);
        auto objectEvents = objectRuntime.consumeEvents();
        assert(objectEvents.size() == 2);
        assert(objectEvents[0].kind ==
               usm::game::LevelObjectEventKind::Hit);
        assert(objectEvents[0].voxSoundId == 0x6b);
        assert(objectEvents[1].kind ==
               usm::game::LevelObjectEventKind::Destroyed);

        // Player::CheckAttackTarget (0x0034fca0) walks the complete
        // intersecting destroyable list. Preserve simultaneous contacts
        // instead of reducing the list to the nearest object.
        usm::game::LevelObjectRuntime multiObjectRuntime;
        assert(multiObjectRuntime.initialize(bootstrap));
        std::vector<std::int32_t> overlappingDestroyables;
        for (const auto& object : bootstrap.objects()) {
            if (object.kind == usm::game::LevelObjectKind::Destroyable &&
                object.attackable && object.health > 1.0F) {
                overlappingDestroyables.push_back(object.objectId);
                if (overlappingDestroyables.size() == 2) {
                    break;
                }
            }
        }
        assert(overlappingDestroyables.size() == 2);
        const usm::assets::Vector3 sharedDestroyablePosition{
            1000.0F, 2000.0F, 30.0F};
        for (const std::int32_t objectId : overlappingDestroyables) {
            assert(multiObjectRuntime.setRuntimeState(
                objectId, sharedDestroyablePosition, true, true));
        }
        const auto simultaneousObjectHits =
            multiObjectRuntime.applyPlayerMeleeHits(
                sharedDestroyablePosition, {1.0F, 0.0F, 0.0F},
                10.0F, 1.0F, -1.0F);
        for (const std::int32_t objectId : overlappingDestroyables) {
            assert(std::any_of(
                simultaneousObjectHits.begin(),
                simultaneousObjectHits.end(),
                [objectId](const auto& hit) {
                    return hit.objectId == objectId &&
                           hit.actualDamage == 1.0F;
                }));
        }
        objectRuntime.advanceAnimations(10000);
        assert(objectRuntime.find(278)->destructionPhase ==
               usm::game::LevelObjectDestructionPhase::Destroyed);
        assert(objectRuntime.find(278)->collisionEnabled);
        for (const auto& drop : bootstrap.dropObjects()) {
            const auto* state = objectRuntime.find(drop.objectId);
            assert(state != nullptr && !state->visible);
        }
        usm::game::CinematicThread objectThread;
        objectThread.objectId = 20032;
        usm::game::CinematicCommand moveLevelObject;
        moveLevelObject.name = "MoveObject";
        moveLevelObject.attributes.push_back(
            {"vector3d", "abspos", "100.0, 200.0, 300.0"});
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, objectThread, moveLevelObject));
        assert(objectRuntime.find(20032)->position.x == 100.0F);
        assert(objectRuntime.find(20032)->worldTransform[14] == 300.0F);
        usm::game::CinematicThread movingObjectThread;
        movingObjectThread.objectId = 20032;
        usm::game::CinematicCommand movingObjectStart;
        movingObjectStart.timestampMilliseconds = 0;
        movingObjectStart.name = "MoveObject";
        movingObjectStart.attributes.push_back(
            {"vector3d", "abspos", "100.0, 200.0, 300.0"});
        movingObjectStart.attributes.push_back(
            {"quaternion", "rot", "0.0, 0.0, 0.0, 1.0"});
        usm::game::CinematicCommand movingObjectEnd;
        movingObjectEnd.timestampMilliseconds = 1000;
        movingObjectEnd.name = "MoveObject";
        movingObjectEnd.attributes.push_back(
            {"vector3d", "abspos", "300.0, 600.0, 700.0"});
        movingObjectEnd.attributes.push_back(
            {"quaternion", "rot", "0.0, 0.0, 0.707107, 0.707107"});
        movingObjectThread.commands.push_back(movingObjectStart);
        movingObjectThread.commands.push_back(movingObjectEnd);
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, movingObjectThread,
            movingObjectThread.commands.front()));
        assert(objectRuntime.find(20032)->cinematicMotion.active);
        objectRuntime.advanceAnimations(250);
        assert(objectRuntime.find(20032)->position.x == 100.0F);
        objectRuntime.advanceAnimations(250);
        assert(std::abs(objectRuntime.find(20032)->position.x - 150.0F) <
               0.001F);
        assert(std::abs(objectRuntime.find(20032)->position.y - 300.0F) <
               0.001F);
        assert(std::abs(objectRuntime.find(20032)->position.z - 400.0F) <
               0.001F);
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, movingObjectThread, movingObjectThread.commands.back()));
        assert(objectRuntime.find(20032)->position.x == 300.0F);
        assert(!objectRuntime.find(20032)->cinematicMotion.active);
        usm::game::CinematicCommand missingObjectAnimation;
        missingObjectAnimation.name = "SetAnim";
        missingObjectAnimation.attributes.push_back(
            {"string", "$Anim", "missing_native_no_op"});
        const std::string objectAnimationBeforeMissing =
            objectRuntime.find(20032)->activeAnimation;
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, objectThread, missingObjectAnimation));
        assert(objectRuntime.find(20032)->activeAnimation ==
               objectAnimationBeforeMissing);
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, objectThread,
            usm::game::CinematicCommand{0, -1, "Physics", {}}));
        assert(objectRuntime.find(20032)->physicsEnabled);
        usm::game::CinematicCommand hideStream;
        hideStream.name = "ShowStream";
        hideStream.attributes.push_back(
            {"int", "ID^StreamPiping", "1202"});
        hideStream.attributes.push_back({"bool", "Visible", "false"});
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, {}, hideStream));
        assert(!objectRuntime.find(1202)->visible);
        usm::game::CinematicThread webWallThread;
        webWallThread.objectId = 1151;
        usm::game::CinematicCommand openWebWall;
        openWebWall.name = "SetAnim";
        openWebWall.attributes.push_back({"string", "$Anim", "open"});
        openWebWall.attributes.push_back({"bool", "loop", "false"});
        assert(objectRuntime.applyCinematicCommand(
            bootstrap, webWallThread, openWebWall));
        assert(objectRuntime.find(1151)->activeAnimation == "open");
        assert(!objectRuntime.find(1151)->animationLoops);
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
        assert(testSlideRuntime.enabled(99).has_value());
        assert(*testSlideRuntime.enabled(99));
        usm::game::CinematicCommand setSlideEnabled;
        setSlideEnabled.name = "Enable_Slide";
        setSlideEnabled.attributes = {{"int", "^ID^Slide", "99"},
                                      {"bool", "Enable", "false"}};
        assert(testSlideRuntime.applyCinematicCommand(setSlideEnabled));
        assert(!*testSlideRuntime.enabled(99));
        assert(testSlideRuntime.findCatch({25.0F, 10.0F, 0.0F}).slide ==
               nullptr);
        setSlideEnabled.attributes.back().value = "true";
        assert(testSlideRuntime.applyCinematicCommand(setSlideEnabled));
        assert(*testSlideRuntime.enabled(99));
        setSlideEnabled.attributes.front().value = "999";
        assert(!testSlideRuntime.applyCinematicCommand(setSlideEnabled));
        assert(!testSlideRuntime.enabled(999).has_value());
        setSlideEnabled.attributes.erase(setSlideEnabled.attributes.begin());
        assert(!testSlideRuntime.applyCinematicCommand(setSlideEnabled));
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
        const auto suppressedSlideExit = testSlideRuntime.jumpFinish(2433);
        assert(std::abs(
                   suppressedSlideExit.velocityCentimetersPerSecond.x -
                   100.0F) < 0.001F);
        assert(testSlideRuntime.findCatch({25.0F, 10.0F, 0.0F}).slide ==
               nullptr);
        testSlideRuntime.advanceCooldown(2432);
        assert(testSlideRuntime.findCatch({25.0F, 10.0F, 0.0F}).slide ==
               nullptr);
        testSlideRuntime.advanceCooldown(1);
        assert(testSlideRuntime.findCatch({25.0F, 10.0F, 0.0F}).slide ==
               &testSlides[0]);
        assert(testSlideRuntime.start(caughtSlide, 100.0F));
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
        // CSlider::Update keeps ordinary jump/fall states from re-catching a
        // terminal segment in its final 200 cm, while native slider-jump-fall
        // and web-release states may still catch there.
        assert(testSlideRuntime.findCatch({225.0F, 0.0F, 0.0F}, true, false)
                   .slide == nullptr);
        assert(testSlideRuntime.findCatch({225.0F, 0.0F, 0.0F}, true, true)
                   .slide == &testSlides[0]);
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
        assert(normalAttack->hitType == 101);
        assert(normalAttack->damage == 35.0F);
        assert(normalAttack->hitProtectionMilliseconds == 0.0F);
        assert(normalAttack->horizontalForce == 0.0F);
        assert(normalAttack->verticalForce == 0.0F);
        assert(normalAttack->maximumReach() == 200.0F);
        assert(normalAttack->minimumAngleDegrees == -90.0F);
        assert(normalAttack->maximumAngleDegrees == 90.0F);
        // CBehaviorMeleeAttack::CanBeInterrupt (0x003b9644) reads the
        // second packed attack boolean at EnemyAttackInfo+0x3d while the
        // authored strike is executing. Both opening-thug attacks permit
        // interruption; treating their wind-up as an implicit block makes
        // the first encounter feel artificially unresponsive.
        assert(normalAttack->interruptibleDuringExecution);
        assert(normalAttack->senseSlowMotionDenominator == 3.0F);
        assert(normalAttack->senseReactionType == 1);
        assert(normalAttack->forceSenseActionId == -1);
        assert(normalAttack->sensePhotoTargetId == -1);
        const auto* knifeAttack = bootstrap.attackConfigs().find(6);
        assert(knifeAttack != nullptr);
        assert(knifeAttack->name == "ATTACK_HIT_LIGHT");
        assert(knifeAttack->hitType == 100);
        assert(knifeAttack->damage == 25.0F);
        assert(knifeAttack->hitProtectionMilliseconds == 0.0F);
        assert(knifeAttack->horizontalForce == 0.0F);
        assert(knifeAttack->verticalForce == 0.0F);
        assert(knifeAttack->maximumReach() == 200.0F);
        assert(knifeAttack->interruptibleDuringExecution);
        assert(knifeAttack->senseSlowMotionDenominator == 3.0F);
        assert(knifeAttack->senseReactionType == 1);
        assert(knifeAttack->forceSenseActionId == -1);
        assert(knifeAttack->sensePhotoTargetId == -1);
        const auto* batJumpAttack = bootstrap.attackConfigs().find(11);
        assert(batJumpAttack != nullptr);
        assert(batJumpAttack->name == "ATTACK_HIT_NORMAL_bat_jump");
        assert(batJumpAttack->hitType == 101);
        assert(batJumpAttack->damage == 50.0F);
        assert(batJumpAttack->hitProtectionMilliseconds == 0.0F);
        assert(batJumpAttack->horizontalForce == 0.0F);
        assert(batJumpAttack->verticalForce == 0.0F);
        assert(batJumpAttack->maximumReach() == 300.0F);
        assert(batJumpAttack->interruptibleDuringExecution);
        assert(batJumpAttack->senseSlowMotionDenominator == 3.0F);
        assert(batJumpAttack->senseReactionType == 1);
        assert(batJumpAttack->forceSenseActionId == -1);
        assert(batJumpAttack->sensePhotoTargetId == -1);
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
        const auto* knifeThugAttributes = enemyAttributes.find(0);
        const auto* batThugAttributes = enemyAttributes.find(1);
        const auto* molotovAttributes = enemyAttributes.find(2);
        const auto* gunThugAttributes = enemyAttributes.find(3);
        const auto* bigThugAttributes = enemyAttributes.find(4);
        const auto* sandmanAttributes = enemyAttributes.find(16);
        const auto* blueAircraftAttributes = enemyAttributes.find(20);
        assert(knifeThugAttributes != nullptr &&
               knifeThugAttributes->allowsHorizontalHitForce &&
               knifeThugAttributes->allowsVerticalHitForce &&
               knifeThugAttributes->allowsLaunchHitType &&
               knifeThugAttributes->canBeCounterHit);
        // The native behavior map at 0x004c0848 assigns slot 8 to
        // CBehaviorBlock (interface 0x12d). Neither opening archetype lists
        // that slot, so their first-room damage must never be discarded as
        // a reconstructed block response.
        assert(std::find(knifeThugAttributes->behaviorTypeMapIndices.begin(),
                         knifeThugAttributes->behaviorTypeMapIndices.end(),
                         8) ==
               knifeThugAttributes->behaviorTypeMapIndices.end());
        assert(batThugAttributes != nullptr);
        assert(std::find(batThugAttributes->behaviorTypeMapIndices.begin(),
                         batThugAttributes->behaviorTypeMapIndices.end(), 8) ==
               batThugAttributes->behaviorTypeMapIndices.end());
        assert(molotovAttributes != nullptr &&
               molotovAttributes->exportedId == 2 &&
               molotovAttributes->name == "THUG_MOLOTOV" &&
               molotovAttributes->collisionRadius == 40.0F &&
               molotovAttributes->collisionHeight == 150.0F &&
               molotovAttributes->minimumRangeAttackDistance == 500.0F &&
               molotovAttributes->maximumRangeAttackDistance == 1200.0F &&
               molotovAttributes->rangedAttackTypeMapIndices ==
                   std::vector<std::int32_t>{10} &&
               molotovAttributes->canBeTiedUp() &&
               molotovAttributes->canBeDraggedTo());
        assert(gunThugAttributes != nullptr &&
               gunThugAttributes->exportedId == 3 &&
               gunThugAttributes->name == "THUG_GUN" &&
               gunThugAttributes->collisionRadius == 40.0F &&
               gunThugAttributes->collisionHeight == 150.0F &&
               gunThugAttributes->rangedAttackTypeMapIndices ==
                   std::vector<std::int32_t>{4});
        assert(bigThugAttributes != nullptr &&
               bigThugAttributes->name == "THUG_BIG" &&
               bigThugAttributes->collisionRadius == 60.0F &&
               bigThugAttributes->collisionHeight == 180.0F &&
               bigThugAttributes->rangedAttackTypeMapIndices ==
                   std::vector<std::int32_t>{6} &&
               !bigThugAttributes->canBeTiedUp() &&
               bigThugAttributes->canBeDraggedTo());
        assert(sandmanAttributes != nullptr &&
               sandmanAttributes->collisionRadius == 100.0F &&
               sandmanAttributes->collisionHeight == 200.0F);
        assert(blueAircraftAttributes != nullptr &&
               !blueAircraftAttributes->canBeTiedUp() &&
               !blueAircraftAttributes->canBeDraggedTo());
        assert(usm::game::resolveEnemyRangeWeaponType(10) == 5);
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
        const auto* meleeAttackState = behaviorConfigs.findState(
            "ENEMY_BEHAVIOR_MELEE_ATTACK_STATE_DO_ATTACK");
        assert(meleeAttackState != nullptr && meleeAttackState->id == 11);
        assert(meleeAttackState->animationSelectionMode == 3);
        assert(behaviorConfigs.resolveStateAnimationNames(
                   meleeAttackState->name, 1) ==
               (std::vector<std::string_view>{"idle_at1_idle",
                                               "idle_jump_at3_idle"}));
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
        assert(enemySounds.decodedSoundCount() >= 32);
        std::size_t landingImpactSoundCount = 0;
        assert(enemySounds.dispatch(
            0x4b,
            [&landingImpactSoundCount](const usm::audio::PcmAudio& clip,
                                       bool loop) {
                assert(clip.frameCount() > 0);
                assert(!loop);
                ++landingImpactSoundCount;
                return usm::Result::success();
            }));
        assert(landingImpactSoundCount == 1);
        std::size_t coveredHurtAndDeathSounds = 0;
        for (const auto enemyTypeId : firstLevelEnemyTypes) {
            for (const auto& state : behaviorConfigs.states()) {
                if (!state.name.starts_with("ENEMY_BEHAVIOR_HURT_STATE_") &&
                    state.name != "ENEMY_BEHAVIOR_DEAD_STATE" &&
                    state.name != "ENEMY_BEHAVIOR_DEAD_STATE_ON_WALL") {
                    continue;
                }
                for (const auto voxId : behaviorConfigs.resolveStateSoundIds(
                         state.name, enemyTypeId)) {
                    assert(enemySounds.dispatch(
                        voxId,
                        [&coveredHurtAndDeathSounds](
                            const usm::audio::PcmAudio& clip, bool loop) {
                            assert(clip.frameCount() > 0);
                            assert(!loop);
                            ++coveredHurtAndDeathSounds;
                            return usm::Result::success();
                        }));
                }
            }
        }
        assert(coveredHurtAndDeathSounds > 0);
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
        assert(firstEncounterTrigger->roomId == 1);
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
        std::array<bool, 16> activeRooms{};
        usm::game::LevelTriggerRuntime roomGatedTriggerRuntime;
        roomGatedTriggerRuntime.bind(
            std::span<const usm::game::LevelTriggerAsset>(
                &*firstEncounterTrigger, 1));
        assert(roomGatedTriggerRuntime
                   .update(bootstrap.player().position, activeRooms)
                   .empty());
        activeRooms[0] = true;
        assert(roomGatedTriggerRuntime
                   .update(bootstrap.player().position, activeRooms)
                   .size() == 1);
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
        std::set<std::int32_t> reachableCinematics{1264, 1267};
        std::vector<std::int32_t> cinematicQueue{1264, 1267};
        const auto enqueueCinematic = [&reachableCinematics,
                                       &cinematicQueue](std::int32_t id) {
            if (id >= 0 && reachableCinematics.insert(id).second) {
                cinematicQueue.push_back(id);
            }
        };
        const auto enqueueTriggerCinematics = [&enqueueCinematic](
                                                   const auto& trigger) {
            enqueueCinematic(trigger.outToInCinematicId);
            enqueueCinematic(trigger.inToOutCinematicId);
            enqueueCinematic(trigger.whileInsideCinematicId);
            enqueueCinematic(trigger.whileOutsideCinematicId);
        };
        for (const usm::game::LevelTriggerAsset& trigger :
             bootstrap.triggers()) {
            if (trigger.enabled) {
                enqueueTriggerCinematics(trigger);
            }
        }
        for (std::size_t queueIndex = 0;
             queueIndex < cinematicQueue.size(); ++queueIndex) {
            const auto cinematic = std::find_if(
                bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
                [&cinematicQueue, queueIndex](const auto& candidate) {
                    return candidate.objectId == cinematicQueue[queueIndex];
                });
            if (cinematic == bootstrap.cinematics().end() ||
                !cinematic->scriptAvailable) {
                std::cerr << "Reachable cinematic is unavailable: "
                          << cinematicQueue[queueIndex] << '\n';
                return 1;
            }
            for (const usm::game::CinematicThread& thread :
                 cinematic->script.threads()) {
                for (const usm::game::CinematicCommand& command :
                     thread.commands) {
                    if (command.name == "EnableTrigger") {
                        const usm::game::CinematicAttribute* triggerId =
                            command.findAttribute("^ID^Trigger");
                        assert(triggerId != nullptr);
                        std::int32_t parsedTriggerId = -1;
                        const auto parsed = std::from_chars(
                            triggerId->value.data(),
                            triggerId->value.data() + triggerId->value.size(),
                            parsedTriggerId);
                        assert(parsed.ec == std::errc{} &&
                               parsed.ptr == triggerId->value.data() +
                                                 triggerId->value.size());
                        const auto enabledTrigger = std::find_if(
                            bootstrap.triggers().begin(),
                            bootstrap.triggers().end(),
                            [parsedTriggerId](const auto& trigger) {
                                return trigger.objectId == parsedTriggerId;
                            });
                        assert(enabledTrigger != bootstrap.triggers().end());
                        enqueueTriggerCinematics(*enabledTrigger);
                    }
                    const char* attributeName =
                        command.name == "StartCinematic"
                            ? "CinematicID"
                        : command.name == "PlayDAECamera"
                            ? "^ID^Cinematic^Next"
                            : nullptr;
                    if (attributeName == nullptr) {
                        continue;
                    }
                    const usm::game::CinematicAttribute* next =
                        command.findAttribute(attributeName);
                    assert(next != nullptr);
                    std::int32_t nextId = -1;
                    const auto parsed = std::from_chars(
                        next->value.data(),
                        next->value.data() + next->value.size(), nextId);
                    assert(parsed.ec == std::errc{} &&
                           parsed.ptr ==
                               next->value.data() + next->value.size());
                    enqueueCinematic(nextId);
                }
            }
        }
        const std::set<std::int32_t> expectedReachableCinematics{
            71,    535,   969,   972,   974,   1115,  1140, 1148, 1150,
            1155,  1162,  1170,  1175,  1197,  1198, 1212, 1215,
            1238,  1253,  1254,  1256,  1264,  1265,  1266, 1267, 10336,
            10338, 20004, 20026, 20038, 30002, 40005, 40027, 40040};
        assert(reachableCinematics == expectedReachableCinematics);
        for (const std::int32_t unreachableEditorCinematic :
             {432, 1270, 1274, 20006, 20010, 20013}) {
            assert(!reachableCinematics.contains(unreachableEditorCinematic));
        }
        // These shipped tutorial/Room 9 branches are also inactive: their
        // source triggers 1171 and 30005 are disabled in the level data and no
        // reachable cinematic enables either one.
        assert(!reachableCinematics.contains(1172));
        assert(!reachableCinematics.contains(30003));
        std::set<std::int32_t> loadedSceneObjectIds;
        for (const usm::assets::IrrSceneNode& node :
             bootstrap.mainScene().nodes()) {
            loadedSceneObjectIds.insert(node.id);
        }
        for (const usm::game::LevelRoomAsset& room : bootstrap.rooms()) {
            for (const usm::assets::IrrSceneNode& node : room.scene.nodes()) {
                loadedSceneObjectIds.insert(node.id);
            }
        }
        for (const std::int32_t cinematicId : reachableCinematics) {
            const auto cinematic = std::find_if(
                bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
                [cinematicId](const auto& candidate) {
                    return candidate.objectId == cinematicId;
                });
            assert(cinematic != bootstrap.cinematics().end());
            for (const usm::game::CinematicThread& thread :
                 cinematic->script.threads()) {
                assert(thread.objectId < 0 ||
                       loadedSceneObjectIds.contains(thread.objectId));
            }
        }
        const auto firstEncounterCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1162;
            });
        assert(firstEncounterCinematic != bootstrap.cinematics().end());
        assert(firstEncounterCinematic->scriptFile ==
               "cinematics/levelnew_01_1162_cinematic.cff");
        assert(firstEncounterCinematic->script.commandCount() > 0);
        usm::game::GameplayCinematicScheduler cinematicScheduler;
        cinematicScheduler.bind(bootstrap.cinematics());
        assert(cinematicScheduler.start(1162));
        assert(cinematicScheduler.start(20026));
        assert(cinematicScheduler.start(20026));
        const auto concurrentCinematics = cinematicScheduler.activeIds();
        assert(concurrentCinematics.size() == 2);
        assert(std::find(concurrentCinematics.begin(),
                         concurrentCinematics.end(), 1162) !=
               concurrentCinematics.end());
        assert(std::find(concurrentCinematics.begin(),
                         concurrentCinematics.end(), 20026) !=
               concurrentCinematics.end());
        assert(cinematicScheduler.update(
            50, [](const usm::game::LevelCinematicAsset& cinematic,
                   const usm::game::CinematicThread&,
                   const usm::game::CinematicCommand& command) {
                assert(cinematic.objectId == 1162 ||
                       cinematic.objectId == 20026);
                return command.name != "IfEnemyDead";
            }));
        assert(cinematicScheduler.active(20026));
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
        const auto& actionConfigs = bootstrap.quickTimeActionConfigs();
        assert(actionConfigs.definitions().size() == 28);
        const auto* throwBegin = actionConfigs.find(9);
        const auto* throwMain = actionConfigs.find(6);
        const auto* throwSuccess = actionConfigs.find(7);
        const auto* throwFailure = actionConfigs.find(8);
        assert(throwBegin != nullptr &&
               throwBegin->name == "k_qte_action_throw_begin" &&
               throwBegin->playerAnimation == "idle_to_grab" &&
               throwBegin->npcAnimation == "grab_to_hold" &&
               throwBegin->successStateId == 6 &&
               throwBegin->failureStateId == -1);
        assert(throwMain != nullptr &&
               throwMain->name == "k_qte_action_throw_main" &&
               throwMain->buttonConfigId == 11 && throwMain->loop &&
               throwMain->attackDirection == 0 &&
               throwMain->playerAnimation == "struggleing" &&
               throwMain->npcAnimation == "struggleing" &&
               throwMain->attackFrames ==
                   (std::vector<std::int16_t>{20, 40, 60, 80}) &&
               throwMain->attackDamage ==
                   (std::vector<std::int16_t>{20, 20, 20, 20}) &&
               throwMain->playerEndAction == -1 &&
               throwMain->npcEndAction == -1 &&
               throwMain->successStateId == 7 &&
               throwMain->failureStateId == 8);
        const auto* throwButton = bootstrap.buttonConfigs().find(11);
        assert(throwButton != nullptr &&
               throwButton->name == "k_igm_button_qte_tap_8" &&
               throwButton->durationMilliseconds == 4000.0F &&
               throwButton->requiredActionCount == 8);
        assert(throwSuccess != nullptr &&
               throwSuccess->attackDirection == 2 &&
               throwSuccess->playerAnimation ==
                   "struggle_to_hit_to_idle" &&
               throwSuccess->npcAnimation ==
                   "struggle_to_hurt_to_idle" &&
               throwSuccess->attackFrames ==
                   std::vector<std::int16_t>{21} &&
               throwSuccess->attackDamage ==
                   std::vector<std::int16_t>{100});
        assert(throwSuccess->playerEndAction == 0 &&
               throwSuccess->npcEndAction == 0 &&
               throwFailure->playerEndAction == 1 &&
               throwFailure->npcEndAction == 0);
        assert(throwFailure != nullptr &&
               throwFailure->attackDirection == 3 &&
               throwFailure->playerAnimation ==
                   "grab_to_knockbackflying" &&
               throwFailure->npcAnimation == "hold_to_throw" &&
               throwFailure->attackFrames ==
                   std::vector<std::int16_t>{50} &&
               throwFailure->attackDamage ==
                   std::vector<std::int16_t>{100});
        usm::game::CinematicCommand startLevelOneQte;
        startLevelOneQte.name = "StartQTE";
        startLevelOneQte.attributes = {
            {"int", "QTEID", "6"},
            {"int", "^ID^Cinematic^Success", "20006"},
            {"int", "^ID^Cinematic^Fail", "20010"},
        };
        usm::game::QuickTimeEventRuntime successfulQte;
        successfulQte.bind(bootstrap.buttonConfigs());
        assert(successfulQte.applyCommand(startLevelOneQte, 20005));
        assert(successfulQte.sourceCinematicId() == 20005);
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
        const auto* wallWebButton = bootstrap.buttonConfigs().find(12);
        assert(wallWebButton != nullptr);
        assert(wallWebButton->interactionType == 3 &&
               wallWebButton->requiredActionCount == 8 &&
               wallWebButton->durationMilliseconds == 3000.0F);
        auto startMashQte = startLevelOneQte;
        startMashQte.attributes.front().value = "12";
        usm::game::QuickTimeEventRuntime mashQte;
        mashQte.bind(bootstrap.buttonConfigs());
        assert(mashQte.applyCommand(startMashQte));
        mashQte.update(0, true);
        assert(mashQte.active() && mashQte.completedActionCount() == 1);
        mashQte.update(499, false);
        assert(mashQte.completedActionCount() == 1);
        mashQte.update(1, false);
        assert(mashQte.completedActionCount() == 0);
        for (int tap = 0; tap < 7; ++tap) {
            mashQte.update(50, true);
            assert(mashQte.active());
        }
        mashQte.update(50, true);
        assert(!mashQte.active() && mashQte.completedActionCount() == 8);
        assert(mashQte.consumeCinematicRequest() == 20006);
        assert(mashQte.applyCommand(startMashQte));
        assert(mashQte.completedActionCount() == 0);
        mashQte.update(3000, false);
        assert(!mashQte.active());
        assert(mashQte.consumeCinematicRequest() == 20010);
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
        assert(beforeBossCinematic->actors.back().animationClipId == 0);
        assert(beforeBossCinematic->actors.back()
                   .animationClipStartMilliseconds == 15699);
        assert(beforeBossCinematic->actors.back()
                   .animationClipDurationMilliseconds() == 20634);
        assert(beforeBossCinematic->colladaDurationMilliseconds == 36334);
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
        assert(levelEndCinematic->actors.back()
                   .animationClipStartMilliseconds == 28649);
        assert(levelEndCinematic->actors.back()
                   .animationClipDurationMilliseconds() == 8900);
        assert(levelEndCinematic->colladaDurationMilliseconds == 40599);
        std::size_t forcedRoomCommandCount = 0;
        for (const auto& cinematic : bootstrap.cinematics()) {
            for (const auto& thread : cinematic.script.threads()) {
                for (const auto& command : thread.commands) {
                    if (command.name != "MustBeVisibleRoom") {
                        continue;
                    }
                    ++forcedRoomCommandCount;
                    const auto* set = command.findAttribute("Set");
                    const auto* rooms =
                        command.findAttribute("MustBeVisible");
                    assert(set != nullptr);
                    if (cinematic.objectId == 1265) {
                        assert(set->value == "true");
                        assert(rooms != nullptr);
                        assert(rooms->value == "1,2,3,4,5");
                    } else {
                        assert(cinematic.objectId == 1266);
                        assert(set->value == "false");
                    }
                }
            }
        }
        assert(forcedRoomCommandCount == 2);
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
        assert(gameOverCinematic->hasColladaPlayback());
        assert(std::any_of(
            gameOverCinematic->script.threads().begin(),
            gameOverCinematic->script.threads().end(),
            [&bootstrap](const auto& thread) {
                return thread.objectId == bootstrap.player().objectId &&
                       std::any_of(thread.commands.begin(),
                                   thread.commands.end(),
                                   [](const auto& command) {
                                       return command.name == "PlayDAEAnim";
                                   });
            }));
        assert(std::any_of(
            gameOverCinematic->script.threads().begin(),
            gameOverCinematic->script.threads().end(),
            [](const auto& thread) {
                return std::any_of(
                    thread.commands.begin(), thread.commands.end(),
                    [](const auto& command) {
                        return command.name == "Transport" &&
                               command.timestampMilliseconds == 55000;
                    });
            }));
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
        usm::game::LevelEnemyRuntime playDaeEnemyRuntime;
        assert(playDaeEnemyRuntime.initialize(bootstrap));
        assert(!playDaeEnemyRuntime.find(398)->visible);
        usm::game::CinematicThread playDaeEnemyThread;
        playDaeEnemyThread.objectId = 398;
        usm::game::CinematicCommand playDaeEnemyCommand;
        playDaeEnemyCommand.name = "PlayDAEAnim";
        playDaeEnemyCommand.attributes = {
            {"string", "AnimFile", ".\\meshes_bin\\thug_lv1.bdae"},
            {"int", "clipID", "0"},
        };
        assert(playDaeEnemyRuntime.applyCinematicCommand(
            bootstrap, playDaeEnemyThread, playDaeEnemyCommand));
        assert(playDaeEnemyRuntime.find(398)->visible);
        assert(!playDaeEnemyRuntime.find(398)->physicsActive);
        playDaeEnemyRuntime.endColladaAnimation(398);
        assert(playDaeEnemyRuntime.find(398)->visible);
        assert(playDaeEnemyRuntime.find(398)->physicsActive);
        usm::game::CinematicThread showHealthThread;
        showHealthThread.type = 1;
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
        usm::game::CinematicThread objectHealthThread;
        objectHealthThread.type = 0;
        objectHealthThread.objectId = 1199;
        showHammerHealth.attributes.front().value = "-1";
        assert(enemyRuntime.applyCinematicCommand(
            bootstrap, objectHealthThread, showHammerHealth));
        assert(enemyRuntime.shownHealthBarEnemy()->asset->objectId == 1199);
        constexpr std::array<std::int32_t, 16> conditionEnemyIds{
            394, 395, 397, 398, 399, 401, 488, 489,
            505, 506, 1139, 1199, 1251, 10339, 10340, 30000,
        };
        for (const std::int32_t enemyId : conditionEnemyIds) {
            assert(enemyRuntime.find(enemyId) != nullptr);
        }
        usm::game::LevelEnemyRuntime reverseAnimationRuntime;
        assert(reverseAnimationRuntime.initialize(bootstrap));
        usm::game::CinematicThread reverseAnimationThread;
        reverseAnimationThread.objectId = 394;
        usm::game::CinematicCommand reverseAnimationCommand;
        reverseAnimationCommand.name = "SetAnim";
        reverseAnimationCommand.attributes = {
            {"string", "$Anim", "idle_knife_at_idle"},
            {"bool", "loop", "false"},
            {"bool", "reverse", "true"},
            {"float", "speed", "0.5"},
        };
        assert(reverseAnimationRuntime.applyCinematicCommand(
            bootstrap, reverseAnimationThread, reverseAnimationCommand));
        const auto* reversedEnemy = reverseAnimationRuntime.find(394);
        assert(reversedEnemy != nullptr);
        const auto& reversedArchetype =
            bootstrap.enemyArchetypes()[reversedEnemy->asset->archetypeIndex];
        const auto* reversedClip =
            reversedArchetype.animationBank.findClip("idle_knife_at_idle");
        assert(reversedClip != nullptr);
        assert(!reversedEnemy->animationLoops);
        assert(reversedEnemy->animationReversed);
        assert(reversedEnemy->animationSpeed == 0.5F);
        assert(reversedEnemy->animationTimeMilliseconds ==
               reversedClip->durationMilliseconds());
        usm::game::CinematicCommand missingEnemyAnimation;
        missingEnemyAnimation.name = "SetAnim";
        missingEnemyAnimation.attributes.push_back(
            {"string", "$Anim", "missing_native_no_op"});
        assert(reverseAnimationRuntime.applyCinematicCommand(
            bootstrap, reverseAnimationThread, missingEnemyAnimation));
        assert(reverseAnimationRuntime.find(394)->activeAnimation ==
               "idle_knife_at_idle");
        reverseAnimationRuntime.advanceAnimations(200);
        assert(reverseAnimationRuntime.find(394)->animationTimeMilliseconds ==
               reversedClip->durationMilliseconds() - 100);
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
        assert(!scriptedKnifeEnemy->physicsActive);
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
        usm::game::LevelCollision encounterCollision;
        assert(encounterCollision.build(bootstrap.rooms()));
        const float stagedBatEnemyHeight = enemyRuntime.find(395)->position.z;
        enemyRuntime.updateGameplay(500, {}, &encounterCollision);
        assert(enemyRuntime.find(395)->position.z == stagedBatEnemyHeight);
        assert(!enemyRuntime.find(395)->physicsActive);
        const auto beginEncounterCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 71;
            });
        assert(beginEncounterCinematic != bootstrap.cinematics().end());
        usm::game::LevelEnemyRuntime beginMotionRuntime;
        assert(beginMotionRuntime.initialize(bootstrap));
        usm::game::CinematicPlayer beginMotionPlayer;
        assert(beginMotionPlayer.start(beginEncounterCinematic->script));
        usm::Result beginMotionResult = usm::Result::success();
        assert(beginMotionPlayer.advanceTo(
            0, [&bootstrap, &beginMotionRuntime, &beginMotionResult](
                   const usm::game::CinematicThread& thread,
                   const usm::game::CinematicCommand& command) {
                if (beginMotionResult) {
                    beginMotionResult =
                        beginMotionRuntime.applyCinematicCommand(
                            bootstrap, thread, command);
                }
            }));
        assert(beginMotionResult);
        const auto* movingKnife = beginMotionRuntime.find(394);
        assert(movingKnife != nullptr && movingKnife->cinematicMotion.active);
        assert(movingKnife->cinematicMotion.durationMilliseconds == 1450);
        const float motionStartX = movingKnife->position.x;
        const float motionEndX = movingKnife->cinematicMotion.endPosition.x;
        beginMotionRuntime.updateGameplay(725, {}, nullptr);
        assert(beginMotionRuntime.find(394)->position.x == motionStartX);
        beginMotionRuntime.updateGameplay(1, {}, nullptr);
        assert(std::abs(beginMotionRuntime.find(394)->position.x -
                        (motionStartX + motionEndX) * 0.5F) < 0.01F);
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
        assert(enemyRuntime.find(394)->physicsActive);
        assert(enemyRuntime.find(395)->aiEnabled);
        assert(enemyRuntime.find(395)->physicsActive);
        assert(enemyRuntime.find(397)->aiEnabled);
        assert(enemyRuntime.find(397)->physicsActive);
        const auto* exactAttackTarget = enemyRuntime.findPlayerAttackTarget(
            enemyRuntime.find(394)->position, {1.0F, 0.0F, 0.0F}, false,
            1.0F);
        assert(exactAttackTarget != nullptr);
        assert(exactAttackTarget->asset->objectId == 394);
        assert(enemyRuntime.findPlayerAttackTarget(
                   enemyRuntime.find(394)->position,
                   {1.0F, 0.0F, 0.0F}, true, 1.0F) == nullptr);
        const auto targetOcclusionAnchor = enemyRuntime.find(394)->position;
        const usm::assets::Vector3 targetOcclusionPlayer{
            targetOcclusionAnchor.x - 600.0F,
            targetOcclusionAnchor.y,
            targetOcclusionAnchor.z};
        const auto* unobstructedAttackTarget =
            enemyRuntime.findPlayerAttackTarget(
                targetOcclusionPlayer, {1.0F, 0.0F, 0.0F}, true,
                1000.0F);
        assert(unobstructedAttackTarget != nullptr);
        usm::assets::ColladaGeometry targetOccluder;
        targetOccluder.name = "double_target_occluder";
        // Place the wall immediately in front of the fabricated player so
        // every forward-cone candidate crosses it, not merely actor 394.
        const float targetOccluderX = targetOcclusionPlayer.x + 1.0F;
        targetOccluder.vertices = {
            {{targetOccluderX, targetOcclusionAnchor.y - 5000.0F,
              targetOcclusionAnchor.z}},
            {{targetOccluderX, targetOcclusionAnchor.y + 5000.0F,
              targetOcclusionAnchor.z}},
            {{targetOccluderX, targetOcclusionAnchor.y - 5000.0F,
              targetOcclusionAnchor.z + 1000.0F}},
            {{targetOccluderX, targetOcclusionAnchor.y + 5000.0F,
              targetOcclusionAnchor.z + 1000.0F}},
        };
        usm::assets::ColladaMeshBuffer targetOccluderBuffer;
        targetOccluderBuffer.indices = {0, 2, 3, 0, 3, 1};
        targetOccluder.meshBuffers.push_back(targetOccluderBuffer);
        const std::array targetOccluderSet{targetOccluder};
        usm::game::LevelCollision targetOcclusionWorld;
        assert(targetOcclusionWorld.build(targetOccluderSet));
        // SearchTargetByEyeHorizon (0x00343b70) rejects candidates when its
        // Unit::IsBlockedByWorld virtual (0x00324670) hits authored world
        // geometry. Directional input has no nearest-range fallback.
        assert(enemyRuntime.findPlayerAttackTarget(
                   targetOcclusionPlayer, {1.0F, 0.0F, 0.0F}, true,
                   1000.0F, &targetOcclusionWorld) == nullptr);
        assert(bootstrap.enemyArchetypes().size() == 6);
        assert(bootstrap.enemies().size() == 34);
        const auto firstKnifeEnemy = std::find_if(
            bootstrap.enemies().begin(), bootstrap.enemies().end(),
            [](const usm::game::LevelEnemyAsset& enemy) {
                return enemy.objectId == 394;
            });
        assert(firstKnifeEnemy != bootstrap.enemies().end());
        assert(firstKnifeEnemy->health == 500.0F);
        struct OpeningEnemyAppearance {
            std::int32_t objectId;
            std::string_view gameType;
            float textureTranslationU;
        };
        for (const OpeningEnemyAppearance& expected : {
                 OpeningEnemyAppearance{394, "MeleeThugEnemy_knife", -0.498F},
                 OpeningEnemyAppearance{395, "MeleeThugEnemy_bat", 0.0F},
                 OpeningEnemyAppearance{397, "MeleeThugEnemy_knife", -0.498F},
             }) {
            const auto openingEnemy = std::find_if(
                bootstrap.enemies().begin(), bootstrap.enemies().end(),
                [&expected](const usm::game::LevelEnemyAsset& enemy) {
                    return enemy.objectId == expected.objectId;
                });
            assert(openingEnemy != bootstrap.enemies().end());
            assert(openingEnemy->health == 500.0F);
            assert(openingEnemy->gameType == expected.gameType);
            const auto& archetype =
                bootstrap.enemyArchetypes()[openingEnemy->archetypeIndex];
            const bool expectedMaterial = std::any_of(
                archetype.mesh.materials().begin(),
                archetype.mesh.materials().end(),
                [&expected](const usm::assets::ColladaMaterial& material) {
                    return std::abs(
                               material.diffuseTextureTransform[4] -
                               expected.textureTranslationU) < 0.00001F &&
                           std::abs(material.diffuseTextureTransform[5]) <
                               0.00001F;
                });
            assert(expectedMaterial);
            assert(std::none_of(
                archetype.animationBank.tracks().begin(),
                archetype.animationBank.tracks().end(), [](const auto& track) {
                    return track.property ==
                               usm::assets::ColladaAnimationProperty::
                                   TextureOffsetU ||
                           track.property ==
                               usm::assets::ColladaAnimationProperty::
                                   TextureOffsetV;
                }));
        }
        assert(firstKnifeEnemy->aiEnabled);
        assert(firstKnifeEnemy->lineSpeedCentimetersPerMillisecond == 0.3F);
        assert(firstKnifeEnemy->awarenessRadius == 1500.0F);
        assert(firstKnifeEnemy->initialAnimation == "idle_knife_at_idle");
        assert(bootstrap.enemyArchetypes()[firstKnifeEnemy->archetypeIndex]
                   .animationFile ==
               "../entities/meshes_bin/thug_bat_anim.bdae");
        const auto& knifeArchetype =
            bootstrap.enemyArchetypes()[firstKnifeEnemy->archetypeIndex];
        assert(std::all_of(
            knifeArchetype.animationBank.tracks().begin(),
            knifeArchetype.animationBank.tracks().end(), [](const auto& track) {
                return track.property !=
                           usm::assets::ColladaAnimationProperty::Rotation ||
                       track.componentCount == 4;
            }));
        const auto* knifeIdleClip =
            knifeArchetype.animationBank.findClip("idle_knife_at_idle");
        assert(knifeIdleClip != nullptr);
        assert(knifeIdleClip->durationMilliseconds() > 200);
        // Enemy Unit::UpdateDisplacement consumes the archetype's exported
        // dummy/pelvis streams just like Player. Keep this source asset gate
        // beside the skeletal-pose gate so hurt reactions cannot silently
        // regress to a moving mesh over a stationary collision body.
        assert(knifeArchetype.animationDisplacement.frameCount() != 0);
        const auto* knifeAirHurtClip =
            knifeArchetype.animationBank.findClip("air_to_fast_hurt");
        assert(knifeAirHurtClip != nullptr);
        const auto knifeAirHurtStart =
            knifeArchetype.animationDisplacement.physicalAt(
                knifeAirHurtClip->startMilliseconds);
        const auto knifeAirHurtEnd =
            knifeArchetype.animationDisplacement.physicalAt(
                knifeAirHurtClip->endMilliseconds);
        assert(std::abs(knifeAirHurtEnd.x - knifeAirHurtStart.x) > 0.01F ||
               std::abs(knifeAirHurtEnd.y - knifeAirHurtStart.y) > 0.01F ||
               std::abs(knifeAirHurtEnd.z - knifeAirHurtStart.z) > 0.01F);
        std::vector<usm::assets::ColladaGeometry> knifeIdleStartPose;
        std::vector<usm::assets::ColladaGeometry> knifeIdleLaterPose;
        assert(usm::assets::evaluateColladaPose(
            knifeArchetype.mesh, knifeArchetype.animationBank,
            knifeIdleClip->startMilliseconds, knifeIdleStartPose));
        assert(usm::assets::evaluateColladaPose(
            knifeArchetype.mesh, knifeArchetype.animationBank,
            knifeIdleClip->startMilliseconds + 200, knifeIdleLaterPose));
        assert(knifeIdleStartPose.size() == knifeIdleLaterPose.size());
        bool knifeIdlePoseChanged = false;
        for (std::size_t geometryIndex = 0;
             geometryIndex < knifeIdleStartPose.size(); ++geometryIndex) {
            assert(knifeIdleStartPose[geometryIndex].vertices.size() ==
                   knifeIdleLaterPose[geometryIndex].vertices.size());
            for (std::size_t vertexIndex = 0;
                 vertexIndex <
                 knifeIdleStartPose[geometryIndex].vertices.size();
                 ++vertexIndex) {
                const auto& start = knifeIdleStartPose[geometryIndex]
                                        .vertices[vertexIndex]
                                        .position;
                const auto& later = knifeIdleLaterPose[geometryIndex]
                                        .vertices[vertexIndex]
                                        .position;
                if (std::abs(start.x - later.x) > 0.001F ||
                    std::abs(start.y - later.y) > 0.001F ||
                    std::abs(start.z - later.z) > 0.001F) {
                    knifeIdlePoseChanged = true;
                    break;
                }
            }
        }
        assert(knifeIdlePoseChanged);
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

        usm::game::LevelEnemyRuntime molotovAttackRuntime;
        assert(molotovAttackRuntime.initialize(levelTwo));
        for (const auto& state : molotovAttackRuntime.states()) {
            if (state.asset != nullptr && state.asset->objectId != 642) {
                usm::game::CinematicThread disableThread;
                disableThread.objectId = state.asset->objectId;
                assert(molotovAttackRuntime.applyCinematicCommand(
                    levelTwo, disableThread,
                    usm::game::CinematicCommand{0, -1, "DisableAI", {}}));
            }
        }
        const auto* molotovThrower = molotovAttackRuntime.find(642);
        assert(molotovThrower != nullptr);
        const auto* molotovThrowClip =
            levelTwo.enemyArchetypes()
                [molotovThrower->asset->archetypeIndex]
                    .animationBank.findClip("idle_throw_molotov_idle");
        assert(molotovThrowClip != nullptr &&
               molotovThrowClip->durationMilliseconds() == 1500);
        const usm::assets::Vector3 molotovVictim{
            molotovThrower->position.x + 1000.0F,
            molotovThrower->position.y,
            molotovThrower->position.z};
        molotovAttackRuntime.updateGameplay(500, molotovVictim);
        molotovThrower = molotovAttackRuntime.find(642);
        assert(molotovThrower->activeAnimation ==
               "idle_throw_molotov_idle");
        assert(std::abs(molotovThrower->animationSpeed - 1.5F) < 0.001F);
        assert(molotovThrower->animationTimeMilliseconds == 750);
        assert(molotovAttackRuntime.molotovs().size() == 1);
        const auto& thrownMolotov = molotovAttackRuntime.molotovs().front();
        assert(thrownMolotov.sourceObjectId == 642);
        assert(thrownMolotov.phase ==
               usm::game::EnemyMolotovPhase::Flying);
        assert(thrownMolotov.damage == 50.0F);
        assert(std::abs(std::hypot(thrownMolotov.velocity.x,
                                  thrownMolotov.velocity.y) -
                        1000.0F) < 0.01F);
        assert(thrownMolotov.gravityCentimetersPerSecondSquared < 0.0F);

        std::array<float, 16> authoredHand{};
        const auto& molotovArchetype =
            levelTwo.enemyArchetypes()
                [molotovThrower->asset->archetypeIndex];
        assert(usm::assets::evaluateColladaSceneNodeTransform(
            molotovArchetype.mesh, molotovArchetype.animationBank,
            molotovThrowClip->startMilliseconds + 750,
            "Bip01_R_Hand", authoredHand));
        const usm::assets::Vector3 localHand{authoredHand[12],
                                             authoredHand[13],
                                             authoredHand[14]};
        const auto& throwerWorld = molotovThrower->worldTransform;
        const usm::assets::Vector3 worldHand{
            localHand.x * throwerWorld[0] +
                localHand.y * throwerWorld[4] +
                localHand.z * throwerWorld[8] + throwerWorld[12],
            localHand.x * throwerWorld[1] +
                localHand.y * throwerWorld[5] +
                localHand.z * throwerWorld[9] + throwerWorld[13],
            localHand.x * throwerWorld[2] +
                localHand.y * throwerWorld[6] +
                localHand.z * throwerWorld[10] + throwerWorld[14]};
        assert(std::abs(thrownMolotov.position.x -
                        (worldHand.x + thrownMolotov.velocity.x * 0.05F)) <
               0.01F);
        assert(std::abs(thrownMolotov.position.y -
                        (worldHand.y + thrownMolotov.velocity.y * 0.05F)) <
               0.01F);
        assert(std::abs(thrownMolotov.position.z - worldHand.z) < 0.01F);
        auto molotovEvents =
            molotovAttackRuntime.consumeProjectileEvents();
        assert(molotovEvents.size() == 1);
        assert(molotovEvents.front().kind ==
               usm::game::EnemyProjectileEventKind::Spawned);
        const auto molotovCues = molotovAttackRuntime.consumeSoundCues();
        assert(molotovCues.size() == 1);
        assert(molotovCues.front().sourceObjectId == 642);

        usm::game::LevelCollision levelTwoCollision;
        assert(levelTwoCollision.build(levelTwo.rooms()));
        // Level 2 point 831 releases Spider-Man into Room 7's authored
        // climbable facade.  This is the state-19 CheckClimbableWall(..., 4)
        // contact used by Player::UpdateMove at 0x00350a8c.
        usm::game::LevelWallContact roomSevenSwingWall;
        assert(levelTwoCollision.climbableWallContact(
            {-6893.12F, 50842.70F, 746.35F},
            {-6886.00F, 50755.00F, 743.87F}, roomSevenSwingWall));
        assert(roomSevenSwingWall.geometryName == "wall08");
        assert(roomSevenSwingWall.physicsFlags ==
               usm::game::LevelPhysicsFlags::ClimbableWall);
        assert(roomSevenSwingWall.normal.y > 0.99F);
        // wall_jump_left finishes just beyond wall07's authored x edge. The
        // center ray misses, while the native 50 cm capsule still overlaps
        // the panel and must reacquire it.
        usm::game::LevelWallContact roomSevenGapWall;
        assert(!levelTwoCollision.climbableWallContact(
            {-6060.22F, 50813.1F, 1617.82F},
            {-6060.22F, 50688.1F, 1617.82F}, roomSevenGapWall));
        assert(levelTwoCollision.climbableWallContact(
            {-6011.22F, 50813.1F, 1617.82F},
            {-6011.22F, 50688.1F, 1617.82F}, roomSevenGapWall));
        assert(roomSevenGapWall.geometryName == "wall07");
        assert(roomSevenGapWall.physicsFlags ==
               usm::game::LevelPhysicsFlags::ClimbableWall);
        usm::game::GameplayPlayer roomSevenSwingPlayer;
        assert(roomSevenSwingPlayer.initialize(
            levelTwo.player(), &levelTwoCollision, &playerStateConfigs,
            levelTwo.webGrabPoints(), levelTwo.slides(),
            levelTwo.waypoints(), &levelTwo.buttonConfigs()));
        const usm::game::CameraPose roomSevenSwingCamera{
            {-6765.22F, 53917.60F, 385.59F},
            {-6738.26F, 53218.60F, 410.91F},
            {0.0F, 0.0F, 1.0F}};
        const std::array<bool, 10> roomSevenVisible{
            false, false, false, true, true, true, true, true, false, false};
        roomSevenSwingPlayer.restoreAt(
            {-6912.50F, 52853.52F, 2.01F}, {0.0F, -1.0F, 0.0F});
        roomSevenSwingPlayer.setWebGrabViewContext(
            roomSevenSwingCamera, 640.0F / 360.0F, roomSevenVisible);
        assert(roomSevenSwingPlayer.requestJump(
            {0.0F, 1.0F}, roomSevenSwingCamera));
        for (int frame = 0; frame < 4; ++frame) {
            roomSevenSwingPlayer.update(
                {0.0F, 1.0F}, roomSevenSwingCamera, 50);
        }
        assert(roomSevenSwingPlayer.requestWeb());
        for (int frame = 0;
             frame < 80 && !roomSevenSwingPlayer.onWall(); ++frame) {
            roomSevenSwingPlayer.update(
                {0.0F, 1.0F}, roomSevenSwingCamera, 50);
        }
        assert(roomSevenSwingPlayer.onWall());
        assert(roomSevenSwingPlayer.activeStateId() == 1);
        assert(std::abs(roomSevenSwingPlayer.position().y - 50813.1F) <
               1.0F);
        // Room 7 requires a lateral jump between authored wall08 and wall07.
        // The jump direction comes from the current button frame, not the
        // previous climb frame cached by updateWallTraversal.
        assert(roomSevenSwingPlayer.requestJump(
            {1.0F, 0.0F}, roomSevenSwingCamera));
        assert(roomSevenSwingPlayer.activeStateId() == 11);
        assert(roomSevenSwingPlayer.activeAnimation() == "wall_jump_right");
        bool molotovGrounded = false;
        for (std::uint32_t elapsed = 0;
             elapsed < 3000 && !molotovGrounded; elapsed += 50) {
            molotovAttackRuntime.updateGameplay(
                50, molotovVictim, &levelTwoCollision);
            molotovGrounded =
                !molotovAttackRuntime.molotovs().empty() &&
                molotovAttackRuntime.molotovs().front().phase ==
                    usm::game::EnemyMolotovPhase::ExplodeReady;
        }
        assert(molotovGrounded);
        assert(molotovAttackRuntime.molotovs().front()
                   .phaseElapsedMilliseconds == 0);
        molotovAttackRuntime.updateGameplay(1333, molotovVictim,
                                             &levelTwoCollision);
        assert(molotovAttackRuntime.molotovs().empty());
        const auto molotovHits =
            molotovAttackRuntime.consumePlayerHits();
        assert(molotovHits.size() == 1);
        assert(molotovHits.front().sourceObjectId == 642);
        assert(molotovHits.front().damage == 50.0F);
        const auto molotovEffects =
            molotovAttackRuntime.consumeEffectCues();
        assert(molotovEffects.size() == 1);
        assert(molotovEffects.front().effectType == "molotov_bomb");
        molotovEvents = molotovAttackRuntime.consumeProjectileEvents();
        assert(std::any_of(
            molotovEvents.begin(), molotovEvents.end(), [](const auto& event) {
                return event.kind ==
                       usm::game::EnemyProjectileEventKind::Grounded;
            }));
        assert(std::any_of(
            molotovEvents.begin(), molotovEvents.end(), [](const auto& event) {
                return event.kind ==
                       usm::game::EnemyProjectileEventKind::Exploded;
            }));

        usm::game::LevelEnemyRuntime cinematicActionRuntime;
        assert(cinematicActionRuntime.initialize(levelTwo));
        const auto [throwingThread, throwingCommand] =
            findLevelTwoCommand(20017, "Throwing");
        assert(throwingThread != nullptr && throwingCommand != nullptr);
        assert(cinematicActionRuntime.applyCinematicCommand(
            levelTwo, *throwingThread, *throwingCommand));
        const auto* cinematicActionRhino =
            cinematicActionRuntime.find(20019);
        assert(cinematicActionRhino != nullptr);
        assert(cinematicActionRhino->cinematicActionActive);
        assert(cinematicActionRhino->cinematicActionObjectId == 1177);
        const auto [stopActionThread, stopActionCommand] =
            findLevelTwoCommand(20020, "StopAction");
        assert(stopActionThread != nullptr && stopActionCommand != nullptr);
        assert(cinematicActionRuntime.applyCinematicCommand(
            levelTwo, *stopActionThread, *stopActionCommand));
        cinematicActionRhino = cinematicActionRuntime.find(20019);
        assert(cinematicActionRhino != nullptr);
        assert(!cinematicActionRhino->cinematicActionActive);
        assert(cinematicActionRhino->cinematicActionObjectId == -1);
        usm::game::CinematicCommand missingThrow = *throwingCommand;
        missingThrow.attributes.front().value = "999999";
        assert(!cinematicActionRuntime.applyCinematicCommand(
            levelTwo, *throwingThread, missingThrow));

        // Rhino phase zero is recovered from CBoss' FIFO task table at
        // 0x00329cd4 and CBehaviorDush (states 78-85), not inferred from a
        // capture: approach, task-3 melee twice, then task-11 dash.
        usm::game::LevelEnemyRuntime rhinoRuntime;
        assert(rhinoRuntime.initialize(levelTwo));
        assert(rhinoRuntime.setDiagnosticAiEnabled(20055, true, true));
        const auto* rhino = rhinoRuntime.find(20055);
        assert(rhino != nullptr);
        const auto& rhinoArchetype =
            levelTwo.enemyArchetypes()[rhino->asset->archetypeIndex];
        const auto* rhinoPunch =
            rhinoArchetype.animationBank.findClip("punch_left");
        const auto* rhinoReady =
            rhinoArchetype.animationBank.findClip("idle_charge_run");
        const auto* rhinoRush =
            rhinoArchetype.animationBank.findClip("rush");
        const auto* rhinoBash =
            rhinoArchetype.animationBank.findClip("run_bash_attack");
        assert(rhinoPunch != nullptr &&
               rhinoPunch->durationMilliseconds() == 967);
        assert(rhinoReady != nullptr &&
               rhinoReady->durationMilliseconds() == 1667);
        assert(rhinoRush != nullptr &&
               rhinoRush->durationMilliseconds() == 800);
        assert(rhinoBash != nullptr &&
               rhinoBash->durationMilliseconds() == 434);
        usm::game::QuickTimeActionRuntime successfulThrow;
        successfulThrow.bind(levelTwo.quickTimeActionConfigs(),
                             levelTwo.buttonConfigs(),
                             levelTwo.player().animationBank,
                             rhinoArchetype.animationBank);
        assert(successfulThrow.begin(9));
        assert(successfulThrow.consumeEnteredActionState() == 9);
        assert(successfulThrow.playerAnimation() == "idle_to_grab");
        assert(successfulThrow.npcAnimation() == "grab_to_hold");
        assert(successfulThrow.update(5000, false));
        assert(successfulThrow.consumeEnteredActionState() == 6);
        assert(successfulThrow.buttonPromptActive());
        assert(successfulThrow.requiredActionCount() == 8);
        for (int tap = 0; tap < 8; ++tap) {
            assert(successfulThrow.update(50, true));
        }
        assert(successfulThrow.consumeEnteredActionState() == 7);
        assert(!successfulThrow.buttonPromptActive());
        assert(successfulThrow.update(699, false));
        assert(successfulThrow.consumeHits().empty());
        assert(successfulThrow.update(1, false));
        const auto successHits = successfulThrow.consumeHits();
        assert(successHits.size() == 1);
        assert(successHits.front().target ==
               usm::game::QuickTimeActionTarget::Npc);
        assert(successHits.front().animationFrame == 21);
        assert(successHits.front().damage == 100.0F);
        assert(successfulThrow.update(5000, false));
        assert(successfulThrow.consumeCompletion() ==
               usm::game::QuickTimeActionOutcome::Success);

        usm::game::QuickTimeActionRuntime failedThrow;
        failedThrow.bind(levelTwo.quickTimeActionConfigs(),
                         levelTwo.buttonConfigs(),
                         levelTwo.player().animationBank,
                         rhinoArchetype.animationBank);
        assert(failedThrow.begin(9));
        assert(failedThrow.update(5000, false));
        assert(failedThrow.consumeEnteredActionState() == 6);
        assert(failedThrow.update(4000, false));
        assert(failedThrow.consumeEnteredActionState() == 8);
        const auto struggleHits = failedThrow.consumeHits();
        assert(struggleHits.size() == 4);
        for (std::size_t index = 0; index < struggleHits.size(); ++index) {
            assert(struggleHits[index].target ==
                   usm::game::QuickTimeActionTarget::Player);
            assert(struggleHits[index].animationFrame ==
                   static_cast<std::int16_t>((index + 1) * 20));
            assert(struggleHits[index].damage == 20.0F);
        }
        assert(failedThrow.update(1665, false));
        assert(failedThrow.consumeHits().empty());
        assert(failedThrow.update(1, false));
        const auto failureHits = failedThrow.consumeHits();
        assert(failureHits.size() == 1);
        assert(failureHits.front().target ==
               usm::game::QuickTimeActionTarget::Player);
        assert(failureHits.front().animationFrame == 50);
        assert(failureHits.front().damage == 100.0F);
        assert(failedThrow.update(5000, false));
        assert(failedThrow.consumeCompletion() ==
               usm::game::QuickTimeActionOutcome::Failure);
        const usm::assets::Vector3 rhinoVictim{
            rhino->position.x + 200.0F, rhino->position.y,
            rhino->position.z};
        rhinoRuntime.updateGameplay(1, rhinoVictim);
        rhino = rhinoRuntime.find(20055);
        assert(rhino->rhinoTask ==
               usm::game::RhinoBossTaskState::Approach);
        assert(rhino->activeAnimation == "run");
        assert(rhino->rhinoMeleeAttacksRemaining == 2);
        rhinoRuntime.updateGameplay(1, rhinoVictim);
        rhino = rhinoRuntime.find(20055);
        assert(rhino->rhinoTask == usm::game::RhinoBossTaskState::Melee);
        assert(rhino->activeAnimation == "punch_left");

        const std::uint32_t rhinoPunchImpact =
            rhinoPunch->durationMilliseconds() * 57U / 100U;
        assert(rhinoPunchImpact == 551);
        rhinoRuntime.updateGameplay(rhinoPunchImpact - 1, rhinoVictim);
        auto rhinoHits = rhinoRuntime.consumePlayerHits();
        const auto firstRhinoPunch = std::find_if(
            rhinoHits.begin(), rhinoHits.end(), [](const auto& hit) {
                return hit.sourceObjectId == 20055 && hit.attackId == 12;
            });
        assert(firstRhinoPunch != rhinoHits.end());
        assert(firstRhinoPunch->damage == 50.0F);
        rhinoRuntime.updateGameplay(
            rhinoPunch->durationMilliseconds() - rhinoPunchImpact,
            rhinoVictim);
        rhinoRuntime.updateGameplay(1, rhinoVictim);
        rhino = rhinoRuntime.find(20055);
        assert(rhino->rhinoTask == usm::game::RhinoBossTaskState::Melee);
        assert(rhino->rhinoMeleeAttacksRemaining == 1);
        rhinoRuntime.updateGameplay(rhinoPunchImpact - 1, rhinoVictim);
        rhinoHits = rhinoRuntime.consumePlayerHits();
        const auto secondRhinoPunch = std::find_if(
            rhinoHits.begin(), rhinoHits.end(), [](const auto& hit) {
                return hit.sourceObjectId == 20055 && hit.attackId == 12;
            });
        assert(secondRhinoPunch != rhinoHits.end());
        assert(secondRhinoPunch->damage == 50.0F);
        rhinoRuntime.updateGameplay(
            rhinoPunch->durationMilliseconds() - rhinoPunchImpact,
            rhinoVictim);
        rhinoRuntime.updateGameplay(1, rhinoVictim);
        rhino = rhinoRuntime.find(20055);
        assert(rhino->rhinoTask ==
               usm::game::RhinoBossTaskState::DashReady);
        assert(rhino->activeAnimation == "idle_charge_run");
        assert(rhino->rhinoMeleeAttacksRemaining == 0);
        rhinoRuntime.updateGameplay(
            rhinoReady->durationMilliseconds() - 1, rhinoVictim);
        rhinoRuntime.updateGameplay(1, rhinoVictim);
        rhino = rhinoRuntime.find(20055);
        assert(rhino->rhinoTask ==
               usm::game::RhinoBossTaskState::DashRush);
        assert(rhino->activeAnimation == "rush");
        for (std::uint32_t elapsed = 0;
             elapsed < rhinoRush->durationMilliseconds() &&
             rhinoRuntime.find(20055)->rhinoTask ==
                 usm::game::RhinoBossTaskState::DashRush;
             elapsed += 25) {
            rhinoRuntime.updateGameplay(25, rhinoVictim);
        }
        rhino = rhinoRuntime.find(20055);
        assert(rhino->rhinoTask ==
               usm::game::RhinoBossTaskState::DashSuccess);
        assert(rhino->activeAnimation == "run_bash_attack");
        rhinoHits = rhinoRuntime.consumePlayerHits();
        const auto rhinoDashHit = std::find_if(
            rhinoHits.begin(), rhinoHits.end(), [](const auto& hit) {
                return hit.sourceObjectId == 20055 && hit.attackId == 17;
            });
        assert(rhinoDashHit != rhinoHits.end());
        assert(rhinoDashHit->damage == 80.0F);

        usm::game::LevelEnemyRuntime rhinoPhaseOneRuntime;
        assert(rhinoPhaseOneRuntime.initialize(levelTwo));
        assert(rhinoPhaseOneRuntime.setDiagnosticAiEnabled(20055, true,
                                                            true));
        const auto* phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(phaseOneRhino != nullptr);
        const usm::assets::Vector3 phaseHitPosition = phaseOneRhino->position;
        assert(rhinoPhaseOneRuntime.applyPlayerMeleeHit(
                   phaseHitPosition, {1.0F, 0.0F, 0.0F}, 10.0F,
                   phaseOneRhino->maximumHealth, -1.0F) == 20055);
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(phaseOneRhino->rhinoPhase == 1);
        assert(std::abs(phaseOneRhino->health -
                        phaseOneRhino->maximumHealth * 0.66F) < 0.01F);
        const auto* rhinoHurt = rhinoArchetype.animationBank.findClip(
            phaseOneRhino->activeAnimation);
        assert(rhinoHurt != nullptr);
        rhinoPhaseOneRuntime.updateGameplay(
            rhinoHurt->durationMilliseconds(), phaseHitPosition);
        rhinoPhaseOneRuntime.updateGameplay(1, phaseHitPosition);
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        const usm::assets::Vector3 phaseOneVictim{
            phaseOneRhino->position.x + 200.0F, phaseOneRhino->position.y,
            phaseOneRhino->position.z};
        rhinoPhaseOneRuntime.updateGameplay(1, phaseOneVictim);
        rhinoPhaseOneRuntime.updateGameplay(1, phaseOneVictim);
        for (int melee = 0; melee < 2; ++melee) {
            rhinoPhaseOneRuntime.updateGameplay(
                rhinoPunch->durationMilliseconds(), phaseOneVictim);
            rhinoPhaseOneRuntime.updateGameplay(1, phaseOneVictim);
        }
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(phaseOneRhino->rhinoTask ==
               usm::game::RhinoBossTaskState::ThrowApproach);
        rhinoPhaseOneRuntime.updateGameplay(1, phaseOneVictim);
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(phaseOneRhino->rhinoTask ==
               usm::game::RhinoBossTaskState::ThrowReady);
        const auto* rhinoGrabReady =
            rhinoArchetype.animationBank.findClip("idle_to_grab_ready");
        assert(rhinoGrabReady != nullptr &&
               rhinoGrabReady->durationMilliseconds() == 600);
        rhinoPhaseOneRuntime.updateGameplay(
            rhinoGrabReady->durationMilliseconds(), phaseOneVictim);
        rhinoPhaseOneRuntime.updateGameplay(1, phaseOneVictim);
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(phaseOneRhino->rhinoTask ==
               usm::game::RhinoBossTaskState::ThrowRush);
        rhinoPhaseOneRuntime.updateGameplay(50, phaseOneVictim);
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(phaseOneRhino->rhinoTask ==
               usm::game::RhinoBossTaskState::ThrowCatch);
        assert(rhinoPhaseOneRuntime.rhinoQuickTimeAction().active());
        assert(rhinoPhaseOneRuntime.rhinoQuickTimeAction().actionStateId() ==
               9);
        const auto attachedPlayerTransform =
            rhinoPhaseOneRuntime.rhinoQuickTimePlayerWorldTransform();
        assert(attachedPlayerTransform.has_value());
        const auto* grabToHold =
            rhinoArchetype.animationBank.findClip("grab_to_hold");
        assert(grabToHold != nullptr);
        std::array<float, 16> handModelTransform{};
        assert(usm::assets::evaluateColladaSceneNodeTransform(
            rhinoArchetype.mesh, rhinoArchetype.animationBank,
            grabToHold->startMilliseconds, "R_Hand_Dummy",
            handModelTransform));
        const usm::assets::Vector3 alignedHandOrigin{
            handModelTransform[12] * phaseOneRhino->worldTransform[0] +
                handModelTransform[13] * phaseOneRhino->worldTransform[4] +
                handModelTransform[14] * phaseOneRhino->worldTransform[8] +
                phaseOneRhino->worldTransform[12],
            handModelTransform[12] * phaseOneRhino->worldTransform[1] +
                handModelTransform[13] * phaseOneRhino->worldTransform[5] +
                handModelTransform[14] * phaseOneRhino->worldTransform[9] +
                phaseOneRhino->worldTransform[13],
            handModelTransform[12] * phaseOneRhino->worldTransform[2] +
                handModelTransform[13] * phaseOneRhino->worldTransform[6] +
                handModelTransform[14] * phaseOneRhino->worldTransform[10] +
                phaseOneRhino->worldTransform[14]};
        assert(std::abs(alignedHandOrigin.x - phaseOneVictim.x) < 0.001F);
        assert(std::abs(alignedHandOrigin.y - phaseOneVictim.y) < 0.001F);
        const usm::assets::Vector3 handRelativePlayerOrigin{
            handModelTransform[12] -
                usm::game::kPlayerCollisionHalfHeightCentimeters *
                    handModelTransform[8],
            handModelTransform[13] -
                usm::game::kPlayerCollisionHalfHeightCentimeters *
                    handModelTransform[9],
            handModelTransform[14] -
                usm::game::kPlayerCollisionHalfHeightCentimeters *
                    handModelTransform[10]};
        const auto& rhinoWorld = phaseOneRhino->worldTransform;
        const usm::assets::Vector3 expectedAttachedOrigin{
            handRelativePlayerOrigin.x * rhinoWorld[0] +
                handRelativePlayerOrigin.y * rhinoWorld[4] +
                handRelativePlayerOrigin.z * rhinoWorld[8] + rhinoWorld[12],
            handRelativePlayerOrigin.x * rhinoWorld[1] +
                handRelativePlayerOrigin.y * rhinoWorld[5] +
                handRelativePlayerOrigin.z * rhinoWorld[9] + rhinoWorld[13],
            handRelativePlayerOrigin.x * rhinoWorld[2] +
                handRelativePlayerOrigin.y * rhinoWorld[6] +
                handRelativePlayerOrigin.z * rhinoWorld[10] + rhinoWorld[14]};
        assert(std::abs((*attachedPlayerTransform)[12] -
                        expectedAttachedOrigin.x) < 0.001F);
        assert(std::abs((*attachedPlayerTransform)[13] -
                        expectedAttachedOrigin.y) < 0.001F);
        assert(std::abs((*attachedPlayerTransform)[14] -
                        expectedAttachedOrigin.z) < 0.001F);
        usm::game::GameplayPlayer attachedPlayer;
        assert(attachedPlayer.initialize(levelTwo.player()));
        const auto detachPosition =
            rhinoPhaseOneRuntime.rhinoQuickTimePlayerDetachPosition();
        assert(detachPosition.has_value());
        attachedPlayer.setQuickTimeActionPose(
            rhinoPhaseOneRuntime.rhinoQuickTimeAction().playerAnimation(), 0,
            false, &attachedPlayerTransform.value(), nullptr,
            &detachPosition.value());
        assert(attachedPlayer.quickTimeActionDriven());
        assert(attachedPlayer.worldTransform() ==
               attachedPlayerTransform.value());
        attachedPlayer.clearQuickTimeActionPose();
        assert(!attachedPlayer.quickTimeActionDriven());
        assert(std::abs(attachedPlayer.position().x -
                        detachPosition->x) < 0.001F);
        assert(std::abs(attachedPlayer.position().y -
                        detachPosition->y) < 0.001F);
        assert(std::abs(attachedPlayer.position().z -
                        detachPosition->z) < 0.001F);
        rhinoPhaseOneRuntime.updateGameplay(5000, phaseOneVictim);
        assert(rhinoPhaseOneRuntime.rhinoQuickTimeAction().actionStateId() ==
               6);
        for (int tap = 0; tap < 8; ++tap) {
            rhinoPhaseOneRuntime.updateGameplay(50, phaseOneVictim, nullptr,
                                                 true);
        }
        assert(rhinoPhaseOneRuntime.rhinoQuickTimeAction().actionStateId() ==
               7);
        const float healthBeforeThrowCounter =
            rhinoPhaseOneRuntime.find(20055)->health;
        rhinoPhaseOneRuntime.updateGameplay(700, phaseOneVictim);
        assert(std::abs(rhinoPhaseOneRuntime.find(20055)->health -
                        (healthBeforeThrowCounter - 100.0F)) < 0.01F);
        rhinoPhaseOneRuntime.updateGameplay(5000, phaseOneVictim);
        phaseOneRhino = rhinoPhaseOneRuntime.find(20055);
        assert(!rhinoPhaseOneRuntime.rhinoQuickTimeAction().active());
        assert(phaseOneRhino->rhinoTask ==
               usm::game::RhinoBossTaskState::DashReady);
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
        const std::uint32_t knifeSenseFrame =
            knifeAttackClip->durationMilliseconds() * 2U / 100U;
        assert(firstKnifeImpact > 0);
        assert(secondKnifeImpact > firstKnifeImpact);
        const usm::assets::Vector3 knifeVictim{
            attackingKnife->position.x + 100.0F,
            attackingKnife->position.y,
            attackingKnife->position.z};
        usm::game::LevelEnemyRuntime senseTimingRuntime;
        assert(senseTimingRuntime.initialize(bootstrap));
        assert(senseTimingRuntime.setDiagnosticAiEnabled(395, false));
        assert(senseTimingRuntime.setDiagnosticAiEnabled(397, false));
        assert(senseTimingRuntime.setDiagnosticAiEnabled(394, true, true));
        senseTimingRuntime.updateGameplay(knifeSenseFrame - 1U, knifeVictim);
        assert(senseTimingRuntime.find(394)->meleeAttackActive);
        assert(!senseTimingRuntime.find(394)->meleeSenseActive);
        assert(senseTimingRuntime.findSpiderSenseAttacker(knifeVictim) == nullptr);
        senseTimingRuntime.updateGameplay(1U, knifeVictim);
        assert(senseTimingRuntime.find(394)->meleeSenseActive);
        assert(senseTimingRuntime.findSpiderSenseAttacker(knifeVictim) != nullptr);
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
        assert(firstKnifeHit->hitType == 100);
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
        assert(enemyAttackRuntime.find(394)->meleeAttackActive);
        assert(!enemyAttackRuntime.find(394)->animationLoops);
        enemyAttackRuntime.updateGameplay(
            knifeAttackClip->durationMilliseconds() - secondKnifeImpact,
            knifeVictim);
        assert(enemyAttackRuntime.find(394)->meleeAttackActive);
        enemyAttackRuntime.updateGameplay(1, knifeVictim);
        attackingKnife = enemyAttackRuntime.find(394);
        assert(!attackingKnife->meleeAttackActive);
        assert(attackingKnife->activeAnimation == "idle_knife_at_idle");
        assert(attackingKnife->animationLoops);
        assert(!attackingKnife->meleeAttackRegistered);
        assert(attackingKnife->meleeAttackCooldownMilliseconds == 2000);
        // Registration consumes the first native value. The two authored
        // action sounds each consume one random(0, 1) call at their key
        // frames, so unregister uses the fourth value and produces a
        // 1745 ms manager handoff before the 2000 ms enemy interval.
        enemyAttackRuntime.updateGameplay(1744, knifeVictim);
        assert(!enemyAttackRuntime.find(394)->meleeAttackActive);
        enemyAttackRuntime.updateGameplay(1, knifeVictim);
        assert(!enemyAttackRuntime.find(394)->meleeAttackActive);
        enemyAttackRuntime.updateGameplay(1998, knifeVictim);
        assert(!enemyAttackRuntime.find(394)->meleeAttackActive);
        enemyAttackRuntime.updateGameplay(1, knifeVictim);
        attackingKnife = enemyAttackRuntime.find(394);
        assert(attackingKnife->meleeAttackActive);
        assert(!attackingKnife->animationLoops);
        assert(attackingKnife->meleeAttackRegistered);
        assert(attackingKnife->animationTimeMilliseconds == 1);

        // The bat's state-11 table exposes two equally ranked native choices.
        // With the shipped generator, the first registration consumes 7407
        // for its lease and the tie roll is 34: jumping wins. Its authored
        // action sound consumes the third value; unregister gets 1745 ms,
        // then the second lease is 8087 and tie roll 51 selects standing.
        usm::game::LevelEnemyRuntime batAttackRuntime;
        assert(batAttackRuntime.initialize(bootstrap));
        assert(batAttackRuntime.setDiagnosticAiEnabled(394, false));
        assert(batAttackRuntime.setDiagnosticAiEnabled(397, false));
        assert(batAttackRuntime.setDiagnosticAiEnabled(395, true, true));
        const auto* attackingBat = batAttackRuntime.find(395);
        assert(attackingBat != nullptr);
        usm::assets::Vector3 batVictim{
            attackingBat->position.x + 100.0F,
            attackingBat->position.y,
            attackingBat->position.z};
        batAttackRuntime.updateGameplay(1, batVictim);
        attackingBat = batAttackRuntime.find(395);
        assert(attackingBat->activeAnimation == "idle_jump_at3_idle");
        assert(attackingBat->meleeRegistrationTimerMilliseconds == 7407.0F);
        const auto* standingBatClip =
            bootstrap.enemyArchetypes()[attackingBat->asset->archetypeIndex]
                .animationBank.findClip("idle_at1_idle");
        const auto* jumpingBatClip =
            bootstrap.enemyArchetypes()[attackingBat->asset->archetypeIndex]
                .animationBank.findClip("idle_jump_at3_idle");
        assert(standingBatClip != nullptr && jumpingBatClip != nullptr);
        const auto advanceBatKeepingVictimNear =
            [&](std::uint32_t milliseconds) {
                std::vector<usm::game::EnemyPlayerHit> hits;
                for (std::uint32_t elapsed = 0; elapsed < milliseconds;
                     ++elapsed) {
                    const auto* bat = batAttackRuntime.find(395);
                    batVictim = {
                        bat->position.x + bat->facing.x * 100.0F,
                        bat->position.y + bat->facing.y * 100.0F,
                        bat->position.z};
                    batAttackRuntime.updateGameplay(1, batVictim);
                    auto frameHits = batAttackRuntime.consumePlayerHits();
                    hits.insert(hits.end(), frameHits.begin(),
                                frameHits.end());
                }
                return hits;
            };
        const std::uint32_t jumpingBatImpact =
            jumpingBatClip->durationMilliseconds() * 70U / 100U;
        auto batHits = advanceBatKeepingVictimNear(
            jumpingBatImpact - attackingBat->animationTimeMilliseconds);
        assert(batHits.size() == 1);
        assert(batHits.front().sourceObjectId == 395);
        assert(batHits.front().attackId == 11);
        assert(batHits.front().damage == 50.0F);
        assert(batHits.front().hitType == 101);
        (void)advanceBatKeepingVictimNear(
            jumpingBatClip->durationMilliseconds() - jumpingBatImpact + 1U);
        attackingBat = batAttackRuntime.find(395);
        batVictim = {attackingBat->position.x + attackingBat->facing.x * 100.0F,
                     attackingBat->position.y + attackingBat->facing.y * 100.0F,
                     attackingBat->position.z};
        batAttackRuntime.updateGameplay(1744, batVictim);
        assert(!batAttackRuntime.find(395)->meleeAttackActive);
        attackingBat = batAttackRuntime.find(395);
        batVictim = {attackingBat->position.x + attackingBat->facing.x * 100.0F,
                     attackingBat->position.y + attackingBat->facing.y * 100.0F,
                     attackingBat->position.z};
        batAttackRuntime.updateGameplay(1, batVictim);
        batAttackRuntime.updateGameplay(1998, batVictim);
        batAttackRuntime.updateGameplay(1, batVictim);
        attackingBat = batAttackRuntime.find(395);
        assert(attackingBat->activeAnimation == "idle_at1_idle");
        assert(attackingBat->meleeRegistrationTimerMilliseconds == 8087.0F);
        const std::uint32_t standingBatImpact =
            standingBatClip->durationMilliseconds() * 47U / 100U;
        assert(attackingBat->animationTimeMilliseconds <= standingBatImpact);
        batHits = advanceBatKeepingVictimNear(
            standingBatImpact - attackingBat->animationTimeMilliseconds);
        assert(batHits.size() == 1);
        assert(batHits.front().sourceObjectId == 395);
        assert(batHits.front().attackId == 7);
        assert(batHits.front().damage == 35.0F);
        assert(batHits.front().hitType == 101);

        usm::game::LevelEnemyRuntime separationRuntime;
        assert(separationRuntime.initialize(bootstrap));
        for (const std::int32_t objectId : {394, 395, 397}) {
            assert(separationRuntime.setDiagnosticAiEnabled(objectId, true));
        }
        const usm::assets::Vector3 sharedMeleeTarget{13399.0F, -7000.0F,
                                                     7.0F};
        for (int frame = 0; frame < 160; ++frame) {
            separationRuntime.updateGameplay(50, sharedMeleeTarget);
        }
        std::size_t registeredMeleeEnemies = 0;
        for (const std::int32_t firstId : {394, 395, 397}) {
            const auto* first = separationRuntime.find(firstId);
            assert(first != nullptr);
            registeredMeleeEnemies += first->meleeAttackRegistered ? 1U : 0U;
            for (const std::int32_t secondId : {394, 395, 397}) {
                if (secondId <= firstId) {
                    continue;
                }
                const auto* second = separationRuntime.find(secondId);
                assert(second != nullptr);
                const float distance = std::hypot(
                    second->position.x - first->position.x,
                    second->position.y - first->position.y);
                assert(distance + 0.1F >=
                       first->collisionRadius + second->collisionRadius);
            }
        }
        assert(registeredMeleeEnemies <= 1);
        // Correct 300 cm bat spacing can prevent this particular convergence
        // from overlapping at all; the pairwise radius assertions above are
        // the invariant, whether or not depenetration had to emit an event.
        (void)separationRuntime.consumeEnemySeparationEvents();
        usm::game::LevelEnemyRuntime spawnRuntime;
        assert(spawnRuntime.initialize(bootstrap));
        assert(!spawnRuntime.find(398)->visible);
        assert(!spawnRuntime.find(399)->visible);
        assert(!spawnRuntime.find(400)->visible);
        assert(!spawnRuntime.find(401)->visible);
        const auto secondEncounterCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 974;
            });
        assert(secondEncounterCinematic != bootstrap.cinematics().end());
        usm::game::CinematicPlayer secondEncounterPlayer;
        assert(secondEncounterPlayer.start(secondEncounterCinematic->script));
        usm::Result secondEncounterResult = usm::Result::success();
        assert(secondEncounterPlayer.advanceTo(
            4000, [&](const usm::game::CinematicThread& thread,
                      const usm::game::CinematicCommand& command) {
                if (secondEncounterResult) {
                    secondEncounterResult =
                        spawnRuntime.applyCinematicCommand(
                            bootstrap, thread, command);
                }
            }));
        assert(secondEncounterResult);
        usm::game::LevelCollision spawnCollision;
        assert(spawnCollision.build(bootstrap.rooms()));
        for (int frame = 0; frame < 40; ++frame) {
            spawnRuntime.updateGameplay(50, {}, &spawnCollision);
        }
        for (const std::int32_t objectId : {398, 399, 401}) {
            const auto* spawnedEnemy = spawnRuntime.find(objectId);
            assert(spawnedEnemy != nullptr && spawnedEnemy->visible);
            assert(spawnedEnemy->physicsActive);
            assert(spawnedEnemy->grounded);
            assert(spawnedEnemy->position.z < 100.0F);
        }
        usm::game::LevelEnemyRuntime damageRuntime;
        assert(damageRuntime.initialize(bootstrap));
        assert(damageRuntime.spiderSenseSlowMotionDenominator(394) == 3.0F);
        const auto* damageTarget = damageRuntime.find(394);
        assert(damageTarget != nullptr);
        const usm::assets::Vector3 damagePosition = damageTarget->position;
        const usm::assets::Vector3 attackPosition{
            damagePosition.x - 100.0F, damagePosition.y, damagePosition.z};
        const usm::assets::Vector3 verticallySeparatedAttackPosition{
            attackPosition.x, attackPosition.y,
            attackPosition.z + 1000.0F};
        assert(!damageRuntime.applyPlayerMeleeHit(
            verticallySeparatedAttackPosition, {1.0F, 0.0F, 0.0F},
            200.0F, 100.0F));
        assert(!damageRuntime.applyPlayerMeleeHit(
            attackPosition, {-1.0F, 0.0F, 0.0F}, 200.0F, 100.0F));
        const auto firstHit = damageRuntime.applyPlayerMeleeHit(
            attackPosition, {1.0F, 0.0F, 0.0F}, 200.0F, 100.0F);
        assert(firstHit && *firstHit == 394);
        assert(damageRuntime.find(394)->health == 400.0F);
        assert(damageRuntime.find(394)->behavior ==
               usm::game::EnemyBehaviorState::Hurt);
        // ParseAnimInfo mode 2 (0x003a8648-0x003a866a) samples the three
        // common-hurt clips. Initial native RNG state selects index two.
        assert(damageRuntime.find(394)->activeAnimation ==
               "idle_hurt_right_idle");
        assert(!damageRuntime.find(394)->animationLoops);
        usm::game::LevelEnemyRuntime senseHitRuntime;
        assert(senseHitRuntime.initialize(bootstrap));
        const auto* senseArcTarget = senseHitRuntime.find(395);
        assert(senseArcTarget != nullptr);
        const auto senseHits = senseHitRuntime.applyPlayerSenseMeleeHits(
            senseArcTarget->position, {1.0F, 0.0F, 0.0F}, 1.0F, 300.0F,
            -1.0F, 394);
        assert(std::any_of(
            senseHits.begin(), senseHits.end(), [](const auto& hit) {
                return hit.objectId == 395 && hit.actualDamage == 300.0F;
            }));
        assert(std::any_of(
            senseHits.begin(), senseHits.end(), [](const auto& hit) {
                return hit.objectId == 394 && hit.actualDamage == 300.0F;
            }));
        assert(senseHitRuntime.find(394)->health == 200.0F);
        usm::game::LevelEnemyRuntime sectorHitRuntime;
        assert(sectorHitRuntime.initialize(bootstrap));
        const auto* sectorTargetA = sectorHitRuntime.find(394);
        const auto* sectorTargetB = sectorHitRuntime.find(395);
        const auto* sectorTargetC = sectorHitRuntime.find(397);
        assert(sectorTargetA != nullptr && sectorTargetB != nullptr &&
               sectorTargetC != nullptr);
        const usm::assets::Vector3 sectorCenter{
            (sectorTargetA->position.x + sectorTargetB->position.x +
             sectorTargetC->position.x) /
                3.0F,
            (sectorTargetA->position.y + sectorTargetB->position.y +
             sectorTargetC->position.y) /
                3.0F,
            sectorTargetA->position.z};
        const auto sectorHits =
            sectorHitRuntime.applyPlayerSectorMeleeHits(
                sectorCenter, {1.0F, 0.0F, 0.0F}, 550.0F, 35.0F,
                -1.0F, 102, &sectorCenter, 0.0F, 600.0F);
        for (const std::int32_t objectId : {394, 395, 397}) {
            assert(std::any_of(
                sectorHits.begin(), sectorHits.end(),
                [objectId](const auto& hit) {
                    return hit.objectId == objectId &&
                           hit.actualDamage == 35.0F;
                }));
            assert(sectorHitRuntime.find(objectId)->health == 465.0F);
        }
        usm::game::LevelEnemyRuntime capsuleCenterRuntime;
        assert(capsuleCenterRuntime.initialize(bootstrap));
        const auto* capsuleCenterTarget = capsuleCenterRuntime.find(394);
        assert(capsuleCenterTarget != nullptr);
        const usm::assets::Vector3 capsuleBoundaryAttack{
            capsuleCenterTarget->position.x - 100.0F,
            capsuleCenterTarget->position.y,
            capsuleCenterTarget->position.z +
                usm::game::kPlayerCollisionHalfHeightCentimeters +
                capsuleCenterTarget->collisionRadius + 1.0F};
        // testPieCollision (0x003d5434/0x003d5462) transforms the capsule's
        // createEnemyPhysics-authored local {0,0,radius} center. This narrow
        // overlap misses if the target is incorrectly centered on Unit base.
        const auto capsuleBoundaryHits =
            capsuleCenterRuntime.applyPlayerSectorMeleeHits(
                capsuleBoundaryAttack, {1.0F, 0.0F, 0.0F}, 200.0F, 1.0F,
                -1.0F, 100, &capsuleBoundaryAttack, 0.0F, 0.0F);
        assert(capsuleBoundaryHits.size() == 1);
        assert(capsuleBoundaryHits.front().objectId == 394);
        // Player::SendHitMessage reuses one AIHitTargetInfo and scales its
        // vertical force by 0.85 immediately before each dispatch.
        assert(std::abs(sectorHitRuntime.find(394)->verticalVelocity -
                        510.0F) < 0.001F);
        assert(std::abs(sectorHitRuntime.find(395)->verticalVelocity -
                        433.5F) < 0.001F);
        assert(std::abs(sectorHitRuntime.find(397)->verticalVelocity -
                        368.475F) < 0.001F);
        // Motion 0x6d is the exception to the usual shared sector record.
        // Player::CheckAttackTarget (0x0034fca0, 0x003505ce-0x00350614)
        // sends the retained Unit the raw kick-down record, while bystanders
        // receive the separately constructed type-0x79, 200/500-force hit.
        usm::game::LevelEnemyRuntime airKickDownSectorRuntime;
        assert(airKickDownSectorRuntime.initialize(bootstrap));
        const auto airKickDownHits =
            airKickDownSectorRuntime.applyPlayerAirKickDownSectorMeleeHits(
                sectorCenter, {1.0F, 0.0F, 0.0F}, 550.0F, 80.0F,
                -1.0F, 394, &sectorCenter, 320.0F, 700.0F);
        assert(airKickDownHits.size() == 3);
        const auto* retainedKickDownTarget =
            airKickDownSectorRuntime.find(394);
        assert(retainedKickDownTarget != nullptr);
        assert(retainedKickDownTarget->lastPlayerHitType == 109);
        assert(retainedKickDownTarget->hurtStateId == 69);
        assert(std::abs(retainedKickDownTarget->verticalVelocity -
                        595.0F) < 0.001F);
        assert(std::abs(std::hypot(
                            retainedKickDownTarget->hurtVelocity.x,
                            retainedKickDownTarget->hurtVelocity.y) -
                        320.0F) < 0.001F);
        for (const std::int32_t objectId : {395, 397}) {
            const auto* collateral = airKickDownSectorRuntime.find(objectId);
            assert(collateral != nullptr);
            assert(collateral->lastPlayerHitType == 121);
            // The 0x79 family chooses front/back variants from each thug's
            // authored facing and available animation set.
            assert(collateral->hurtStateId == 49 ||
                   collateral->hurtStateId == 50 ||
                   collateral->hurtStateId == 52 ||
                   collateral->hurtStateId == 53);
            assert(collateral->verticalVelocity == 500.0F);
            assert(std::abs(std::hypot(collateral->hurtVelocity.x,
                                       collateral->hurtVelocity.y) -
                            200.0F) < 0.001F);
        }
        usm::game::LevelEnemyRuntime measuredDamageRuntime;
        assert(measuredDamageRuntime.initialize(bootstrap));
        const auto measuredHit =
            measuredDamageRuntime.applyPlayerMeleeHitDetailed(
                attackPosition, {1.0F, 0.0F, 0.0F}, 200.0F, 1000.0F);
        assert(measuredHit && measuredHit->objectId == 394);
        assert(measuredHit->actualDamage == 500.0F);
        // Default difficulty and no damage upgrade: Player::SendHitMessage
        // leaves shipped contact damage unchanged. The collision-complete
        // autoplay gate proves that both state 78 pulses contact the correctly
        // centered grounded enemy cylinder. This targeted ledger independently
        // proves every resulting health delta and final clamp.
        usm::game::LevelEnemyRuntime healthAuditRuntime;
        assert(healthAuditRuntime.initialize(bootstrap));
        const std::array<float, 9> openingChainDamage{
            35.0F, 35.0F, 55.0F, 85.0F, 85.0F,
            80.0F, 35.0F, 35.0F, 55.0F};
        const std::array<float, 9> openingChainHealth{
            465.0F, 430.0F, 375.0F, 290.0F, 205.0F,
            125.0F, 90.0F, 55.0F, 0.0F};
        for (std::size_t hit = 0; hit < openingChainDamage.size(); ++hit) {
            const auto result =
                healthAuditRuntime.applyPlayerTargetedHitDetailed(
                    394, openingChainDamage[hit]);
            assert(result && result->objectId == 394);
            assert(result->actualDamage ==
                   std::min(openingChainDamage[hit],
                            hit == 0 ? 500.0F
                                     : openingChainHealth[hit - 1]));
            assert(healthAuditRuntime.find(394)->health ==
                   openingChainHealth[hit]);
        }
        usm::game::LevelEnemyRuntime knockbackRuntime;
        assert(knockbackRuntime.initialize(bootstrap));
        assert(knockbackRuntime.setDiagnosticAiEnabled(394, true));
        knockbackRuntime.updateGameplay(16, {}, &encounterCollision);
        const auto knockbackStart = knockbackRuntime.find(394)->position;
        const usm::assets::Vector3 knockbackSource{
            knockbackStart.x - 100.0F, knockbackStart.y,
            knockbackStart.z};
        assert(knockbackRuntime.applyPlayerMeleeHitDetailed(
            knockbackSource, {1.0F, 0.0F, 0.0F}, 200.0F, 35.0F,
            0.0F, 100, &knockbackSource, 400.0F, 0.0F));
        assert(knockbackRuntime.find(394)->hurtStateId == 49);
        assert(knockbackRuntime.find(394)->hurtVelocity.x > 399.0F);
        knockbackRuntime.updateGameplay(50, {}, &encounterCollision);
        assert(knockbackRuntime.find(394)->position.x > knockbackStart.x);

        usm::game::LevelEnemyRuntime launcherHitRuntime;
        assert(launcherHitRuntime.initialize(bootstrap));
        assert(launcherHitRuntime.setDiagnosticAiEnabled(394, true));
        launcherHitRuntime.updateGameplay(16, {}, &encounterCollision);
        const auto launcherTargetStart =
            launcherHitRuntime.find(394)->position;
        const usm::assets::Vector3 launcherSource{
            launcherTargetStart.x - 100.0F, launcherTargetStart.y,
            launcherTargetStart.z};
        assert(launcherHitRuntime.applyPlayerMeleeHitDetailed(
            launcherSource, {1.0F, 0.0F, 0.0F}, 200.0F, 35.0F,
            0.0F, 102, &launcherSource, 0.0F, 1000.0F));
        const auto* launchedEnemy = launcherHitRuntime.find(394);
        assert(launchedEnemy->hurtStateId == 57);
        assert(launchedEnemy->activeAnimation == "idle_to_air");
        assert(!launchedEnemy->grounded);
        assert(launchedEnemy->verticalVelocity == 850.0F);
        launcherHitRuntime.updateGameplay(50, {}, &encounterCollision);
        assert(launcherHitRuntime.find(394)->position.z >
               launcherTargetStart.z);
        for (int frame = 0;
             frame < 200 &&
             launcherHitRuntime.find(394)->hurtStateId != 58;
             ++frame) {
            launcherHitRuntime.updateGameplay(10, {}, &encounterCollision);
        }
        launchedEnemy = launcherHitRuntime.find(394);
        assert(launchedEnemy->hurtStateId == 58);
        assert(launchedEnemy->activeAnimation == "air_to_fall");
        assert(!launchedEnemy->grounded);
        assert(launcherHitRuntime.applyPlayerTargetedHitDetailed(
            394, 35.0F, 105, &launcherSource));
        launchedEnemy = launcherHitRuntime.find(394);
        assert(launchedEnemy->hurtStateId == 60);
        assert(launchedEnemy->activeAnimation == "air_to_fast_hurt");

        // CBehaviorHurt::BehaviorStart (0x003b87a8) can select an ordinary
        // ground hurt clip for a light hit received during an authored enemy
        // jump. Unit::UpdateDisplacement (0x00324df0) still moves that root
        // through the native cylinder, so its downward Dummy track must land
        // on the street rather than tunnelling below it.
        usm::game::LevelEnemyRuntime airborneGroundHurtRuntime;
        assert(airborneGroundHurtRuntime.initialize(bootstrap));
        assert(airborneGroundHurtRuntime.setDiagnosticAiEnabled(395, true));
        airborneGroundHurtRuntime.updateGameplay(
            16, {}, &encounterCollision);
        const auto airborneGroundHurtStart =
            airborneGroundHurtRuntime.find(395)->position;
        const usm::assets::Vector3 airborneGroundHurtSource{
            airborneGroundHurtStart.x - 100.0F,
            airborneGroundHurtStart.y,
            airborneGroundHurtStart.z};
        assert(airborneGroundHurtRuntime.applyPlayerTargetedHitDetailed(
            395, 35.0F, 102, &airborneGroundHurtSource,
            0.0F, 1000.0F));
        airborneGroundHurtRuntime.updateGameplay(
            50, {}, &encounterCollision);
        assert(!airborneGroundHurtRuntime.find(395)->grounded);
        assert(airborneGroundHurtRuntime.applyPlayerTargetedHitDetailed(
            395, 35.0F, 100, &airborneGroundHurtSource,
            400.0F, 0.0F));
        for (int frame = 0; frame < 200 &&
             !airborneGroundHurtRuntime.find(395)->grounded; ++frame) {
            airborneGroundHurtRuntime.updateGameplay(
                10, {}, &encounterCollision);
        }
        assert(airborneGroundHurtRuntime.find(395)->grounded);
        assert(airborneGroundHurtRuntime.find(395)->position.z > -1.0F);

        // CBehaviorHurt state 0x45 has a separate, source-proven landing
        // presentation. Pin every member and its lifetime so an animation-
        // only kickdown cannot silently replace the native impact again.
        usm::game::LevelEnemyRuntime kickdownLandingRuntime;
        assert(kickdownLandingRuntime.initialize(bootstrap));
        assert(kickdownLandingRuntime.setDiagnosticAiEnabled(394, true));
        kickdownLandingRuntime.updateGameplay(
            16, {}, &encounterCollision);
        const auto kickdownPosition =
            kickdownLandingRuntime.find(394)->position;
        const usm::assets::Vector3 kickdownSource{
            kickdownPosition.x - 100.0F, kickdownPosition.y,
            kickdownPosition.z};
        assert(kickdownLandingRuntime.applyPlayerTargetedHitDetailed(
            394, 35.0F, 109, &kickdownSource));
        assert(kickdownLandingRuntime.find(394)->hurtStateId == 69);
        (void)kickdownLandingRuntime.consumeSoundCues();
        kickdownLandingRuntime.updateGameplay(
            0, {}, &encounterCollision);
        assert(kickdownLandingRuntime.find(394)->hurtStateId == 70);
        assert(kickdownLandingRuntime.find(394)->activeAnimation ==
               "onground_to_whirl_flying");
        const auto cameraShakes =
            kickdownLandingRuntime.consumeCameraShakeCues();
        assert(cameraShakes.size() == 1);
        assert(cameraShakes.front().maximumOffset == 3.0F);
        assert(cameraShakes.front().frameCount == 12);
        assert(cameraShakes.front().axisRates.x == 1.0F);
        assert(cameraShakes.front().axisRates.y == 1.0F);
        assert(cameraShakes.front().axisRates.z == 1.0F);
        const auto landingParticles =
            kickdownLandingRuntime.consumeEffectCues();
        assert(landingParticles.size() == 1);
        assert(landingParticles.front().effectType == "rock_splash");
        assert(landingParticles.front().position.x == kickdownPosition.x);
        assert(landingParticles.front().position.y == kickdownPosition.y);
        assert(landingParticles.front().position.z == kickdownPosition.z);
        const auto landingSounds =
            kickdownLandingRuntime.consumeSoundCues();
        assert(std::ranges::any_of(
            landingSounds,
            [](const usm::game::EnemySoundCue& cue) {
                return cue.voxSoundId == 0x4b;
            }));
        auto landingModels =
            kickdownLandingRuntime.landingAnimatedEffects();
        assert(landingModels.size() == 2);
        assert(landingModels[0].kind ==
               usm::game::EnemyLandingAnimatedEffectKind::Shockwave);
        assert(landingModels[0].scale == 1.5F);
        assert(landingModels[0].lifetimeMilliseconds == 1000);
        assert(landingModels[0].additiveModulateMaterial);
        assert(landingModels[1].kind ==
               usm::game::EnemyLandingAnimatedEffectKind::CrashWall);
        assert(landingModels[1].scale == 5.0F);
        assert(landingModels[1].lifetimeMilliseconds == 5000);
        assert(!landingModels[1].additiveModulateMaterial);
        const auto landingModelSpawns =
            kickdownLandingRuntime
                .consumeLandingAnimatedEffectSpawnEvents();
        assert(landingModelSpawns.size() == 2);
        assert(landingModelSpawns[0].kind ==
               usm::game::EnemyLandingAnimatedEffectKind::Shockwave);
        assert(landingModelSpawns[0].scale == 1.5F);
        assert(landingModelSpawns[0].lifetimeMilliseconds == 1000);
        assert(landingModelSpawns[0].additiveModulateMaterial);
        assert(landingModelSpawns[1].kind ==
               usm::game::EnemyLandingAnimatedEffectKind::CrashWall);
        assert(landingModelSpawns[1].scale == 5.0F);
        assert(landingModelSpawns[1].lifetimeMilliseconds == 5000);
        assert(!landingModelSpawns[1].additiveModulateMaterial);
        assert(kickdownLandingRuntime
                   .consumeLandingAnimatedEffectSpawnEvents()
                   .empty());
        kickdownLandingRuntime.updateGameplay(
            1000, {}, &encounterCollision);
        landingModels = kickdownLandingRuntime.landingAnimatedEffects();
        assert(landingModels.size() == 1);
        assert(landingModels.front().kind ==
               usm::game::EnemyLandingAnimatedEffectKind::CrashWall);
        kickdownLandingRuntime.updateGameplay(
            4000, {}, &encounterCollision);
        assert(kickdownLandingRuntime.landingAnimatedEffects().empty());
        usm::game::LevelEnemyRuntime webBindingRuntime;
        assert(webBindingRuntime.initialize(bootstrap));
        assert(webBindingRuntime.setDiagnosticAiEnabled(394, false));
        const auto webBindingHit =
            webBindingRuntime.applyPlayerWebBindingDetailed(394, 20.0F);
        assert(webBindingHit && webBindingHit->objectId == 394);
        assert(webBindingHit->actualDamage == 20.0F);
        const auto* tiedEnemy = webBindingRuntime.find(394);
        assert(tiedEnemy != nullptr);
        assert(tiedEnemy->health == 480.0F);
        assert(tiedEnemy->behavior ==
               usm::game::EnemyBehaviorState::TiedUp);
        assert(tiedEnemy->activeAnimation == "tied");
        assert(tiedEnemy->animationLoops);
        assert(tiedEnemy->tiedUpRemainingMilliseconds == 4000);
        webBindingRuntime.updateGameplay(3999, {});
        tiedEnemy = webBindingRuntime.find(394);
        assert(tiedEnemy->behavior ==
               usm::game::EnemyBehaviorState::TiedUp);
        assert(tiedEnemy->tiedUpRemainingMilliseconds == 1);
        webBindingRuntime.updateGameplay(1, {});
        tiedEnemy = webBindingRuntime.find(394);
        assert(tiedEnemy->behavior ==
               usm::game::EnemyBehaviorState::Disabled);
        assert(tiedEnemy->tiedUpRemainingMilliseconds == 0);
        usm::game::LevelEnemyRuntime airKnockdownBindingRuntime;
        assert(airKnockdownBindingRuntime.initialize(bootstrap));
        assert(airKnockdownBindingRuntime.setDiagnosticAiEnabled(394,
                                                                 false));
        const float tiedLieStartZ =
            airKnockdownBindingRuntime.find(394)->position.z;
        assert(airKnockdownBindingRuntime.applyPlayerAirKnockdownBinding(
            394));
        const auto* tiedLieEnemy = airKnockdownBindingRuntime.find(394);
        assert(tiedLieEnemy->behavior ==
               usm::game::EnemyBehaviorState::TiedUp);
        assert(tiedLieEnemy->activeAnimation == "air_to_onground");
        assert(tiedLieEnemy->animationLoops);
        assert(tiedLieEnemy->tiedUpRemainingMilliseconds == 4000);
        airKnockdownBindingRuntime.updateGameplay(
            100, {}, &encounterCollision);
        tiedLieEnemy = airKnockdownBindingRuntime.find(394);
        assert(tiedLieEnemy->position.z > tiedLieStartZ);
        enemySoundCues = damageRuntime.consumeSoundCues();
        assert(enemySoundCues.size() == 1);
        // SetState samples the three common-hurt voices after ParseAnimInfo;
        // the second native generator value selects sound-map index two.
        assert(enemySoundCues.front().voxSoundId == 187);
        usm::game::LevelEnemyRuntime airborneHurtRuntime;
        assert(airborneHurtRuntime.initialize(bootstrap));
        usm::game::CinematicThread airborneHurtThread;
        airborneHurtThread.objectId = 394;
        usm::game::CinematicCommand raiseEnemy;
        raiseEnemy.name = "MoveObject";
        raiseEnemy.attributes.push_back(
            {"vector3d", "abspos",
             "13355.999023, -6311.277344, 505.131531"});
        assert(airborneHurtRuntime.applyCinematicCommand(
            bootstrap, airborneHurtThread, raiseEnemy));
        assert(airborneHurtRuntime.applyPlayerMeleeHit(
            {13255.999023F, -6311.277344F, 505.131531F},
            {1.0F, 0.0F, 0.0F}, 200.0F, 35.0F));
        const float airborneHurtStartZ =
            airborneHurtRuntime.find(394)->position.z;
        airborneHurtRuntime.updateGameplay(50, {}, &encounterCollision);
        assert(airborneHurtRuntime.find(394)->behavior ==
               usm::game::EnemyBehaviorState::Hurt);
        assert(airborneHurtRuntime.find(394)->position.z <
               airborneHurtStartZ);
        assert(airborneHurtRuntime.find(394)->verticalVelocity < 0.0F);
        // The Room 1 storefront boundary is one slanted vertical triangle.
        // Its upper vertex is offset in XY, so deriving a collision plane
        // from the triangle's longest projected edge places the portable
        // wall on the wrong side of the road. Reproduce the first-encounter
        // web launch at that boundary and require the native 50 cm cylinder
        // manifold to keep the thug supported through landing and get-up.
        usm::game::LevelEnemyRuntime storefrontLaunchRuntime;
        assert(storefrontLaunchRuntime.initialize(bootstrap));
        usm::game::CinematicThread storefrontLaunchThread;
        storefrontLaunchThread.objectId = 395;
        usm::game::CinematicCommand positionStorefrontTarget;
        positionStorefrontTarget.name = "MoveObject";
        positionStorefrontTarget.attributes.push_back(
            {"vector3d", "abspos", "13361.6, -6733.75, 5.73798"});
        assert(storefrontLaunchRuntime.applyCinematicCommand(
            bootstrap, storefrontLaunchThread, positionStorefrontTarget));
        assert(storefrontLaunchRuntime.applyCinematicCommand(
            bootstrap, storefrontLaunchThread,
            usm::game::CinematicCommand{0, -1, "EnableAI", {}}));
        const usm::assets::Vector3 storefrontHitSource{
            13389.19F, -6829.87F, 5.73798F};
        assert(storefrontLaunchRuntime.applyPlayerTargetedHitDetailed(
            395, 35.0F, 121, &storefrontHitSource, 800.0F, 460.0F));
        bool storefrontTargetLanded = false;
        float storefrontMinimumZ = std::numeric_limits<float>::max();
        usm::assets::Vector3 storefrontLandingPosition;
        for (int frame = 0; frame < 80; ++frame) {
            storefrontLaunchRuntime.updateGameplay(
                50, storefrontHitSource, &encounterCollision);
            const auto* frameTarget = storefrontLaunchRuntime.find(395);
            assert(frameTarget != nullptr);
            storefrontMinimumZ =
                std::min(storefrontMinimumZ, frameTarget->position.z);
            if (frameTarget->grounded) {
                storefrontTargetLanded = true;
                storefrontLandingPosition = frameTarget->position;
            }
        }
        const auto* storefrontTarget = storefrontLaunchRuntime.find(395);
        assert(storefrontTarget != nullptr);
        assert(storefrontTarget->position.z > 0.0F);
        assert(storefrontMinimumZ > 0.0F);
        assert(storefrontTargetLanded);
        float storefrontSupport = 0.0F;
        assert(encounterCollision.groundHeight(
            storefrontLandingPosition, storefrontTarget->collisionRadius,
            5.0F, storefrontSupport));
        assert(std::abs(storefrontLandingPosition.z - storefrontSupport) <
               1e-3F);
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
        assert(enemySoundCues[0].voxSoundId == 185);
        assert(enemySoundCues[1].voxSoundId == 186);
        assert(enemySoundCues[2].voxSoundId == 185);
        assert(enemySoundCues[3].voxSoundId == 188);
        usm::game::LevelCollision levelCollision;
        assert(levelCollision.build(bootstrap.rooms()));
        assert(levelCollision.triangleCount() > 100);
        // Room 8 Plane02 has a 4.15 cm lip at y=5243.45: triangles 7/8
        // raise the roof from z=3141.20 to z=3145.35, while triangles 22/23
        // form its vertical edge. createSpridemanPhysics (0x003d8a34) uses a
        // 50 cm lower sphere and processSphereTriangle (0x003d23cc) resolves
        // its closest edge point. The player therefore rolls across this lip
        // instead of treating the separate 185 cm gameplay height as a wall.
        usm::assets::Vector3 rooftopLipResolved;
        assert(levelCollision.resolveGroundMotion(
            {5984.235840F, 5236.448242F, 3141.200000F},
            {5984.235840F, 5255.000000F, 3141.200000F},
            rooftopLipResolved));
        assert(rooftopLipResolved.y > 5243.45F);
        assert(std::abs(rooftopLipResolved.z - 3145.348145F) < 0.02F);
        // The same edge is wholly below the lower sphere late in the jump.
        // The native capsule/mesh query has no contact in that frame, so the
        // airborne sweep must not retain the old tall-prism obstruction.
        usm::assets::Vector3 rooftopLipAirResolved;
        levelCollision.resolveAirMotion(
            {5984.235840F, 5227.000000F, 3248.000000F},
            {5984.235840F, 5250.000000F, 3195.000000F},
            rooftopLipAirResolved);
        assert(std::abs(rooftopLipAirResolved.y - 5250.0F) < 0.01F);
        // Plane02 triangles 40/41 form a tall authored-normal +Y face at
        // y=5286.45. The normal player route approaches from its back side.
        // PhysicsTriangleMeshShape::constructMesh (0x003d95d8) preserves
        // that winding, so the ordinary face is one-way; only an explicit
        // DoubleSide flag blocks both directions. The reverse traversal must
        // still stop at the native 50 cm lower-sphere margin.
        usm::assets::Vector3 rooftopBackfaceResolved;
        assert(levelCollision.resolveGroundMotion(
            {5984.235840F, 5236.448242F, 3145.348145F},
            {5984.235840F, 5320.000000F, 3145.348145F},
            rooftopBackfaceResolved));
        assert(rooftopBackfaceResolved.y > 5286.45F);
        usm::assets::Vector3 rooftopFrontFaceResolved;
        assert(levelCollision.resolveGroundMotion(
            {5984.235840F, 5350.000000F, 3145.348145F},
            {5984.235840F, 5200.000000F, 3145.348145F},
            rooftopFrontFaceResolved));
        assert(rooftopFrontFaceResolved.y >= 5336.44F);
        // Room 8 enemy 421 starts on the first rooftop after the falling-tank
        // traversal.  Its first awareness tick must advance by only the
        // authored 0.3 cm/ms line speed.  This catches wall depenetration
        // incorrectly snapping a newly active enemy across the rooftop.
        usm::game::LevelEnemyRuntime rooftopEnemyRuntime;
        assert(rooftopEnemyRuntime.initialize(bootstrap));
        const auto* rooftopEnemy = rooftopEnemyRuntime.find(421);
        assert(rooftopEnemy != nullptr);
        const usm::assets::Vector3 rooftopEnemyStart =
            rooftopEnemy->position;
        const usm::assets::Vector3 rooftopEnemyDesired{
            rooftopEnemyStart.x + 2.4945F,
            rooftopEnemyStart.y - 7.0730F,
            rooftopEnemyStart.z};
        usm::assets::Vector3 rooftopEnemyResolved;
        assert(levelCollision.resolveGroundMotion(
            rooftopEnemyStart, rooftopEnemyDesired, rooftopEnemyResolved));
        assert(std::sqrt(
                   (rooftopEnemyResolved.x - rooftopEnemyStart.x) *
                       (rooftopEnemyResolved.x - rooftopEnemyStart.x) +
                   (rooftopEnemyResolved.y - rooftopEnemyStart.y) *
                       (rooftopEnemyResolved.y - rooftopEnemyStart.y)) <=
               7.6F);
        rooftopEnemyRuntime.updateGameplay(
            25, {5844.52F, 4823.41F, 3141.20F}, &levelCollision);
        rooftopEnemy = rooftopEnemyRuntime.find(421);
        assert(rooftopEnemy != nullptr);
        const float rooftopEnemyTravelX =
            rooftopEnemy->position.x - rooftopEnemyStart.x;
        const float rooftopEnemyTravelY =
            rooftopEnemy->position.y - rooftopEnemyStart.y;
        assert(std::sqrt(rooftopEnemyTravelX * rooftopEnemyTravelX +
                         rooftopEnemyTravelY * rooftopEnemyTravelY) <= 7.6F);
        // The authored Room 1 player spawn overlaps the southern collision
        // shell by roughly 29 cm at the native 50 cm capsule radius.  A
        // resolver that preserves the side of the initial overlap strands
        // Spider-Man outside the street and eventually wedges him at the
        // next shell corner.  Player-specific recovery toward the face's
        // authored normal allows an uninterrupted route into the first
        // encounter while enemy depenetration retains its conservative rule.
        usm::assets::Vector3 streetRoutePosition =
            bootstrap.player().position;
        const usm::assets::Vector3 firstEncounterRouteTarget{
            14067.479492F, -8205.359375F, 7.0F};
        for (int frame = 0; frame < 300; ++frame) {
            const float differenceX =
                firstEncounterRouteTarget.x - streetRoutePosition.x;
            const float differenceY =
                firstEncounterRouteTarget.y - streetRoutePosition.y;
            const float distance = std::sqrt(
                differenceX * differenceX + differenceY * differenceY);
            if (distance <= 50.0F) {
                break;
            }
            const float step = std::min(35.0F, distance);
            const usm::assets::Vector3 desired{
                streetRoutePosition.x + differenceX / distance * step,
                streetRoutePosition.y + differenceY / distance * step,
                streetRoutePosition.z};
            usm::assets::Vector3 resolved;
            assert(levelCollision.resolveGroundMotion(
                streetRoutePosition, desired, resolved, 75.0F, 150.0F, 0U,
                usm::game::LevelCollisionDepenetration::TowardAuthoredNormal));
            streetRoutePosition = resolved;
        }
        const float streetRouteDifferenceX =
            firstEncounterRouteTarget.x - streetRoutePosition.x;
        const float streetRouteDifferenceY =
            firstEncounterRouteTarget.y - streetRoutePosition.y;
        assert(std::sqrt(streetRouteDifferenceX * streetRouteDifferenceX +
                         streetRouteDifferenceY * streetRouteDifferenceY) <=
               50.0F);
        usm::game::LevelEnemyRuntime unsupportedSpawnRuntime;
        assert(unsupportedSpawnRuntime.initialize(bootstrap));
        const auto* unsupportedSpawn = unsupportedSpawnRuntime.find(417);
        assert(unsupportedSpawn != nullptr);
        const float unsupportedSpawnZ = unsupportedSpawn->position.z;
        unsupportedSpawnRuntime.updateGameplay(50, {}, &levelCollision);
        unsupportedSpawn = unsupportedSpawnRuntime.find(417);
        assert(unsupportedSpawn->anchoredWithoutSupport);
        assert(unsupportedSpawn->grounded);
        assert(unsupportedSpawn->position.z == unsupportedSpawnZ);
        const usm::assets::Vector3 unsupportedSpawnIngressTarget{
            -500.0F, 385.0F, unsupportedSpawnZ};
        for (int frame = 0;
             frame < 200 && unsupportedSpawn->anchoredWithoutSupport;
             ++frame) {
            unsupportedSpawnRuntime.updateGameplay(
                50, unsupportedSpawnIngressTarget, &levelCollision);
            unsupportedSpawn = unsupportedSpawnRuntime.find(417);
        }
        assert(!unsupportedSpawn->anchoredWithoutSupport);
        assert(unsupportedSpawn->grounded);
        float unsupportedSpawnGroundHeight = 0.0F;
        assert(levelCollision.groundHeight(unsupportedSpawn->position, 5.0F,
                                           5.0F,
                                           unsupportedSpawnGroundHeight));
        assert(std::abs(unsupportedSpawn->position.z -
                        unsupportedSpawnGroundHeight) < 1e-3F);
        assert(unsupportedSpawn->position.x < 400.0F);
        for (int frame = 0; frame < 100; ++frame) {
            unsupportedSpawnRuntime.updateGameplay(
                50, unsupportedSpawnIngressTarget, &levelCollision);
        }
        unsupportedSpawn = unsupportedSpawnRuntime.find(417);
        assert(unsupportedSpawn->behavior ==
               usm::game::EnemyBehaviorState::Chasing);
        assert(!unsupportedSpawn->meleeAttackActive);
        usm::game::LevelEnemyRuntime sandmanTaskRuntime;
        assert(sandmanTaskRuntime.initialize(bootstrap));
        usm::game::CinematicThread sandmanThread;
        sandmanThread.objectId = 1199;
        usm::game::CinematicCommand showSandman;
        showSandman.name = "SetVisible";
        showSandman.attributes.push_back({"bool", "Visible", "true"});
        assert(sandmanTaskRuntime.applyCinematicCommand(
            bootstrap, sandmanThread, showSandman));
        assert(sandmanTaskRuntime.applyCinematicCommand(
            bootstrap, sandmanThread,
            usm::game::CinematicCommand{0, -1, "EnableAI", {}}));
        const auto* taskedSandman = sandmanTaskRuntime.find(1199);
        assert(taskedSandman != nullptr);
        const usm::assets::Vector3 sandmanTarget{
            taskedSandman->position.x + 100.0F, taskedSandman->position.y,
            taskedSandman->position.z};
        sandmanTaskRuntime.updateGameplay(1, sandmanTarget);
        taskedSandman = sandmanTaskRuntime.find(1199);
        assert(taskedSandman->sandmanTask ==
               usm::game::SandmanBossTaskState::GroundAttack);
        assert(taskedSandman->activeAnimation == "ground_attack1");
        assert(taskedSandman->meleeAttackActive);
        assert(!taskedSandman->animationLoops);
        sandmanTaskRuntime.updateGameplay(666, sandmanTarget);
        sandmanTaskRuntime.updateGameplay(1, sandmanTarget);
        taskedSandman = sandmanTaskRuntime.find(1199);
        assert(taskedSandman->sandmanTask ==
               usm::game::SandmanBossTaskState::GroundAttackRecovery);
        assert(taskedSandman->activeAnimation ==
               "ground_attack1_to_idle");
        assert(!taskedSandman->meleeAttackActive);
        sandmanTaskRuntime.updateGameplay(766, sandmanTarget);
        sandmanTaskRuntime.updateGameplay(1, sandmanTarget);
        taskedSandman = sandmanTaskRuntime.find(1199);
        assert(taskedSandman->sandmanTask ==
               usm::game::SandmanBossTaskState::Jump);
        assert(taskedSandman->activeAnimation == "idle_to_jump_to_air");
        assert(taskedSandman->sandmanJumpDurationMilliseconds == 2133);
        sandmanTaskRuntime.updateGameplay(2133, sandmanTarget);
        taskedSandman = sandmanTaskRuntime.find(1199);
        assert(taskedSandman->sandmanTask ==
               usm::game::SandmanBossTaskState::GroundAttack);
        assert(taskedSandman->activeAnimation == "ground_attack1");
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
        usm::game::GameplayPlayer deadStatePlayer;
        assert(deadStatePlayer.initialize(
            bootstrap.player(), &levelCollision, &playerStateConfigs));
        assert(deadStatePlayer.applyDamage(deadStatePlayer.maximumHealth()));
        assert(deadStatePlayer.dead());
        assert(deadStatePlayer.activeStateId() == 0x80);
        assert(deadStatePlayer.activeStateName() == "k_state_dead_over");
        assert(deadStatePlayer.consumeEnteredState() == "k_state_dead_over");
        usm::game::GameplayPlayer hurtReactionPlayer;
        assert(hurtReactionPlayer.initialize(
            bootstrap.player(), &levelCollision, &playerStateConfigs));
        const auto& hurtClip = bootstrap.player().animationBank.clips()[
            static_cast<std::size_t>(hurtState->primaryAnimationId)];
        assert(hurtReactionPlayer.applyDamage(30.0F, 0, 1000));
        assert(hurtReactionPlayer.activeAnimation() == hurtClip.name);
        assert(hurtReactionPlayer.activeStateId() == 44);
        assert(!hurtReactionPlayer.requestPunch());
        const std::uint32_t hurtDuration =
            std::max<std::uint32_t>(1000, hurtClip.durationMilliseconds());
        hurtReactionPlayer.update({}, {}, hurtDuration - 1);
        assert(hurtReactionPlayer.activeAnimation() == hurtClip.name);
        hurtReactionPlayer.update({}, {}, 1);
        assert(hurtReactionPlayer.activeAnimation() == "idle_stand");
        usm::game::GameplayPlayer heavyHurtPlayer;
        assert(heavyHurtPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs));
        heavyHurtPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                  {1.0F, 0.0F, 0.0F});
        assert(heavyHurtPlayer.applyDamage(35.0F, 0, 0, 101));
        assert(heavyHurtPlayer.activeStateId() == 45);
        assert(heavyHurtPlayer.activeStateName() == "k_state_hurt_heavy");
        assert(heavyHurtPlayer.activeAnimation() ==
               "idle_to_hurt_heavy_to_idle");
        heavyHurtPlayer.update({}, {}, 400);
        assert(heavyHurtPlayer.position().x < -50.0F);
        heavyHurtPlayer.update({}, {}, 400);
        assert(heavyHurtPlayer.activeAnimation() == "idle_stand");
        hurtReactionPlayer.restoreAt({500.0F, 600.0F, 700.0F},
                                     {0.0F, 1.0F, 0.0F});
        assert(hurtReactionPlayer.position().x == 500.0F);
        assert(hurtReactionPlayer.position().y == 600.0F);
        assert(hurtReactionPlayer.position().z == 700.0F);
        assert(hurtReactionPlayer.worldTransform()[12] == 500.0F);
        assert(hurtReactionPlayer.activeAnimation() == "idle_stand");
        usm::game::GameplayPlayer cinematicDamagePlayer;
        assert(cinematicDamagePlayer.initialize(bootstrap.player(),
                                                &levelCollision));
        usm::game::CinematicThread playerDamageThread;
        playerDamageThread.type = 3;
        playerDamageThread.objectId = bootstrap.player().objectId;
        assert(cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread,
            usm::game::CinematicCommand{0, -1, "DisableAI", {}}));
        assert(cinematicDamagePlayer.cinematicDriven());
        usm::game::CinematicCommand scriptedPlayerAnimation;
        scriptedPlayerAnimation.name = "SetAnim";
        scriptedPlayerAnimation.attributes.push_back(
            {"string", "$Anim", "knockback_flying_to_onground"});
        scriptedPlayerAnimation.attributes.push_back(
            {"bool", "loop", "false"});
        scriptedPlayerAnimation.attributes.push_back(
            {"float", "speed", "1.000000"});
        assert(cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread, scriptedPlayerAnimation));
        assert(cinematicDamagePlayer.activeAnimation() ==
               "knockback_flying_to_onground");
        usm::game::CinematicCommand missingPlayerAnimation;
        missingPlayerAnimation.name = "SetAnim";
        missingPlayerAnimation.attributes.push_back(
            {"string", "$Anim", "missing_native_no_op"});
        assert(cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread, missingPlayerAnimation));
        assert(cinematicDamagePlayer.activeAnimation() ==
               "knockback_flying_to_onground");
        usm::game::CinematicCommand scriptedPlayerMove;
        scriptedPlayerMove.name = "MoveObject";
        scriptedPlayerMove.attributes.push_back(
            {"vector3d", "abspos", "4331.134766, 4229.501953, 114.176003"});
        scriptedPlayerMove.attributes.push_back(
            {"quaternion", "rot",
             "0.007853, -0.006498, -0.712064, 0.702041"});
        assert(cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread, scriptedPlayerMove));
        assert(std::abs(cinematicDamagePlayer.position().x - 4331.134766F) <
               0.001F);
        cinematicDamagePlayer.update({}, {}, 100);
        assert(cinematicDamagePlayer.animationTimeMilliseconds() == 100);
        usm::game::CinematicThread movingPlayerThread;
        movingPlayerThread.type = 3;
        movingPlayerThread.objectId = bootstrap.player().objectId;
        usm::game::CinematicCommand movingPlayerStart;
        movingPlayerStart.timestampMilliseconds = 100;
        movingPlayerStart.name = "MoveObject";
        movingPlayerStart.attributes.push_back(
            {"vector3d", "abspos", "10.0, 20.0, 30.0"});
        movingPlayerStart.attributes.push_back(
            {"quaternion", "rot", "0.0, 0.0, 0.0, 1.0"});
        usm::game::CinematicCommand movingPlayerEnd;
        movingPlayerEnd.timestampMilliseconds = 1100;
        movingPlayerEnd.name = "MoveObject";
        movingPlayerEnd.attributes.push_back(
            {"vector3d", "abspos", "210.0, 420.0, 630.0"});
        movingPlayerEnd.attributes.push_back(
            {"quaternion", "rot", "0.0, 0.0, 0.707107, 0.707107"});
        movingPlayerThread.commands.push_back(movingPlayerStart);
        movingPlayerThread.commands.push_back(movingPlayerEnd);
        assert(cinematicDamagePlayer.applyCinematicCommand(
            movingPlayerThread, movingPlayerThread.commands.front()));
        cinematicDamagePlayer.update({}, {}, 250);
        assert(cinematicDamagePlayer.position().x == 10.0F);
        cinematicDamagePlayer.update({}, {}, 250);
        assert(std::abs(cinematicDamagePlayer.position().x - 60.0F) <
               0.001F);
        assert(std::abs(cinematicDamagePlayer.position().y - 120.0F) <
               0.001F);
        assert(std::abs(cinematicDamagePlayer.position().z - 180.0F) <
               0.001F);
        assert(cinematicDamagePlayer.applyCinematicCommand(
            movingPlayerThread, movingPlayerThread.commands.back()));
        assert(cinematicDamagePlayer.position().x == 210.0F);
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
        assert(cinematicDamagePlayer.applyCinematicCommand(
            playerDamageThread,
            usm::game::CinematicCommand{0, -1, "EnableAI", {}}));
        assert(!cinematicDamagePlayer.cinematicDriven());
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
        const auto& initialInvisibleRooms = gameplayCamera.mustInvisibleRooms();
        for (std::size_t roomIndex = 0;
             roomIndex < initialInvisibleRooms.size(); ++roomIndex) {
            const bool expected = roomIndex == 1 || roomIndex == 4 ||
                                  roomIndex == 5 || roomIndex == 6 ||
                                  roomIndex == 7;
            assert(initialInvisibleRooms[roomIndex] == expected);
        }
        assert(std::none_of(gameplayCamera.mustVisibleRooms().begin(),
                            gameplayCamera.mustVisibleRooms().end(),
                            [](bool visible) { return visible; }));
        const auto gameplayCameraPose =
            gameplayCamera.sample(bootstrap.player().position);
        assert(gameplayCamera.relocateToContainingArea(
            {7575.0F, -6305.0F, 1042.0F}));
        assert(gameplayCamera.currentAreaId() != 283);
        assert(gameplayCamera.relocateToContainingArea(
            bootstrap.player().position));
        assert(gameplayCamera.currentAreaId() == 283);
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
        assert(levelCommandRuntime.listenerOnMainCharacter());
        assert(levelCommandRuntime.applyCommand(
            controlCommand("ListenerPosition", "ListenerOnMC", "false")));
        assert(!levelCommandRuntime.listenerOnMainCharacter());
        assert(levelCommandRuntime.applyCommand(
            controlCommand("ListenerPosition", "ListenerOnMC", "true")));
        assert(levelCommandRuntime.listenerOnMainCharacter());
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
            {"bool", "AttributionEnable", "false"});
        disableControls.attributes.push_back(
            {"bool", "ArrowEnable", "false"});
        disableControls.attributes.push_back(
            {"bool", "BlackEnable", "true"});
        disableControls.attributes.push_back(
            {"bool", "SkipEnable", "true"});
        assert(levelCommandRuntime.applyCommand(disableControls));
        assert(!levelCommandRuntime.controlsEnabled());
        assert(!levelCommandRuntime.attributionEnabled());
        assert(!levelCommandRuntime.objectiveArrowEnabled());
        assert(levelCommandRuntime.blackOverlayEnabled());
        assert(levelCommandRuntime.skipEnabled());
        levelCommandRuntime.endQuickTimeEvent();
        assert(levelCommandRuntime.controlsEnabled());
        assert(!levelCommandRuntime.attributionEnabled());
        assert(!levelCommandRuntime.objectiveArrowEnabled());
        assert(levelCommandRuntime.blackOverlayEnabled());
        assert(levelCommandRuntime.skipEnabled());
        levelCommandRuntime.setColladaMovieUi(true);
        assert(!levelCommandRuntime.controlsEnabled());
        assert(!levelCommandRuntime.attributionEnabled());
        assert(levelCommandRuntime.blackOverlayEnabled());
        assert(levelCommandRuntime.skipEnabled());
        levelCommandRuntime.completeColladaPlayback(false, false);
        assert(levelCommandRuntime.controlsEnabled());
        assert(levelCommandRuntime.attributionEnabled());
        assert(!levelCommandRuntime.objectiveArrowEnabled());
        assert(!levelCommandRuntime.blackOverlayEnabled());
        assert(!levelCommandRuntime.skipEnabled());
        disableControls.attributes.front().value = "true";
        disableControls.attributes[1].value = "true";
        disableControls.attributes[2].value = "true";
        disableControls.attributes[3].value = "false";
        disableControls.attributes[4].value = "false";
        assert(levelCommandRuntime.applyCommand(disableControls));
        assert(levelCommandRuntime.controlsEnabled());
        assert(levelCommandRuntime.attributionEnabled());
        assert(levelCommandRuntime.objectiveArrowEnabled());
        assert(!levelCommandRuntime.blackOverlayEnabled());
        assert(!levelCommandRuntime.skipEnabled());
        disableControls.attributes.front().value = "false";
        disableControls.attributes[1].value = "false";
        disableControls.attributes[2].value = "false";
        disableControls.attributes[3].value = "true";
        disableControls.attributes[4].value = "true";
        assert(levelCommandRuntime.applyCommand(disableControls));
        levelCommandRuntime.bind(triggerRuntime, gameplayCamera,
                                 bootstrap.waypoints());
        assert(levelCommandRuntime.controlsEnabled());
        assert(levelCommandRuntime.attributionEnabled());
        assert(levelCommandRuntime.objectiveArrowEnabled());
        assert(!levelCommandRuntime.blackOverlayEnabled());
        assert(!levelCommandRuntime.skipEnabled());
        usm::game::CinematicCommand forceRooms;
        forceRooms.name = "MustBeVisibleRoom";
        forceRooms.attributes = {
            {"string", "MustBeVisible", "1,2,3,4,5"},
            {"bool", "Set", "true"},
        };
        assert(levelCommandRuntime.applyCommand(forceRooms));
        for (std::size_t roomIndex = 0;
             roomIndex < levelCommandRuntime.forcedVisibleRooms().size();
             ++roomIndex) {
            assert(levelCommandRuntime.forcedVisibleRooms()[roomIndex] ==
                   (roomIndex < 5));
        }
        forceRooms.attributes.back().value = "false";
        forceRooms.attributes.front().value.clear();
        assert(levelCommandRuntime.applyCommand(forceRooms));
        assert(std::none_of(
            levelCommandRuntime.forcedVisibleRooms().begin(),
            levelCommandRuntime.forcedVisibleRooms().end(),
            [](bool visible) { return visible; }));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("StartCinematic", "CinematicID", "1238")));
        const auto cinematicStarts =
            levelCommandRuntime.consumeCinematicStartRequests();
        assert(cinematicStarts.size() == 1 && cinematicStarts.front() == 1238);
        assert(levelCommandRuntime.consumeCinematicStartRequests().empty());
        assert(levelCommandRuntime.applyCommand(
            controlCommand("Save", "^ID^CheckPoint", "30027")));
        assert(levelCommandRuntime.lastCheckpointId() == 30027);
        assert(levelCommandRuntime.applyCommand(
            controlCommand("StartTimer", "InitValue", "0")));
        assert(levelCommandRuntime.bossRushTimerRunning());
        usm::game::LevelCinematicRuntime levelTwoProgressRuntime;
        levelTwoProgressRuntime.bind(triggerRuntime, gameplayCamera,
                                     levelTwo.waypoints());
        const auto [startProgressThread, startProgressCommand] =
            findLevelTwoCommand(20020, "StartProgress");
        assert(startProgressThread != nullptr &&
               startProgressCommand != nullptr);
        assert(levelTwoProgressRuntime.applyCommand(*startProgressThread,
                                                    *startProgressCommand));
        const auto startWayPoint = std::find_if(
            levelTwo.waypoints().begin(), levelTwo.waypoints().end(),
            [](const auto& wayPoint) {
                return wayPoint.objectId == 20071;
            });
        const auto endWayPoint = std::find_if(
            levelTwo.waypoints().begin(), levelTwo.waypoints().end(),
            [](const auto& wayPoint) {
                return wayPoint.objectId == 20080;
            });
        assert(startWayPoint != levelTwo.waypoints().end());
        assert(endWayPoint != levelTwo.waypoints().end());
        const float expectedProgressFailureDistance =
            std::hypot(startWayPoint->position.x - endWayPoint->position.x,
                       startWayPoint->position.y - endWayPoint->position.y) *
            0.88F;
        assert(levelTwoProgressRuntime.bossProgress().bossObjectId == 20019);
        assert(levelTwoProgressRuntime.bossProgress().startWayPointId ==
               20071);
        assert(levelTwoProgressRuntime.bossProgress().endWayPointId ==
               20080);
        assert(std::abs(
                   levelTwoProgressRuntime.bossProgress().failureDistance -
                   expectedProgressFailureDistance) < 0.01F);
        assert(levelTwoProgressRuntime.bossProgressVisible());
        const usm::assets::Vector3 progressPlayer{};
        const usm::assets::Vector3 nearRhino{100.0F, 0.0F, 500.0F};
        levelTwoProgressRuntime.advanceBossProgress(
            0, progressPlayer, &nearRhino);
        assert(std::abs(
                   levelTwoProgressRuntime.bossProgress().currentDistance -
                   100.0F) < 0.001F);
        const auto [rhinoStopThread, rhinoStopCommand] =
            findLevelTwoCommand(20090, "RhinoStop");
        assert(rhinoStopThread != nullptr && rhinoStopCommand != nullptr);
        assert(levelTwoProgressRuntime.applyCommand(*rhinoStopThread,
                                                    *rhinoStopCommand));
        assert(levelTwoProgressRuntime.bossProgress().closing);
        levelTwoProgressRuntime.advanceBossProgress(
            100, progressPlayer, &nearRhino);
        assert(std::abs(
                   levelTwoProgressRuntime.bossProgress().failureDistance -
                   (expectedProgressFailureDistance - 90.0F)) < 0.01F);
        const usm::assets::Vector3 escapedRhino{
            expectedProgressFailureDistance + 1.0F, 0.0F, 500.0F};
        levelTwoProgressRuntime.advanceBossProgress(
            0, progressPlayer, &escapedRhino);
        assert(levelTwoProgressRuntime.bossProgress().failed);
        assert(!levelTwoProgressRuntime.bossProgressVisible());
        const auto [stopProgressThread, stopProgressCommand] =
            findLevelTwoCommand(20051, "StopProgress");
        assert(stopProgressThread != nullptr && stopProgressCommand != nullptr);
        assert(levelTwoProgressRuntime.applyCommand(*stopProgressThread,
                                                    *stopProgressCommand));
        assert(!levelTwoProgressRuntime.bossProgress().visible);
        assert(!levelTwoProgressRuntime.bossProgress().failed);
        assert(levelTwoProgressRuntime.bossProgress().closing);
        usm::game::CinematicThread nonBasicProgressThread =
            *stopProgressThread;
        nonBasicProgressThread.type = 3;
        assert(!levelTwoProgressRuntime.applyCommand(nonBasicProgressThread,
                                                     *stopProgressCommand));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("Unlock", "$SkillID", "0 ultimate")));
        assert(levelCommandRuntime.skillUnlocked(0));
        assert(!levelCommandRuntime.skillUnlocked(1));
        assert(levelCommandRuntime.applyCommand(
            controlCommand("Unlock", "$SkillID", "1 sense")));
        assert(levelCommandRuntime.skillUnlocked(1));
        const usm::game::CinematicCommand transportCommand{
            0, -1, "Transport", {}};
        assert(!levelCommandRuntime.applyCommand(transportCommand));
        assert(!levelCommandRuntime.transportRequested());
        usm::game::CinematicThread transportThread;
        transportThread.type = 3;
        assert(levelCommandRuntime.applyCommand(transportThread,
                                                transportCommand));
        assert(levelCommandRuntime.transportRequested());
        assert(levelCommandRuntime.transport().state ==
               usm::game::TransportState::Closing);
        auto transportCues =
            levelCommandRuntime.consumeTransportSoundCues();
        assert(transportCues.size() == 1);
        assert(transportCues.front() ==
               usm::game::TransportSoundCue::In);
        levelCommandRuntime.advanceTransport(0);
        assert(std::abs(levelCommandRuntime.transport().scale - 50.0F) <
               0.001F);
        levelCommandRuntime.advanceTransport(450);
        levelCommandRuntime.advanceTransport(0);
        assert(levelCommandRuntime.transport().scale > 0.0F);
        assert(levelCommandRuntime.transport().scale < 50.0F);
        levelCommandRuntime.advanceTransport(451);
        assert(levelCommandRuntime.transport().state ==
               usm::game::TransportState::Covered);
        levelCommandRuntime.advanceTransport(99);
        assert(levelCommandRuntime.transport().state ==
               usm::game::TransportState::Covered);
        levelCommandRuntime.advanceTransport(1);
        assert(levelCommandRuntime.transport().state ==
               usm::game::TransportState::Opening);
        transportCues = levelCommandRuntime.consumeTransportSoundCues();
        assert(transportCues.size() == 1);
        assert(transportCues.front() ==
               usm::game::TransportSoundCue::Out);
        levelCommandRuntime.advanceTransport(0);
        assert(levelCommandRuntime.transport().scale > 0.0F);
        assert(levelCommandRuntime.transport().scale < 50.0F);
        levelCommandRuntime.advanceTransport(900);
        assert(levelCommandRuntime.transport().state ==
               usm::game::TransportState::Inactive);
        assert(!levelCommandRuntime.applyCommand(
            controlCommand("Unlock", "$SkillID", "2 invalid")));
        usm::game::CinematicCommand slowMotion;
        slowMotion.name = "SetSlowMotion";
        slowMotion.attributes = {
            {"bool", "Enable", "true"},
            {"int", "Denominator", "5"},
            {"int", "TimeOn", "100"},
            {"int", "TimeOnToEnd", "100"},
            {"bool", "isSFX", "true"},
        };
        assert(levelCommandRuntime.applyCommand(slowMotion));
        auto slowMotionCues =
            levelCommandRuntime.consumeSlowMotionSoundCues();
        assert(slowMotionCues.size() == 1);
        assert(slowMotionCues.front() ==
               usm::game::SlowMotionSoundCue::Enter);
        assert(std::abs(levelCommandRuntime.updateSlowMotion(50.0F) - 10.0F) <
               0.001F);
        assert(std::abs(levelCommandRuntime.updateSlowMotion(50.0F) - 10.0F) <
               0.001F);
        assert(std::abs(levelCommandRuntime.updateSlowMotion(25.0F) - 6.25F) <
               0.001F);
        assert(std::abs(levelCommandRuntime.updateSlowMotion(75.0F) - 75.0F) <
               0.001F);
        assert(std::abs(levelCommandRuntime.updateSlowMotion(16.0F) - 1.0F) <
               0.001F);
        slowMotionCues = levelCommandRuntime.consumeSlowMotionSoundCues();
        assert(slowMotionCues.size() == 1);
        assert(slowMotionCues.front() ==
               usm::game::SlowMotionSoundCue::Exit);
        assert(levelCommandRuntime.applyCommand(slowMotion));
        slowMotion.attributes.front().value = "false";
        assert(levelCommandRuntime.applyCommand(slowMotion));
        slowMotionCues = levelCommandRuntime.consumeSlowMotionSoundCues();
        assert(slowMotionCues.size() == 2);
        assert(slowMotionCues.front() ==
               usm::game::SlowMotionSoundCue::Enter);
        assert(slowMotionCues.back() ==
               usm::game::SlowMotionSoundCue::Exit);
        levelCommandRuntime.setSlowMotion(3.0F, 300.0F, 0.0F, false);
        assert(std::abs(levelCommandRuntime.slowMotionDenominator() - 3.0F) <
               0.001F);
        assert(std::abs(levelCommandRuntime.slowMotionHoldMilliseconds() -
                        300.0F) < 0.001F);
        assert(std::abs(levelCommandRuntime.updateSlowMotion(60.0F) - 20.0F) <
               0.001F);
        assert(levelCommandRuntime.consumeSlowMotionSoundCues().empty());
        levelCommandRuntime.setSlowMotion(3.0F, 1000.0F, 0.0F, true);
        auto forcedSenseCues =
            levelCommandRuntime.consumeSlowMotionSoundCues();
        assert(forcedSenseCues.size() == 1);
        assert(forcedSenseCues.front() ==
               usm::game::SlowMotionSoundCue::Enter);
        levelCommandRuntime.resetSlowMotion();
        assert(levelCommandRuntime.slowMotionDenominator() == 1.0F);
        forcedSenseCues = levelCommandRuntime.consumeSlowMotionSoundCues();
        assert(forcedSenseCues.size() == 1);
        assert(forcedSenseCues.front() ==
               usm::game::SlowMotionSoundCue::Exit);

        usm::game::CinematicCommand shakeCamera;
        shakeCamera.name = "ShakeCamera";
        shakeCamera.attributes = {
            {"float", "MaxOff", "20"},
            {"int", "ShakeFrame", "5"},
            {"float", "XRate", "1"},
            {"float", "YRate", "0.5"},
            {"float", "ZRate", "-0.25"},
        };
        assert(levelCommandRuntime.applyCommand(shakeCamera));
        usm::game::CameraPose unshakenPose;
        unshakenPose.position = {100.0F, 200.0F, 300.0F};
        levelCommandRuntime.advanceCameraShake(49);
        auto shakenPose = levelCommandRuntime.applyCameraShake(unshakenPose);
        assert(shakenPose.position.x == 100.0F);
        levelCommandRuntime.advanceCameraShake(1);
        shakenPose = levelCommandRuntime.applyCameraShake(unshakenPose);
        assert(std::abs(shakenPose.position.x - 80.0F) < 0.001F);
        assert(std::abs(shakenPose.position.y - 190.0F) < 0.001F);
        assert(std::abs(shakenPose.position.z - 305.0F) < 0.001F);
        levelCommandRuntime.advanceCameraShake(50);
        shakenPose = levelCommandRuntime.applyCameraShake(unshakenPose);
        assert(std::abs(shakenPose.position.x - 116.0F) < 0.001F);
        assert(std::abs(shakenPose.position.y - 208.0F) < 0.001F);
        assert(std::abs(shakenPose.position.z - 296.0F) < 0.001F);
        levelCommandRuntime.advanceCameraShake(150);
        shakenPose = levelCommandRuntime.applyCameraShake(unshakenPose);
        assert(std::abs(shakenPose.position.x - 96.0F) < 0.001F);
        levelCommandRuntime.advanceCameraShake(50);
        shakenPose = levelCommandRuntime.applyCameraShake(unshakenPose);
        assert(shakenPose.position.x == 100.0F);
        assert(levelCommandRuntime.applyCommand(
            usm::game::CinematicCommand{0, -1, "StopShakeCamera", {}}));
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
        usm::game::LevelCinematicRuntime playbackCompletionRuntime;
        playbackCompletionRuntime.bind(triggerRuntime, gameplayCamera,
                                       bootstrap.waypoints());
        playbackCompletionRuntime.completeColladaPlayback(false, false);
        assert(!playbackCompletionRuntime.levelEnded());
        assert(!playbackCompletionRuntime.gameEnded());
        playbackCompletionRuntime.completeColladaPlayback(true, false);
        assert(playbackCompletionRuntime.levelEnded());
        assert(playbackCompletionRuntime.goToNextLevel());
        playbackCompletionRuntime.bind(triggerRuntime, gameplayCamera,
                                       bootstrap.waypoints());
        playbackCompletionRuntime.completeColladaPlayback(false, true);
        assert(!playbackCompletionRuntime.levelEnded());
        assert(playbackCompletionRuntime.gameEnded());
        if (std::abs(gameplayCameraPose.target.x - 14688.7354F) >= 0.1F ||
            std::abs(gameplayCameraPose.target.y - -9614.5928F) >= 0.1F ||
            std::abs(gameplayCameraPose.target.z - 128.0306F) >= 0.1F ||
            std::abs(gameplayCameraPose.position.x - 15133.0244F) >= 0.1F ||
            std::abs(gameplayCameraPose.position.y - -10275.4473F) >= 0.1F ||
            std::abs(gameplayCameraPose.position.z - 204.7056F) >= 0.1F) {
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
                                         &playerStateConfigs, {}, {}, {},
                                         nullptr,
                                         &bootstrap.playerHitEffectConfigs()));
        usm::game::GameplayPlayer checkPointPlayer;
        assert(checkPointPlayer.initialize(bootstrap.player(), nullptr,
                                           &playerStateConfigs));
        assert(checkPointPlayer.applyDamage(123.0F));
        checkPointPlayer.addSkillPoints(5);
        checkPointPlayer.addCombo(100.0F, false, 1000);
        checkPointPlayer.updateComboState(3001);
        assert(checkPointPlayer.comboScore() == 101);
        checkPointPlayer.loadCheckPointAt({1.0F, 2.0F, 3.0F},
                                          {0.0F, 1.0F, 0.0F});
        assert(std::abs(checkPointPlayer.health() - 877.0F) < 0.001F);
        assert(checkPointPlayer.skillPoints() == 5);
        assert(checkPointPlayer.comboScore() == 101);
        assert(checkPointPlayer.pendingComboCount() == 0);
        assert(checkPointPlayer.maximumComboCount() == 0);
        assert(checkPointPlayer.completedComboHitCount() == 0);
        assert(checkPointPlayer.activeAnimation() == "idle_stand");
        assert(checkPointPlayer.position().x == 1.0F);
        assert(checkPointPlayer.position().y == 2.0F);
        assert(checkPointPlayer.position().z == 3.0F);
        usm::game::GameplayPlayer comboPlayer;
        assert(comboPlayer.initialize(bootstrap.player(), nullptr,
                                      &playerStateConfigs));
        comboPlayer.addCombo(35.0F, false, 1000);
        assert(comboPlayer.pendingComboCount() == 1);
        assert(comboPlayer.maximumComboCount() == 1);
        comboPlayer.updateComboState(3000);
        assert(comboPlayer.comboScore() == 0);
        comboPlayer.updateComboState(3001);
        assert(comboPlayer.comboScore() == 35);
        assert(comboPlayer.completedComboHitCount() == 1);
        comboPlayer.addCombo(50.0F, false, 4000);
        comboPlayer.addCombo(20.0F, true, 4000);
        assert(comboPlayer.pendingComboCount() == 2);
        assert(comboPlayer.maximumComboCount() == 2);
        comboPlayer.updateComboState(6001);
        assert(comboPlayer.comboScore() == 105);
        assert(comboPlayer.completedComboHitCount() == 3);
        comboPlayer.addCombo(10.0F, false, 7000);
        assert(comboPlayer.applyDamage(1.0F));
        comboPlayer.updateComboState(7000);
        assert(comboPlayer.comboScore() == 115);
        assert(comboPlayer.completedComboHitCount() == 4);
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
        assert(gameplayPlayer.activeStateId() == 74);
        assert(gameplayPlayer.activeStateName() ==
               "k_state_idle_to_punch_right");
        assert(gameplayPlayer.activeAnimation() == "idle_to_punch_right");
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_idle_to_punch_right");
        assert(!gameplayPlayer.requestPunch());
        gameplayPlayer.update({}, gameplayCameraPose, 174);
        assert(!gameplayPlayer.consumeMeleeImpact().has_value());
        assert(gameplayPlayer.hitEffects().empty());
        const auto firstPunchSwoosh =
            gameplayPlayer.consumeAttackSoundTrigger();
        assert(firstPunchSwoosh.has_value());
        assert(firstPunchSwoosh->soundConfigId == 1);
        assert(firstPunchSwoosh->emitterIndex == 0);
        assert(firstPunchSwoosh->stateName ==
               "k_state_idle_to_punch_right");
        gameplayPlayer.update({}, gameplayCameraPose, 1);
        const auto firstPunchImpact = gameplayPlayer.consumeMeleeImpact();
        assert(firstPunchImpact.has_value());
        assert(firstPunchImpact->stateId == 74);
        assert(firstPunchImpact->damage == 35.0F);
        assert(firstPunchImpact->maximumReach == 150.0F);
        assert(firstPunchImpact->minimumAngleDegrees == -40.0F);
        assert(firstPunchImpact->maximumAngleDegrees == 40.0F);
        assert(firstPunchImpact->hitType == 100);
        assert(firstPunchImpact->horizontalForce == 400.0F);
        assert(firstPunchImpact->verticalForce == 0.0F);
        assert(gameplayPlayer.hitEffects().size() == 1);
        assert(gameplayPlayer.hitEffects().front().effectId == 0);
        assert(gameplayPlayer.hitEffects().front().lifetimeMilliseconds ==
               300);
        assert(gameplayPlayer.hitEffects().front().fadeDurationMilliseconds ==
               300);
        assert(gameplayPlayer.hitEffects().front().additiveModulateMaterial);
        assert(!gameplayPlayer.hitEffects().front().followsPlayerBone);
        assert(gameplayPlayer.hitEffects().front().elapsedMilliseconds == 1);
        assert(gameplayPlayer.hitEffects().front()
                   .spawnAnimationMilliseconds == 175);
        assert(!gameplayPlayer.consumeMeleeImpact().has_value());
        assert(gameplayPlayer.punchTransitionReadyAfterImpact());
        assert(!gameplayPlayer.consumeAttackSoundTrigger().has_value());
        gameplayPlayer.update({}, gameplayCameraPose, 100);
        assert(!gameplayPlayer.consumeAttackSoundTrigger().has_value());
        gameplayPlayer.update({}, gameplayCameraPose, 58);
        assert(gameplayPlayer.activeStateId() == 74);
        assert(gameplayPlayer.activeAnimation() == "punch_right_to_idle");
        gameplayPlayer.update({}, gameplayCameraPose, 466);
        assert(gameplayPlayer.activeAnimation() == "idle_stand");

        // Player::UpdateAttacks 0x00353332-0x0035334e consumes a valid
        // Player+0x4d8 request immediately while a linked recovery animation
        // is active.  A late combo press must therefore interrupt
        // punch_right_to_idle instead of adding its remaining 466 ms to the
        // response latency.
        usm::game::GameplayPlayer recoveryCancelPlayer;
        assert(recoveryCancelPlayer.initialize(bootstrap.player(), nullptr,
                                               &playerStateConfigs));
        assert(recoveryCancelPlayer.requestPunch());
        recoveryCancelPlayer.update({}, gameplayCameraPose, 333);
        assert(recoveryCancelPlayer.activeStateId() == 74);
        assert(recoveryCancelPlayer.activeAnimation() ==
               "punch_right_to_idle");
        assert(recoveryCancelPlayer.requestPunch());
        assert(recoveryCancelPlayer.activeStateId() == 75);
        assert(recoveryCancelPlayer.activeAnimation() ==
               "punch_right_to_punch_left");

        // Native PreUpdate advances the animation before UpdateKeyTrigger.
        // State 74's input window starts at runtime frame 3: from frame 2, a
        // 25 ms tick crosses that rounded-frame boundary and must accept the
        // Square/X edge in this tick, not reject it against the stale frame.
        usm::game::GameplayPlayer inputPhasePlayer;
        assert(inputPhasePlayer.initialize(bootstrap.player(), nullptr,
                                           &playerStateConfigs));
        inputPhasePlayer.prepareInputFrame(25);
        assert(inputPhasePlayer.requestPunch());
        inputPhasePlayer.update({}, gameplayCameraPose, 25);
        assert(inputPhasePlayer.activeStateId() == 74);
        assert(inputPhasePlayer.animationTimeMilliseconds() == 0);
        inputPhasePlayer.update({}, gameplayCameraPose, 100);
        assert(!inputPhasePlayer.punchAttackTransitionReady());
        inputPhasePlayer.prepareInputFrame(25);
        assert(inputPhasePlayer.punchAttackTransitionReady());
        assert(inputPhasePlayer.requestPunch());
        inputPhasePlayer.update({}, gameplayCameraPose, 25);
        assert(inputPhasePlayer.activeStateId() == 74);

        // UpdateKeyTrigger (0x0034d0a4) scans every serialized row without
        // breaking. Later qualifying rows replace Player+0x4d8, and the row
        // order differs between idle, jump, and attack states.
        const auto pressed = usm::game::PlayerButtonPhase::Pressed;
        const auto held = usm::game::PlayerButtonPhase::Held;
        const usm::game::PlayerAttackTarget nearPriorityTarget{
            {90.0F, 0.0F, 0.0F}, 40.0F, 8201, false, 150.0F, true};
        const usm::game::PlayerAttackTarget farPriorityTarget{
            {500.0F, 0.0F, 0.0F}, 40.0F, 8202, false, 150.0F, true};
        usm::game::GameplayPlayer idlePriorityPlayer;
        assert(idlePriorityPlayer.initialize(bootstrap.player(), nullptr,
                                             &playerStateConfigs));
        idlePriorityPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                     {1.0F, 0.0F, 0.0F});
        assert(idlePriorityPlayer.preferredInputAction(
                   pressed, pressed, pressed, nearPriorityTarget) ==
               usm::game::PlayerInputAction::Web);
        assert(idlePriorityPlayer.preferredInputAction(
                   pressed, pressed, pressed, farPriorityTarget) ==
               usm::game::PlayerInputAction::Punch);

        usm::game::GameplayPlayer attackPriorityPlayer;
        assert(attackPriorityPlayer.initialize(bootstrap.player(), nullptr,
                                               &playerStateConfigs));
        attackPriorityPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                       {1.0F, 0.0F, 0.0F});
        assert(attackPriorityPlayer.requestPunch(nearPriorityTarget));
        attackPriorityPlayer.update({}, gameplayCameraPose, 150);
        assert(attackPriorityPlayer.preferredInputAction(
                   held, pressed, pressed, nearPriorityTarget) ==
               usm::game::PlayerInputAction::Web);

        usm::game::GameplayPlayer jumpPriorityPlayer;
        assert(jumpPriorityPlayer.initialize(bootstrap.player(), nullptr,
                                             &playerStateConfigs));
        jumpPriorityPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                     {1.0F, 0.0F, 0.0F});
        assert(jumpPriorityPlayer.requestJump());
        jumpPriorityPlayer.update({}, gameplayCameraPose, 100);
        assert(jumpPriorityPlayer.preferredInputAction(
                   pressed, pressed, pressed, nearPriorityTarget) ==
               usm::game::PlayerInputAction::Jump);

        // Sense counter states place target-search predicate 105 after their
        // ordinary punch and web rows. Once the retained target requires a
        // dash, that final punch row must win and queue state 87.
        usm::game::GameplayPlayer senseFarPriorityPlayer;
        assert(senseFarPriorityPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs));
        senseFarPriorityPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                         {1.0F, 0.0F, 0.0F});
        assert(senseFarPriorityPlayer.requestSpiderSense(
            usm::game::PlayerAttackTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F, 8203, false, 150.0F,
                true, true, false, false, std::nullopt, false, 1, true, 0}));
        assert(senseFarPriorityPlayer.activeStateId() == 38);
        senseFarPriorityPlayer.update({}, gameplayCameraPose, 750);
        auto retainedFarSenseTarget = farPriorityTarget;
        retainedFarSenseTarget.objectId = 8203;
        senseFarPriorityPlayer.refreshTrackedAttackTarget(
            retainedFarSenseTarget);
        assert(senseFarPriorityPlayer.requestPunch());
        senseFarPriorityPlayer.update(
            {}, gameplayCameraPose,
            senseFarPriorityPlayer.activeAnimationDurationMilliseconds() -
                senseFarPriorityPlayer.animationTimeMilliseconds());
        assert(senseFarPriorityPlayer.activeStateId() == 87);

        // Ordinary combo predicates write only Player+0x4d8. They retain the
        // original Player+0x594 target and do not re-run either target search
        // or NeedDashToTarget when that victim has been knocked out of the
        // opening punch's direct reach.
        usm::game::GameplayPlayer retainedComboTargetPlayer;
        assert(retainedComboTargetPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs));
        usm::game::PlayerAttackTarget retainedComboTarget{
            {retainedComboTargetPlayer.position().x + 90.0F,
             retainedComboTargetPlayer.position().y,
             retainedComboTargetPlayer.position().z},
            40.0F, 8101, false, 150.0F, true};
        assert(retainedComboTargetPlayer.requestPunch(retainedComboTarget));
        retainedComboTargetPlayer.update({}, gameplayCameraPose, 150);
        const usm::game::PlayerAttackTarget newBestCandidate{
            {retainedComboTargetPlayer.position().x + 80.0F,
             retainedComboTargetPlayer.position().y,
             retainedComboTargetPlayer.position().z},
            40.0F, 8102, false, 150.0F, true};
        retainedComboTarget.position.x =
            retainedComboTargetPlayer.position().x + 500.0F;
        retainedComboTargetPlayer.refreshTrackedAttackTarget(
            retainedComboTarget);
        assert(retainedComboTargetPlayer.requestPunch(newBestCandidate));
        assert(retainedComboTargetPlayer.trackedAttackTargetObjectId() ==
               8101);
        // Player::UpdateAttacks returns immediately after SetNextStateId.
        // Deliberately overshoot the remaining 183 ms of state 74 and prove
        // that the queued state still begins at time zero rather than
        // inheriting the unused portion of this update.
        retainedComboTargetPlayer.update({}, gameplayCameraPose, 300);
        assert(retainedComboTargetPlayer.activeStateId() == 75);
        assert(retainedComboTargetPlayer.animationTimeMilliseconds() == 0);
        assert(retainedComboTargetPlayer.attackTimelineMilliseconds() == 0);
        assert(retainedComboTargetPlayer.trackedAttackTargetObjectId() ==
               8101);

        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_idle_to_punch_right");
        gameplayPlayer.update({}, gameplayCameraPose, 200);
        assert(gameplayPlayer.consumeAttackSoundTrigger().has_value());
        assert(gameplayPlayer.consumeMeleeImpact()->stateId == 74);
        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.activeStateId() == 74);
        gameplayPlayer.update({}, gameplayCameraPose, 133);
        assert(gameplayPlayer.activeStateId() == 75);
        assert(gameplayPlayer.activeAnimation() ==
               "punch_right_to_punch_left");
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_punch_right_to_punch_left");
        gameplayPlayer.update({}, gameplayCameraPose, 150);
        const auto leftPunchImpact = gameplayPlayer.consumeMeleeImpact();
        assert(leftPunchImpact.has_value() && leftPunchImpact->stateId == 75);
        assert(leftPunchImpact->damage == 35.0F);
        assert(std::any_of(
            gameplayPlayer.hitEffects().begin(),
            gameplayPlayer.hitEffects().end(),
            [](const auto& effect) {
                return effect.effectId == 1 &&
                       effect.additiveModulateMaterial;
            }));
        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.activeStateId() == 75);
        gameplayPlayer.update({}, gameplayCameraPose, 84);
        assert(gameplayPlayer.activeStateId() == 79);
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_punch_left_to_kick_right");
        gameplayPlayer.update({}, gameplayCameraPose, 200);
        const auto rightKickImpact = gameplayPlayer.consumeMeleeImpact();
        assert(rightKickImpact.has_value() && rightKickImpact->stateId == 79);
        assert(rightKickImpact->damage == 55.0F);
        assert(std::any_of(
            gameplayPlayer.hitEffects().begin(),
            gameplayPlayer.hitEffects().end(),
            [](const auto& effect) {
                return effect.effectId == 10 &&
                       effect.additiveModulateMaterial;
            }));
        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.activeStateId() == 79);
        gameplayPlayer.update({}, gameplayCameraPose, 100);
        assert(gameplayPlayer.activeStateId() == 90);
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_kick_right_to_fast_kick");
        gameplayPlayer.update({}, gameplayCameraPose, 500);
        for (int impactIndex = 0; impactIndex < 4; ++impactIndex) {
            const auto fastKickImpact = gameplayPlayer.consumeMeleeImpact();
            assert(fastKickImpact.has_value() &&
                   fastKickImpact->stateId == 90);
            assert(fastKickImpact->damage == 21.25F);
            assert(fastKickImpact->horizontalForce ==
                   (impactIndex == 3 ? 100.0F : 0.0F));
        }
        assert(std::any_of(
            gameplayPlayer.hitEffects().begin(),
            gameplayPlayer.hitEffects().end(),
            [](const auto& effect) { return effect.effectId == 5; }));
        // UpdateNormalEffect (0x00348f24) retains only the latest crossed
        // multi-hit frame during a long update. State 90 maps both of its
        // still-live late contacts to effect 5, but must spawn it only once.
        assert(std::count_if(
                   gameplayPlayer.hitEffects().begin(),
                   gameplayPlayer.hitEffects().end(), [](const auto& effect) {
                       return effect.effectId == 5 &&
                              effect.additiveModulateMaterial;
                   }) == 1);
        assert(gameplayPlayer.punchTransitionReadyAfterImpact());
        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.activeStateId() == 90);
        gameplayPlayer.update({}, gameplayCameraPose, 34);
        assert(gameplayPlayer.activeStateId() == 91);
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_kick_right_to_fast_kick_2");
        gameplayPlayer.update({}, gameplayCameraPose, 500);
        for (int impactIndex = 0; impactIndex < 4; ++impactIndex) {
            const auto fastKickImpact = gameplayPlayer.consumeMeleeImpact();
            assert(fastKickImpact.has_value() &&
                   fastKickImpact->stateId == 91);
        }
        assert(gameplayPlayer.requestPunch());
        assert(gameplayPlayer.activeStateId() == 91);
        gameplayPlayer.update({}, gameplayCameraPose, 33);
        assert(gameplayPlayer.activeStateId() == 78);
        assert(gameplayPlayer.activeAnimation() == "kick_left_double_kick");
        assert(gameplayPlayer.consumeEnteredState() ==
               "k_state_kick_left_double_kick");
        gameplayPlayer.update({}, gameplayCameraPose, 500);
        for (int impactIndex = 0; impactIndex < 2; ++impactIndex) {
            const auto doubleKickImpact = gameplayPlayer.consumeMeleeImpact();
            assert(doubleKickImpact.has_value() &&
                   doubleKickImpact->stateId == 78);
            assert(doubleKickImpact->damage == 40.0F);
            assert(doubleKickImpact->horizontalForce ==
                   (impactIndex == 1 ? 800.0F : 0.0F));
            assert(doubleKickImpact->verticalForce ==
                   (impactIndex == 1 ? 600.0F : 0.0F));
        }
        assert(!gameplayPlayer.requestPunch());
        gameplayPlayer.update({}, gameplayCameraPose, 735);
        assert(gameplayPlayer.activeStateId() == 0);
        assert(gameplayPlayer.activeAnimation() == "idle_stand");

        usm::game::GameplayPlayer mixedComboPlayer;
        assert(mixedComboPlayer.initialize(bootstrap.player(), nullptr,
                                           &playerStateConfigs));
        const usm::game::PlayerAttackTarget mixedComboTarget{
            {mixedComboPlayer.position().x + 90.0F,
             mixedComboPlayer.position().y,
             mixedComboPlayer.position().z},
            40.0F, 9238, false, 150.0F, true};
        assert(mixedComboPlayer.requestPunch(mixedComboTarget));
        mixedComboPlayer.update({}, gameplayCameraPose, 150);
        assert(mixedComboPlayer.requestWeb());
        assert(mixedComboPlayer.activeStateId() == 74);
        mixedComboPlayer.update({}, gameplayCameraPose, 183);
        assert(mixedComboPlayer.activeStateId() == 92);
        assert(mixedComboPlayer.activeStateName() ==
               "k_state_kick_to_air_web_bind");
        mixedComboPlayer.update({}, gameplayCameraPose, 1070);
        assert(mixedComboPlayer.activeStateId() == 93);
        assert(mixedComboPlayer.animationTimeMilliseconds() == 0);
        mixedComboPlayer.update({}, gameplayCameraPose, 1070);
        assert(mixedComboPlayer.activeStateId() == 95);
        assert(mixedComboPlayer.animationTimeMilliseconds() == 0);
        mixedComboPlayer.update({}, gameplayCameraPose, 100);
        const auto mixedComboOpeningPunch =
            mixedComboPlayer.consumeMeleeImpact();
        assert(mixedComboOpeningPunch &&
               mixedComboOpeningPunch->stateId == 74);
        const auto mixedComboLaunchKick =
            mixedComboPlayer.consumeMeleeImpact();
        assert(mixedComboLaunchKick && mixedComboLaunchKick->stateId == 92);
        const auto mixedComboKick = mixedComboPlayer.consumeMeleeImpact();
        assert(mixedComboKick && mixedComboKick->stateId == 95);
        assert(mixedComboKick->damage == 80.0F);
        assert(mixedComboKick->hitType == 121);
        assert(mixedComboKick->targetedDelivery);
        assert(mixedComboKick->targetedEnemyObjectId == 9238);

        usm::game::GameplayPlayer launcherPlayer;
        assert(launcherPlayer.initialize(bootstrap.player(), nullptr,
                                        &playerStateConfigs));
        assert(launcherPlayer.requestPunch());
        launcherPlayer.update({}, gameplayCameraPose, 150);
        // CKeyPadCustomer::isKeyDown(key, 2) (0x002f9788) does not satisfy
        // predicate 103 on the two wasKeyPressed samples. A new Cross/A press
        // must not select the held uppercut; it becomes eligible only on the
        // third keypad update while the key remains down.
        assert(!launcherPlayer.requestJump());
        assert(launcherPlayer.requestJump(
            {}, {}, usm::game::PlayerButtonPhase::Held));
        assert(launcherPlayer.activeStateId() == 74);
        launcherPlayer.update({}, gameplayCameraPose, 183);
        assert(launcherPlayer.activeStateId() == 76);
        assert(launcherPlayer.activeStateName() ==
               "k_state_punch_left_to_uppercut_left");
        assert(launcherPlayer.airborne());
        const auto launcherOpeningPunch = launcherPlayer.consumeMeleeImpact();
        assert(launcherOpeningPunch && launcherOpeningPunch->stateId == 74);
        launcherPlayer.update({}, gameplayCameraPose, 200);
        const auto launcherImpact = launcherPlayer.consumeMeleeImpact();
        assert(launcherImpact && launcherImpact->stateId == 76);
        assert(launcherImpact->hitType == 102);
        assert(launcherImpact->horizontalForce == 0.0F);
        assert(launcherImpact->verticalForce == 1000.0F);

        // UpdateKeyTrigger predicate 102 is the A/Cross release edge. It is
        // a distinct state-74 branch from predicate 103's held uppercut.
        usm::game::GameplayPlayer releasedJumpPlayer;
        assert(releasedJumpPlayer.initialize(bootstrap.player(), nullptr,
                                             &playerStateConfigs));
        assert(releasedJumpPlayer.requestPunch());
        releasedJumpPlayer.update({}, gameplayCameraPose, 150);
        assert(releasedJumpPlayer.jumpReleaseAttackTransitionReady());
        assert(releasedJumpPlayer.requestJump(
            {}, {}, usm::game::PlayerButtonPhase::Released));
        assert(releasedJumpPlayer.activeStateId() == 74);
        releasedJumpPlayer.update({}, gameplayCameraPose, 183);
        assert(releasedJumpPlayer.activeStateId() == 97);
        assert(releasedJumpPlayer.activeStateName() ==
               "k_state_ground_kick_to_air");
        const auto releasedOpeningPunch =
            releasedJumpPlayer.consumeMeleeImpact();
        assert(releasedOpeningPunch && releasedOpeningPunch->stateId == 74);
        releasedJumpPlayer.update({}, gameplayCameraPose, 300);
        const auto releasedJumpImpact =
            releasedJumpPlayer.consumeMeleeImpact();
        assert(releasedJumpImpact && releasedJumpImpact->stateId == 97);
        assert(releasedJumpImpact->damage == 60.0F);
        assert(releasedJumpImpact->hitType == 121);
        assert(releasedJumpImpact->horizontalForce == 100.0F);
        assert(releasedJumpImpact->verticalForce == 860.0F);

        usm::game::GameplayPlayer aerialComboPlayer;
        assert(aerialComboPlayer.initialize(bootstrap.player(), nullptr,
                                            &playerStateConfigs));
        usm::game::PlayerAttackTarget flyKickTarget{
            {aerialComboPlayer.position().x + 500.0F,
             aerialComboPlayer.position().y,
             aerialComboPlayer.position().z},
            40.0F, 9239, false, 150.0F};
        assert(aerialComboPlayer.requestJump());
        aerialComboPlayer.update({}, gameplayCameraPose, 100);
        assert(aerialComboPlayer.requestPunch(flyKickTarget));
        assert(aerialComboPlayer.activeStateId() == 81);
        aerialComboPlayer.update({}, gameplayCameraPose, 100);
        const auto firstAirImpact = aerialComboPlayer.consumeMeleeImpact();
        assert(firstAirImpact && firstAirImpact->stateId == 81);
        aerialComboPlayer.update({}, gameplayCameraPose, 50);
        assert(aerialComboPlayer.requestPunch());
        assert(aerialComboPlayer.activeStateId() == 81);
        aerialComboPlayer.update({}, gameplayCameraPose, 385);
        assert(aerialComboPlayer.activeStateId() == 82);
        for (int impactIndex = 0; impactIndex < 3; ++impactIndex) {
            const auto trailingAirImpact =
                aerialComboPlayer.consumeMeleeImpact();
            assert(trailingAirImpact && trailingAirImpact->stateId == 81);
        }
        aerialComboPlayer.update({}, gameplayCameraPose, 150);
        const auto secondAirImpact = aerialComboPlayer.consumeMeleeImpact();
        assert(secondAirImpact && secondAirImpact->stateId == 82);
        assert(aerialComboPlayer.requestPunch());
        assert(aerialComboPlayer.activeStateId() == 82);
        aerialComboPlayer.update({}, gameplayCameraPose, 385);
        assert(aerialComboPlayer.activeStateId() == 83);
        for (int impactIndex = 0; impactIndex < 3; ++impactIndex) {
            const auto trailingAirImpact =
                aerialComboPlayer.consumeMeleeImpact();
            assert(trailingAirImpact && trailingAirImpact->stateId == 82);
        }
        aerialComboPlayer.update({}, gameplayCameraPose, 150);
        flyKickTarget.airborne = true;
        assert(aerialComboPlayer.requestWeb(flyKickTarget));
        assert(aerialComboPlayer.activeStateId() == 83);
        aerialComboPlayer.update({}, gameplayCameraPose, 283);
        assert(aerialComboPlayer.activeStateId() == 84);
        const auto bufferedHeavyImpact =
            aerialComboPlayer.consumeMeleeImpact();
        assert(bufferedHeavyImpact && bufferedHeavyImpact->stateId == 83);
        assert(bufferedHeavyImpact->damage == 85.0F);
        assert(aerialComboPlayer.activeAnimation() ==
               "in_air_fly_kicking_ready");
        aerialComboPlayer.update({}, gameplayCameraPose, 568);
        assert(aerialComboPlayer.activeStateId() == 84);
        assert(aerialComboPlayer.activeAnimation() == "in_air_fly_kicking");
        assert(aerialComboPlayer.animationTimeMilliseconds() == 0);
        assert(aerialComboPlayer.attackTimelineMilliseconds() == 568);
        assert(aerialComboPlayer.trackedAttackTargetObjectId() == 9239);
        flyKickTarget.position.x += 200.0F;
        aerialComboPlayer.refreshTrackedAttackTarget(flyKickTarget);
        const float flyKickStartDistance =
            flyKickTarget.position.x - aerialComboPlayer.position().x;
        aerialComboPlayer.update({}, gameplayCameraPose, 132);
        assert(flyKickTarget.position.x - aerialComboPlayer.position().x <
               flyKickStartDistance - 200.0F);
        const auto flyKickImpact = aerialComboPlayer.consumeMeleeImpact();
        assert(flyKickImpact && flyKickImpact->stateId == 84);
        assert(flyKickImpact->damage == 120.0F);
        assert(flyKickImpact->targetedDelivery);
        assert(flyKickImpact->targetedEnemyObjectId == 9239);
        assert(aerialComboPlayer.attackTimelineMilliseconds() == 700);
        assert(aerialComboPlayer.requestWeb());
        assert(aerialComboPlayer.activeStateId() == 85);
        assert(aerialComboPlayer.activeAnimation() ==
               "in_air_fly_head_butting_ready");
        aerialComboPlayer.update({}, gameplayCameraPose, 632);
        assert(aerialComboPlayer.activeAnimation() ==
               "in_air_fly_head_butting");
        aerialComboPlayer.update({}, gameplayCameraPose, 200);
        assert(aerialComboPlayer.activeAnimation() ==
               "in_air_fly_head_butting_return");
        aerialComboPlayer.update({}, gameplayCameraPose, 318);
        const auto headbuttImpact = aerialComboPlayer.consumeMeleeImpact();
        assert(headbuttImpact && headbuttImpact->stateId == 85);
        assert(headbuttImpact->damage == 120.0F);
        assert(aerialComboPlayer.attackTimelineMilliseconds() == 1150);

        usm::game::GameplayPlayer targetedAirPlayer;
        assert(targetedAirPlayer.initialize(bootstrap.player(), nullptr,
                                           &playerStateConfigs, {}, {}, {},
                                           nullptr,
                                           &bootstrap.playerHitEffectConfigs(),
                                           bootstrap.playerHitEffects()));
        assert(targetedAirPlayer.requestJump());
        targetedAirPlayer.update({}, gameplayCameraPose, 100);
        const auto targetedAirStart = targetedAirPlayer.position();
        const usm::assets::Vector3 targetedAirTarget{
            100.0F, 0.0F, 100.0F};
        assert(targetedAirPlayer.requestPunch(
            usm::game::PlayerAttackTarget{
                targetedAirTarget, 40.0F, 1239, true, 150.0F}));
        assert(targetedAirPlayer.activeStateId() == 86);
        assert(targetedAirPlayer.hitEffects().size() == 1);
        assert(targetedAirPlayer.hitEffects().front().effectId == 26);
        const float targetedAirDx =
            targetedAirTarget.x - targetedAirStart.x;
        const float targetedAirDy =
            targetedAirTarget.y - targetedAirStart.y;
        const float targetedAirDz =
            (targetedAirTarget.z + 150.0F) - targetedAirStart.z;
        const auto targetedAirEffectLifetime =
            static_cast<std::uint32_t>(
                std::sqrt(targetedAirDx * targetedAirDx +
                          targetedAirDy * targetedAirDy +
                          targetedAirDz * targetedAirDz) /
                1400.0F * 1000.0F) +
            600U;
        assert(targetedAirPlayer.hitEffects().front().lifetimeMilliseconds ==
               targetedAirEffectLifetime);
        assert(!targetedAirPlayer.hitEffects().front()
                    .additiveModulateMaterial);

        usm::game::GameplayPlayer sensePlayer;
        assert(sensePlayer.initialize(bootstrap.player(), nullptr,
                                     &playerStateConfigs, {}, {}, {},
                                     nullptr,
                                     &bootstrap.playerHitEffectConfigs(),
                                     bootstrap.playerHitEffects()));
        sensePlayer.restoreAt({0.0F, 0.0F, 0.0F},
                              {1.0F, 0.0F, 0.0F});
        assert(sensePlayer.requestSpiderSense(
            usm::game::PlayerAttackTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F, 1240, false, 150.0F}));
        assert(sensePlayer.activeStateId() == 38);
        assert(!sensePlayer.applyDamage(50.0F));
        sensePlayer.update({}, gameplayCameraPose, 267);
        const auto senseImpact = sensePlayer.consumeMeleeImpact();
        assert(senseImpact && senseImpact->stateId == 38);
        assert(senseImpact->damage == 300.0F);
        assert(senseImpact->ultimateAttack);
        assert(senseImpact->senseAttack);
        assert(std::any_of(
            sensePlayer.hitEffects().begin(), sensePlayer.hitEffects().end(),
            [](const auto& effect) { return effect.effectId == 7; }));

        struct DirectionalSenseCase final {
            usm::assets::Vector3 attackerPosition;
            std::uint16_t stateId;
            std::int32_t effectId;
            std::uint32_t impactMilliseconds;
        };
        constexpr std::array<DirectionalSenseCase, 3> directionalSenseCases{{
            {{-150.0F, 0.0F, 0.0F}, 39, 6, 300},
            {{0.0F, 150.0F, 0.0F}, 40, 8, 200},
            {{0.0F, -150.0F, 0.0F}, 41, 9, 234},
        }};
        for (const auto& senseCase : directionalSenseCases) {
            usm::game::GameplayPlayer directionalSensePlayer;
            assert(directionalSensePlayer.initialize(
                bootstrap.player(), nullptr, &playerStateConfigs,
                {}, {}, {}, nullptr,
                &bootstrap.playerHitEffectConfigs(),
                bootstrap.playerHitEffects()));
            directionalSensePlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                             {1.0F, 0.0F, 0.0F});
            assert(directionalSensePlayer.requestSpiderSense(
                usm::game::PlayerAttackTarget{
                    senseCase.attackerPosition, 40.0F, 1240, false,
                    150.0F}));
            assert(directionalSensePlayer.activeStateId() ==
                   senseCase.stateId);
            directionalSensePlayer.update(
                {}, gameplayCameraPose, senseCase.impactMilliseconds);
            const auto directionalSenseImpact =
                directionalSensePlayer.consumeMeleeImpact();
            assert(directionalSenseImpact);
            assert(directionalSenseImpact->stateId == senseCase.stateId);
            assert(directionalSenseImpact->damage == 300.0F);
            assert(directionalSenseImpact->ultimateAttack);
            assert(directionalSenseImpact->senseAttack);
            assert(std::any_of(
                directionalSensePlayer.hitEffects().begin(),
                directionalSensePlayer.hitEffects().end(),
                [&senseCase](const auto& effect) {
                    return effect.effectId == senseCase.effectId;
                }));
        }

        // DoNormalSenseAction's native tables at 0x004cfd28/0x004cfd38
        // are [38,40,41,39] for close directional counters and
        // [35,34,36,37] for fixed reaction types 2..5. An airborne type-1
        // warning instead uses the perpendicular random pair through the
        // process-wide native generator.
        usm::game::NativeRandomizer senseRandomizer;
        usm::game::GameplayPlayer airSensePlayer;
        assert(airSensePlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs,
            {}, {}, {}, nullptr,
            &bootstrap.playerHitEffectConfigs(),
            bootstrap.playerHitEffects(), &senseRandomizer));
        assert(airSensePlayer.requestJump());
        usm::game::PlayerAttackTarget airSenseTarget{
            {150.0F, 0.0F, 0.0F}, 40.0F, 1242, false, 150.0F};
        airSenseTarget.senseReactionType = 1;
        assert(airSensePlayer.requestSpiderSense(airSenseTarget));
        assert(airSensePlayer.activeStateId() == 36);
        assert(senseRandomizer.state() == 632802407);

        constexpr std::array<std::uint16_t, 4> fixedSenseStates{
            35, 34, 36, 37};
        for (std::size_t index = 0; index < fixedSenseStates.size(); ++index) {
            usm::game::GameplayPlayer fixedSensePlayer;
            assert(fixedSensePlayer.initialize(
                bootstrap.player(), nullptr, &playerStateConfigs,
                {}, {}, {}, nullptr,
                &bootstrap.playerHitEffectConfigs(),
                bootstrap.playerHitEffects()));
            usm::game::PlayerAttackTarget fixedSenseTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F,
                static_cast<std::int32_t>(1250 + index), false, 150.0F};
            fixedSenseTarget.senseReactionType =
                static_cast<std::int32_t>(index + 2);
            assert(fixedSensePlayer.requestSpiderSense(fixedSenseTarget));
            assert(fixedSensePlayer.activeStateId() ==
                   fixedSenseStates[index]);
        }

        usm::game::GameplayPlayer nonCounterableSensePlayer;
        assert(nonCounterableSensePlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs,
            {}, {}, {}, nullptr,
            &bootstrap.playerHitEffectConfigs(),
            bootstrap.playerHitEffects()));
        usm::game::PlayerAttackTarget nonCounterableTarget{
            {150.0F, 0.0F, 0.0F}, 40.0F, 1260, false, 150.0F};
        nonCounterableTarget.senseReactionType = 1;
        nonCounterableTarget.canBeCounterHit = false;
        assert(nonCounterableSensePlayer.requestSpiderSense(
            nonCounterableTarget));
        assert(nonCounterableSensePlayer.activeStateId() == 36);

        usm::game::GameplayPlayer subtypeFiveSensePlayer;
        assert(subtypeFiveSensePlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs,
            {}, {}, {}, nullptr,
            &bootstrap.playerHitEffectConfigs(),
            bootstrap.playerHitEffects()));
        usm::game::PlayerAttackTarget subtypeFiveTarget{
            {150.0F, 0.0F, 0.0F}, 40.0F, 1261, false, 150.0F};
        subtypeFiveTarget.senseReactionType = 4;
        subtypeFiveTarget.enemySubType = 5;
        assert(subtypeFiveSensePlayer.requestSpiderSense(subtypeFiveTarget));
        assert(subtypeFiveSensePlayer.activeStateId() == 35);

        // CheckBlinkStrike (0x00342550) preempts the ordinary quadrant table
        // when CEnemy::IsNearAttackKeyFrame (0x00334dd0) reports its exact
        // -200/+49 ms window. Pin the complete normal-suit state/effect/hit
        // sequence recovered from DoNormalSenseAction and UpdateAttacks.
        usm::game::GameplayPlayer blinkStrikePlayer;
        assert(blinkStrikePlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs,
            {}, {}, {}, nullptr,
            &bootstrap.playerHitEffectConfigs(),
            bootstrap.playerHitEffects()));
        blinkStrikePlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                    {1.0F, 0.0F, 0.0F});
        assert(blinkStrikePlayer.requestSpiderSense(
            usm::game::PlayerAttackTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F, 1241, false, 150.0F,
                true, true, false, false, std::nullopt, true}));
        assert(blinkStrikePlayer.activeStateId() == 42);
        assert(std::ranges::any_of(
            blinkStrikePlayer.hitEffects(), [](const auto& effect) {
                return effect.effectId == 25;
            }));
        // CanEnableSpiderSense (0x00341c98) locks a class-6 response until
        // the inclusive StateBasic+0x30 frame. State 42 stores frame 6.
        blinkStrikePlayer.update({}, gameplayCameraPose, 250);
        assert(!blinkStrikePlayer.requestSpiderSense(
            usm::game::PlayerAttackTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F, 1241, false, 150.0F,
                true, true, false, false, std::nullopt, true}));
        assert(blinkStrikePlayer.lastActionRejectionReason() ==
               "sense_transition_locked");
        blinkStrikePlayer.update({}, gameplayCameraPose, 50);
        assert(blinkStrikePlayer.requestSpiderSense(
            usm::game::PlayerAttackTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F, 1241, false, 150.0F,
                true, true, false, false, std::nullopt, true}));
        blinkStrikePlayer.update({}, gameplayCameraPose, 534);
        assert(std::ranges::any_of(
            blinkStrikePlayer.hitEffects(), [](const auto& effect) {
                return effect.effectId == 24;
            }));
        const auto blinkStrikeChargeStop =
            blinkStrikePlayer.consumeVoxStopEvent();
        assert(blinkStrikeChargeStop);
        assert(blinkStrikeChargeStop->stateId == 42);
        assert(blinkStrikeChargeStop->voxSoundId == 0x53);
        blinkStrikePlayer.update({}, gameplayCameraPose, 66);
        assert(blinkStrikePlayer.activeAnimation() ==
               "super_web_final_to_idle");
        assert(blinkStrikePlayer.animationTimeMilliseconds() == 0);
        blinkStrikePlayer.update({}, gameplayCameraPose, 34);
        const auto blinkStrikeImpact =
            blinkStrikePlayer.consumeMeleeImpact();
        assert(blinkStrikeImpact);
        assert(blinkStrikeImpact->stateId == 42);
        assert(blinkStrikeImpact->damage == 200.0F);
        assert(blinkStrikeImpact->hitType == 509);
        assert(blinkStrikeImpact->senseAttack);
        blinkStrikePlayer.update({}, gameplayCameraPose, 33);
        const auto blinkStrikeSplash =
            blinkStrikePlayer.consumeCombatEffect();
        assert(blinkStrikeSplash);
        assert(blinkStrikeSplash->effectType == "super_web_splash");
        assert(blinkStrikeSplash->voxSoundId == 0x54);

        usm::game::GameplayPlayer ultimatePlayer;
        assert(ultimatePlayer.initialize(bootstrap.player(), nullptr,
                                        &playerStateConfigs, {}, {}, {},
                                        nullptr,
                                        &bootstrap.playerHitEffectConfigs(),
                                        bootstrap.playerHitEffects()));
        assert(ultimatePlayer.requestUltimate());
        assert(ultimatePlayer.activeStateId() == 107);
        assert(!ultimatePlayer.requestSpiderSense(
            usm::game::PlayerAttackTarget{
                {150.0F, 0.0F, 0.0F}, 40.0F, 1241, false, 150.0F}));
        assert(ultimatePlayer.lastActionRejectionReason() ==
               "sense_transition_locked");
        const float ultimateStartingHealth = ultimatePlayer.health();
        assert(!ultimatePlayer.applyDamage(25.0F, 0, 0, 100));
        assert(ultimatePlayer.health() == ultimateStartingHealth);
        assert(ultimatePlayer.hitEffects().size() == 1);
        assert(ultimatePlayer.hitEffects().front().effectId == 24);
        const auto* ultimatePrepare =
            playerStateConfigs.findState("k_state_ultimate_prepare");
        const auto* ultimateWheel =
            playerStateConfigs.findState("k_state_ultimate_wheel");
        assert(ultimatePrepare && ultimateWheel);
        const auto& ultimatePreparePrimary =
            bootstrap.player().animationBank
                .clips()[ultimatePrepare->primaryAnimationId];
        assert(ultimatePlayer.activeAnimationDurationMilliseconds() ==
               ultimatePreparePrimary.durationMilliseconds());
        const auto& ultimatePrepareSecondary =
            bootstrap.player().animationBank
                .clips()[ultimatePrepare->animationIds.front()];
        ultimatePlayer.update({}, gameplayCameraPose,
                              ultimatePreparePrimary.durationMilliseconds());
        assert(ultimatePlayer.activeStateId() == 107);
        assert(ultimatePlayer.activeAnimation() ==
               ultimatePrepareSecondary.name);
        ultimatePlayer.update({}, gameplayCameraPose,
                              ultimatePrepareSecondary.durationMilliseconds());
        assert(ultimatePlayer.activeStateId() == 108);
        assert(std::count_if(
                   ultimatePlayer.hitEffects().begin(),
                   ultimatePlayer.hitEffects().end(),
                   [](const auto& effect) { return effect.effectId == 22; }) ==
               4);
        assert(std::all_of(
            ultimatePlayer.hitEffects().begin(),
            ultimatePlayer.hitEffects().end(), [](const auto& effect) {
                return effect.effectId != 22 ||
                       effect.additiveModulateMaterial;
            }));
        assert(std::count_if(
                   ultimatePlayer.hitEffects().begin(),
                   ultimatePlayer.hitEffects().end(),
                   [](const auto& effect) { return effect.effectId == 23; }) ==
               1);
        const auto ultimatePrepareImpact =
            ultimatePlayer.consumeMeleeImpact();
        assert(ultimatePrepareImpact && ultimatePrepareImpact->stateId == 107);
        assert(ultimatePrepareImpact->damage == 0.0F);
        ultimatePlayer.update({}, gameplayCameraPose, 1);
        const auto ultimateWheelImpact =
            ultimatePlayer.consumeMeleeImpact();
        assert(ultimateWheelImpact && ultimateWheelImpact->stateId == 108);
        assert(ultimateWheelImpact->radialAttack);
        assert(ultimateWheelImpact->ultimateAttack);
        ultimatePlayer.update({}, gameplayCameraPose, 166);
        // The state-108 entry effect was already aged by the complete native
        // EffectManager tick in which it spawned. It survives that render at
        // zero lifetime, then is reclaimed at the start of this next tick.
        assert(std::count_if(
                   ultimatePlayer.hitEffects().begin(),
                   ultimatePlayer.hitEffects().end(),
                   [](const auto& effect) { return effect.effectId == 23; }) ==
               0);
        // State 108 uses a 167 ms start clip followed by repeated 201 ms
        // circle links until its independent 1200 ms phase expires. Native
        // SwitchToNextLinkAnim returns for the tick at every boundary.
        for (int circle = 0; circle < 5; ++circle) {
            ultimatePlayer.update({}, gameplayCameraPose, 201);
            assert(ultimatePlayer.activeStateId() == 108);
            assert(ultimatePlayer.animationTimeMilliseconds() == 0);
        }
        ultimatePlayer.update({}, gameplayCameraPose, 28);
        assert(ultimatePlayer.activeStateId() == 109);
        assert(ultimatePlayer.hitEffects().size() == 1);
        assert(ultimatePlayer.hitEffects().front().effectId == 25);
        assert(ultimatePlayer.hitEffects().front().followsPlayerBone);
        assert(!ultimatePlayer.hitEffects().front().fadeWithLifetime);
        assert(ultimatePlayer.hitEffects().front().lifetimeMilliseconds ==
               566);
        assert(ultimatePlayer.hitEffects().front().fadeDurationMilliseconds ==
               0);
        ultimatePlayer.update({}, gameplayCameraPose,
                              500U);
        const auto animatedOutEffect = std::find_if(
            ultimatePlayer.hitEffects().begin(),
            ultimatePlayer.hitEffects().end(),
            [](const auto& effect) { return effect.effectId == 24; });
        assert(animatedOutEffect != ultimatePlayer.hitEffects().end());
        assert(animatedOutEffect->lifetimeMilliseconds ==
               bootstrap.playerHitEffects()[24].animation.clips()[1]
                   .durationMilliseconds());
        assert(animatedOutEffect->fadeDurationMilliseconds == 800);
        const auto ultimateChargeStop = ultimatePlayer.consumeVoxStopEvent();
        assert(ultimateChargeStop);
        assert(ultimateChargeStop->stateId == 109);
        assert(ultimateChargeStop->voxSoundId == 0x53);
        const auto& ultimateExplode =
            *playerStateConfigs.findState("k_state_ultimate_explode");
        const auto& ultimateExplodePrimary =
            bootstrap.player().animationBank
                .clips()[ultimateExplode.primaryAnimationId];
        ultimatePlayer.update(
            {}, gameplayCameraPose,
            ultimateExplodePrimary.durationMilliseconds() -
                500U);
        assert(ultimatePlayer.activeAnimation() ==
               bootstrap.player().animationBank
                   .clips()[ultimateExplode.animationIds.front()]
                   .name);
        ultimatePlayer.update({}, gameplayCameraPose,
                              651U);
        const auto ultimateSplash = ultimatePlayer.consumeCombatEffect();
        assert(ultimateSplash.has_value());
        assert(ultimateSplash->effectType == "super_web_splash");
        assert(ultimateSplash->voxSoundId == 0x54);

        usm::game::GameplayPlayer farAttackPlayer;
        assert(farAttackPlayer.initialize(bootstrap.player(), nullptr,
                                          &playerStateConfigs));
        farAttackPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                  {1.0F, 0.0F, 0.0F});
        assert(farAttackPlayer.requestPunch(
            usm::game::PlayerAttackTarget{{600.0F, 0.0F, 0.0F}, 50.0F}));
        assert(farAttackPlayer.activeStateId() == 87);
        assert(farAttackPlayer.activeAnimation() == "idle_to_far_attack");
        farAttackPlayer.update({}, gameplayCameraPose, 250);
        assert(farAttackPlayer.position().x > 100.0F);
        assert(std::abs(farAttackPlayer.position().y) < 0.001F);
        assert(std::abs(farAttackPlayer.renderPosition().x -
                        farAttackPlayer.position().x) < 50.0F);
        assert(std::abs(farAttackPlayer.renderPosition().y -
                        farAttackPlayer.position().y) < 50.0F);
        assert(std::abs(farAttackPlayer.facing().x - 1.0F) < 0.001F);
        farAttackPlayer.update({}, gameplayCameraPose, 250);
        assert(farAttackPlayer.activeStateId() == 87);
        assert(farAttackPlayer.activeAnimation() == "far_attack_to_idle");
        farAttackPlayer.update({}, gameplayCameraPose, 500);
        assert(farAttackPlayer.activeStateId() == 0);
        assert(std::abs(farAttackPlayer.position().x - 496.15036F) < 1.0F);
        assert(std::abs(farAttackPlayer.worldTransform()[12] -
                        farAttackPlayer.position().x) < 0.001F);

        usm::game::GameplayPlayer highFarAttackPlayer;
        assert(highFarAttackPlayer.initialize(bootstrap.player(), nullptr,
                                              &playerStateConfigs));
        highFarAttackPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                      {1.0F, 0.0F, 0.0F});
        // CheckHeightToTargetClose (0x003402e4) compares root Z against the
        // player's 50 cm Unit radius.  A target one centimetre above that
        // boundary remains selected and faced, but may not substitute the
        // ground-only state-87 lunge.
        assert(highFarAttackPlayer.requestPunch(
            usm::game::PlayerAttackTarget{{600.0F, 0.0F, 51.0F}, 50.0F}));
        assert(highFarAttackPlayer.activeStateId() == 74);
        assert(highFarAttackPlayer.activeAnimation() ==
               "idle_to_punch_right");

        usm::game::GameplayPlayer boundaryFarAttackPlayer;
        assert(boundaryFarAttackPlayer.initialize(bootstrap.player(), nullptr,
                                                  &playerStateConfigs));
        boundaryFarAttackPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                          {1.0F, 0.0F, 0.0F});
        // The native comparison is inclusive at exactly 50 cm.
        assert(boundaryFarAttackPlayer.requestPunch(
            usm::game::PlayerAttackTarget{{600.0F, 0.0F, 50.0F}, 50.0F}));
        assert(boundaryFarAttackPlayer.activeStateId() == 87);

        usm::game::GameplayPlayer farAttackComboPlayer;
        assert(farAttackComboPlayer.initialize(bootstrap.player(), nullptr,
                                               &playerStateConfigs));
        farAttackComboPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                       {1.0F, 0.0F, 0.0F});
        const usm::game::PlayerAttackTarget farComboTarget{
            {800.0F, 0.0F, 0.0F}, 100.0F};
        assert(farAttackComboPlayer.requestPunch(farComboTarget));
        farAttackComboPlayer.update({}, gameplayCameraPose, 450);
        assert(farAttackComboPlayer.activeStateId() == 87);
        const auto farComboLungeImpact =
            farAttackComboPlayer.consumeMeleeImpact();
        assert(farComboLungeImpact.has_value());
        assert(farComboLungeImpact->stateId == 87);
        const float farAttackPositionBeforeCombo =
            farAttackComboPlayer.position().x;
        assert(farAttackPositionBeforeCombo > 500.0F);
        assert(farAttackComboPlayer.requestPunch(farComboTarget));
        assert(farAttackComboPlayer.activeStateId() == 87);
        farAttackComboPlayer.update({}, gameplayCameraPose, 50);
        assert(farAttackComboPlayer.activeStateId() == 88);
        farAttackComboPlayer.update({}, gameplayCameraPose, 100);
        const auto farComboFollowThroughImpact =
            farAttackComboPlayer.consumeMeleeImpact();
        assert(farComboFollowThroughImpact.has_value());
        assert(farComboFollowThroughImpact->stateId == 88);
        assert(farAttackComboPlayer.position().x >=
               farAttackPositionBeforeCombo - 0.001F);

        usm::game::GameplayPlayer targetedAttackPlayer;
        assert(targetedAttackPlayer.initialize(bootstrap.player(), nullptr,
                                               &playerStateConfigs));
        targetedAttackPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                       {1.0F, 0.0F, 0.0F});
        assert(targetedAttackPlayer.requestPunch(
            usm::game::PlayerAttackTarget{{0.0F, 100.0F, 0.0F}, 50.0F}));
        assert(targetedAttackPlayer.activeStateId() == 74);
        assert(std::abs(targetedAttackPlayer.facing().x) < 0.001F);
        assert(std::abs(targetedAttackPlayer.facing().y - 1.0F) < 0.001F);

        usm::game::GameplayPlayer groundWebPlayer;
        assert(groundWebPlayer.initialize(bootstrap.player(), nullptr,
                                          &playerStateConfigs));
        groundWebPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                  {1.0F, 0.0F, 0.0F});
        const usm::game::PlayerAttackTarget stationaryWebTarget{
            {600.0F, 0.0F, 0.0F}, 50.0F, 1234, false, 180.0F};
        assert(groundWebPlayer.requestWeb(stationaryWebTarget));
        assert(groundWebPlayer.activeStateId() == 58);
        assert(groundWebPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[129].name);
        const auto webProjectileLaunch =
            groundWebPlayer.consumeWebPelletLaunch();
        assert(webProjectileLaunch.has_value());
        assert(webProjectileLaunch->stateId == 58);
        assert(webProjectileLaunch->damage == 20.0F);
        assert(webProjectileLaunch->targetedEnemyObjectId == 1234);
        assert(std::abs(webProjectileLaunch->targetPosition.x - 600.0F) <
               0.001F);
        assert(std::abs(webProjectileLaunch->targetPosition.z - 90.0F) <
               0.001F);
        assert(!groundWebPlayer.consumeMeleeImpact().has_value());

        usm::game::GameplayPlayer heldGroundWebPlayer;
        assert(heldGroundWebPlayer.initialize(bootstrap.player(), nullptr,
                                              &playerStateConfigs));
        heldGroundWebPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                      {1.0F, 0.0F, 0.0F});
        assert(heldGroundWebPlayer.requestWeb(stationaryWebTarget));
        heldGroundWebPlayer.update({}, gameplayCameraPose, 150);
        assert(heldGroundWebPlayer.webHeldAttackTransitionReady());
        assert(heldGroundWebPlayer.requestWeb(
            stationaryWebTarget, {}, usm::game::PlayerButtonPhase::Held));
        heldGroundWebPlayer.update({}, gameplayCameraPose, 50);
        assert(heldGroundWebPlayer.activeStateId() == 60);
        assert(heldGroundWebPlayer.activeStateName() ==
               "k_state_attack_web_bind");
        assert(heldGroundWebPlayer.requestWeb(stationaryWebTarget));
        heldGroundWebPlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[131]
                .durationMilliseconds());
        assert(heldGroundWebPlayer.activeStateId() == 65);
        assert(!heldGroundWebPlayer.consumeMeleeImpact().has_value());
        heldGroundWebPlayer.update({}, gameplayCameraPose, 150);
        assert(heldGroundWebPlayer.requestWeb(stationaryWebTarget));
        heldGroundWebPlayer.update({}, gameplayCameraPose, 524);
        assert(!heldGroundWebPlayer.consumeMeleeImpact().has_value());
        heldGroundWebPlayer.update({}, gameplayCameraPose, 1);
        const auto backwardThrowImpact =
            heldGroundWebPlayer.consumeMeleeImpact();
        assert(backwardThrowImpact.has_value());
        assert(backwardThrowImpact->stateId == 65);
        assert(backwardThrowImpact->damage == 100.0F);
        assert(backwardThrowImpact->hitType == 130);
        assert(backwardThrowImpact->targetedEnemyObjectId == 1234);
        heldGroundWebPlayer.update({}, gameplayCameraPose, 192);
        assert(heldGroundWebPlayer.activeStateId() == 66);
        assert(!heldGroundWebPlayer.consumeMeleeImpact().has_value());
        heldGroundWebPlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[239]
                .durationMilliseconds());
        assert(heldGroundWebPlayer.activeStateId() == 67);
        assert(!heldGroundWebPlayer.consumeMeleeImpact().has_value());
        heldGroundWebPlayer.update({}, gameplayCameraPose, 633);
        assert(!heldGroundWebPlayer.consumeMeleeImpact().has_value());
        heldGroundWebPlayer.update({}, gameplayCameraPose, 1);
        const auto fullTurnThrowImpact =
            heldGroundWebPlayer.consumeMeleeImpact();
        assert(fullTurnThrowImpact.has_value());
        assert(fullTurnThrowImpact->stateId == 67);
        assert(fullTurnThrowImpact->damage == 220.0F);
        assert(fullTurnThrowImpact->hitType == 129);
        assert(fullTurnThrowImpact->targetedEnemyObjectId == 1234);

        usm::game::LevelEnemyRuntime webPelletRuntime;
        assert(webPelletRuntime.initialize(bootstrap));
        assert(webPelletRuntime.setDiagnosticAiEnabled(394, true));
        const auto* webPelletEnemy = webPelletRuntime.find(394);
        assert(webPelletEnemy != nullptr);
        const float webPelletHealthBefore = webPelletEnemy->health;
        const usm::assets::Vector3 webPelletTarget{
            webPelletEnemy->position.x, webPelletEnemy->position.y,
            webPelletEnemy->position.z +
                webPelletEnemy->collisionHeight * 0.5F};
        const usm::assets::Vector3 webPelletOrigin{
            webPelletTarget.x - 600.0F, webPelletTarget.y,
            webPelletTarget.z};
        assert(webPelletRuntime.launchPlayerWebPellet(
            webPelletOrigin, webPelletTarget, 394, 20.0F));
        auto webPelletEvents =
            webPelletRuntime.consumePlayerWebPelletEvents();
        assert(webPelletEvents.size() == 1);
        assert(webPelletEvents.front().kind ==
               usm::game::PlayerWebPelletEventKind::Spawned);
        webPelletRuntime.updateGameplay(400, webPelletOrigin);
        webPelletEvents = webPelletRuntime.consumePlayerWebPelletEvents();
        const auto webPelletContact = std::find_if(
            webPelletEvents.begin(), webPelletEvents.end(),
            [](const usm::game::PlayerWebPelletEvent& event) {
                return event.kind ==
                       usm::game::PlayerWebPelletEventKind::EnemyContact;
            });
        assert(webPelletContact != webPelletEvents.end());
        assert(webPelletContact->hitEnemyObjectId == 394);
        assert(std::abs(webPelletContact->actualDamage - 20.0F) < 0.01F);
        assert(std::abs(webPelletRuntime.find(394)->health -
                        (webPelletHealthBefore - 20.0F)) < 0.01F);
        assert(webPelletRuntime.playerWebPellets().empty());
        groundWebPlayer.update({}, gameplayCameraPose, 150);
        assert(groundWebPlayer.requestWeb(stationaryWebTarget));
        assert(groundWebPlayer.activeStateId() == 58);
        groundWebPlayer.update({}, gameplayCameraPose, 50);
        assert(groundWebPlayer.activeStateId() == 60);
        assert(groundWebPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[131].name);
        const auto& webBindClip =
            bootstrap.player().animationBank.clips()[131];
        groundWebPlayer.update({}, gameplayCameraPose,
                               webBindClip.durationMilliseconds());
        assert(groundWebPlayer.activeStateId() == 60);
        assert(groundWebPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[135].name);
        groundWebPlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[135]
                .durationMilliseconds());
        assert(groundWebPlayer.activeStateId() == 62);
        const auto webDragImpact = groundWebPlayer.consumeMeleeImpact();
        assert(webDragImpact.has_value());
        assert(webDragImpact->stateId == 62);
        // SetNextStateId enters motion 0x7e before replacing Player+0x4a8,
        // so the native direct hit copies state 60's zero damage. State 62's
        // serialized 20 is not delivered as a second pellet hit.
        assert(webDragImpact->damage == 0.0F);
        assert(webDragImpact->targetedEnemyObjectId == 1234);
        assert(webDragImpact->targetedDelivery);
        assert(!groundWebPlayer.consumeMeleeImpact().has_value());
        assert(groundWebPlayer.webLineActive());
        assert(groundWebPlayer.webLineTargetObjectId() == 1234);

        usm::game::GameplayPlayer movingGroundWebPlayer;
        assert(movingGroundWebPlayer.initialize(bootstrap.player(), nullptr,
                                                &playerStateConfigs));
        movingGroundWebPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                        {1.0F, 0.0F, 0.0F});
        const usm::game::PlayerAttackTarget movingWebTarget{
            {600.0F, 0.0F, 100.0F}, 50.0F, 1235, true, 180.0F};
        assert(movingGroundWebPlayer.requestWeb(movingWebTarget));
        assert(movingGroundWebPlayer.activeStateId() == 84);
        assert(movingGroundWebPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[87].name);

        usm::game::GameplayPlayer highGroundWebPlayer;
        assert(highGroundWebPlayer.initialize(bootstrap.player(), nullptr,
                                              &playerStateConfigs));
        highGroundWebPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                      {1.0F, 0.0F, 0.0F});
        const usm::game::PlayerAttackTarget highMovingWebTarget{
            {600.0F, 0.0F, 200.0F}, 50.0F, 1236, true, 180.0F};
        assert(highGroundWebPlayer.requestWeb(highMovingWebTarget));
        assert(highGroundWebPlayer.activeStateId() == 63);
        assert(highGroundWebPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[235].name);
        highGroundWebPlayer.update({}, gameplayCameraPose, 174);
        assert(!highGroundWebPlayer.consumeMeleeImpact().has_value());
        highGroundWebPlayer.update({}, gameplayCameraPose, 1);
        const auto dragDownAttach =
            highGroundWebPlayer.consumeMeleeImpact();
        assert(dragDownAttach.has_value());
        assert(dragDownAttach->stateId == 63);
        assert(dragDownAttach->damage == 0.0F);
        assert(dragDownAttach->hitType == 127);
        const auto& dragDownClip =
            bootstrap.player().animationBank.clips()[235];
        highGroundWebPlayer.update(
            {}, gameplayCameraPose,
            dragDownClip.durationMilliseconds() - 175U);
        const auto dragDownFinish =
            highGroundWebPlayer.consumeMeleeImpact();
        assert(dragDownFinish.has_value());
        assert(dragDownFinish->stateId == 63);
        assert(dragDownFinish->damage == 70.0F);
        assert(dragDownFinish->hitType == 100);
        assert(dragDownFinish->targetedEnemyObjectId == 1236);

        // State 86/motion 0x6c is a retained airborne-target dash, not an
        // in-place kick. SetNextStateId aims at Bip01_Head at 1400 cm/s;
        // UpdateAttackParam retries its contact test after frame 2 until the
        // accepted-contact latch is set.
        usm::game::GameplayPlayer diagonalKickPlayer;
        assert(diagonalKickPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs, {}, {}, {},
            nullptr, &bootstrap.playerHitEffectConfigs(),
            bootstrap.playerHitEffects()));
        diagonalKickPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                     {1.0F, 0.0F, 0.0F});
        assert(diagonalKickPlayer.requestJump());
        diagonalKickPlayer.update({}, gameplayCameraPose, 150);
        const float diagonalKickStartX = diagonalKickPlayer.position().x;
        const usm::game::PlayerAttackTarget diagonalKickTarget{
            {500.0F, 0.0F, 200.0F}, 40.0F, 12360, true, 150.0F,
            true, true, false, false,
            usm::assets::Vector3{500.0F, 0.0F, 330.0F}};
        assert(diagonalKickPlayer.requestPunch(diagonalKickTarget));
        assert(diagonalKickPlayer.activeStateId() == 86);
        diagonalKickPlayer.update({}, gameplayCameraPose, 50);
        assert(diagonalKickPlayer.position().x > diagonalKickStartX);
        const auto diagonalKickFirstProbe =
            diagonalKickPlayer.consumeMeleeImpact();
        assert(diagonalKickFirstProbe.has_value());
        assert(diagonalKickFirstProbe->stateId == 86);
        assert(diagonalKickFirstProbe->damage == 50.0F);
        diagonalKickPlayer.update({}, gameplayCameraPose, 50);
        const auto diagonalKickRetry =
            diagonalKickPlayer.consumeMeleeImpact();
        assert(diagonalKickRetry.has_value());
        assert(diagonalKickRetry->stateId == 86);
        diagonalKickPlayer.notifyMeleeImpactAccepted(86);
        diagonalKickPlayer.update({}, gameplayCameraPose, 50);
        assert(!diagonalKickPlayer.consumeMeleeImpact().has_value());

        usm::game::GameplayPlayer airWebTiePlayer;
        assert(airWebTiePlayer.initialize(bootstrap.player(), nullptr,
                                          &playerStateConfigs));
        airWebTiePlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                  {1.0F, 0.0F, 0.0F});
        assert(airWebTiePlayer.requestJump());
        airWebTiePlayer.update({}, gameplayCameraPose, 150);
        const usm::game::PlayerAttackTarget tieTarget{
            {500.0F, 0.0F, 0.0F}, 40.0F, 1237, true, 150.0F, true, true};
        assert(airWebTiePlayer.requestWeb(tieTarget));
        assert(airWebTiePlayer.activeStateId() == 101);
        assert(airWebTiePlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[93].name);
        assert(airWebTiePlayer.webLineActive());
        assert(airWebTiePlayer.webLineTargetObjectId() == 1237);
        airWebTiePlayer.update({}, gameplayCameraPose, 266);
        assert(!airWebTiePlayer.consumeMeleeImpact().has_value());
        airWebTiePlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[93]
                    .durationMilliseconds() -
                266U);
        assert(airWebTiePlayer.activeStateId() == 102);
        const auto airWebGrabImpact = airWebTiePlayer.consumeMeleeImpact();
        assert(airWebGrabImpact.has_value());
        assert(airWebGrabImpact->stateId == 102);
        assert(airWebGrabImpact->damage == 0.0F);
        assert(airWebGrabImpact->hitType == 115);
        assert(airWebGrabImpact->targetedEnemyObjectId == 1237);
        assert(airWebGrabImpact->targetedDelivery);
        assert(airWebGrabImpact->webAttack);
        airWebTiePlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[94]
                .durationMilliseconds());
        assert(airWebTiePlayer.activeStateId() == 14);
        assert(airWebTiePlayer.airborne());

        usm::game::GameplayPlayer airWebDragPlayer;
        assert(airWebDragPlayer.initialize(bootstrap.player(), nullptr,
                                           &playerStateConfigs));
        airWebDragPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                   {1.0F, 0.0F, 0.0F});
        assert(airWebDragPlayer.requestJump());
        airWebDragPlayer.update({}, gameplayCameraPose, 150);
        const usm::game::PlayerAttackTarget dragTarget{
            {500.0F, 0.0F, 0.0F}, 60.0F, 1238, true, 180.0F, false, true};
        assert(airWebDragPlayer.requestWeb(dragTarget));
        assert(airWebDragPlayer.activeStateId() == 115);
        airWebDragPlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[5]
                    .durationMilliseconds() +
                1U);
        assert(airWebDragPlayer.activeStateId() == 116);
        assert(airWebDragPlayer.animationTimeMilliseconds() == 0);
        airWebDragPlayer.update({}, gameplayCameraPose, 1);
        const auto whirlwindImpact = airWebDragPlayer.consumeMeleeImpact();
        assert(whirlwindImpact.has_value());
        assert(whirlwindImpact->stateId == 116);
        assert(whirlwindImpact->damage == 15.0F);
        assert(!whirlwindImpact->targetedDelivery);
        assert(whirlwindImpact->webAttack);

        // GetAirWebSpecialState (0x00343de4) prioritizes CTargetHelper's
        // nearest non-airborne list over the general tie/drag capability
        // path. Within 200 cm it traverses 99 -> 100 -> 104, including the
        // live FX_LL kick-down trail whose velocity is captured when
        // CAnimObjEffect is created.
        usm::game::GameplayPlayer airKnockdownPlayer;
        assert(airKnockdownPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs, {}, {}, {},
            nullptr, &bootstrap.playerHitEffectConfigs(),
            bootstrap.playerHitEffects()));
        airKnockdownPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                     {1.0F, 0.0F, 0.0F});
        assert(airKnockdownPlayer.requestJump());
        airKnockdownPlayer.update({}, gameplayCameraPose, 150);
        const usm::game::PlayerAttackTarget closeGroundTarget{
            {100.0F, 0.0F, airKnockdownPlayer.position().z},
            40.0F, 1239, false, 180.0F, true, true};
        assert(airKnockdownPlayer.requestWeb(closeGroundTarget));
        assert(airKnockdownPlayer.activeStateId() == 99);
        const auto& airKnockdownPrimary =
            bootstrap.player().animationBank.clips()[91];
        airKnockdownPlayer.update({}, gameplayCameraPose,
                                  airKnockdownPrimary.durationMilliseconds());
        assert(airKnockdownPlayer.activeStateId() == 99);
        assert(airKnockdownPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[92].name);
        const auto approachStart = airKnockdownPlayer.position();
        airKnockdownPlayer.update({}, gameplayCameraPose, 25);
        const auto approachPosition = airKnockdownPlayer.position();
        assert(std::abs(closeGroundTarget.position.x - approachPosition.x) <
               std::abs(closeGroundTarget.position.x - approachStart.x));
        assert(approachPosition.z > approachStart.z);
        for (std::uint32_t elapsed = 0;
             elapsed < 2000 && airKnockdownPlayer.activeStateId() == 99;
             elapsed += 25) {
            airKnockdownPlayer.update({}, gameplayCameraPose, 25);
        }
        assert(airKnockdownPlayer.activeStateId() == 100);
        const auto tiedLieNotify =
            airKnockdownPlayer.consumeEnemyNotifyEvent();
        assert(tiedLieNotify.has_value());
        assert(tiedLieNotify->stateId == 100);
        assert(tiedLieNotify->targetedEnemyObjectId == 1239);
        assert(tiedLieNotify->hitType == 0x71);
        assert(tiedLieNotify->behaviorMessage == 0x6a);
        assert(tiedLieNotify->behaviorState == 0x24);
        assert(airKnockdownPlayer.webLineCount() == 2);
        const auto rightWebAttach =
            airKnockdownPlayer.webLineAttachPosition(0);
        const auto leftWebAttach =
            airKnockdownPlayer.webLineAttachPosition(1);
        assert(std::isfinite(rightWebAttach.x) &&
               std::isfinite(leftWebAttach.x));
        assert(std::abs(rightWebAttach.x - leftWebAttach.x) > 0.001F ||
               std::abs(rightWebAttach.y - leftWebAttach.y) > 0.001F ||
               std::abs(rightWebAttach.z - leftWebAttach.z) > 0.001F);
        for (std::uint32_t elapsed = 0;
             elapsed < 2000 && airKnockdownPlayer.activeStateId() == 100;
             elapsed += 25) {
            airKnockdownPlayer.update({}, gameplayCameraPose, 25);
        }
        assert(airKnockdownPlayer.activeStateId() == 104);
        const auto kickNotify =
            airKnockdownPlayer.consumeEnemyNotifyEvent();
        assert(kickNotify.has_value());
        assert(kickNotify->stateId == 104);
        assert(kickNotify->targetedEnemyObjectId == 1239);
        assert(kickNotify->hitType == 0xa0);
        assert(kickNotify->behaviorMessage == -1);
        assert(kickNotify->behaviorState == -1);
        const float kickPursuitStartZ = airKnockdownPlayer.position().z;
        airKnockdownPlayer.update({}, gameplayCameraPose, 25);
        assert(airKnockdownPlayer.position().z < kickPursuitStartZ);
        assert(std::abs(airKnockdownPlayer.attackRootTranslation().x) <
               0.001F);
        assert(std::abs(airKnockdownPlayer.attackRootTranslation().y) <
               0.001F);
        assert(std::abs(airKnockdownPlayer.attackRootTranslation().z) <
               0.001F);
        for (std::uint32_t elapsed = 0;
             elapsed < 300 && std::ranges::none_of(
                 airKnockdownPlayer.hitEffects(), [](const auto& effect) {
                     return effect.effectId == 19;
                 });
             elapsed += 25) {
            airKnockdownPlayer.update({}, gameplayCameraPose, 25);
        }
        const auto liveKickDownEffect = std::ranges::find_if(
            airKnockdownPlayer.hitEffects(),
            [](const auto& effect) { return effect.effectId == 19; });
        assert(liveKickDownEffect != airKnockdownPlayer.hitEffects().end());
        assert(liveKickDownEffect->followsPlayerBone);
        assert(liveKickDownEffect->boneNameOverride.empty());
        assert(bootstrap.playerHitEffectConfigs().find(19)->boneName ==
               "FX_LL");
        const auto capturedKickDownVelocity =
            liveKickDownEffect->capturedPhysicsVelocity;
        assert(std::abs(capturedKickDownVelocity.x) > 0.001F ||
               std::abs(capturedKickDownVelocity.y) > 0.001F ||
               std::abs(capturedKickDownVelocity.z) > 0.001F);
        const auto kickDownDriftBefore = liveKickDownEffect->driftOffset;
        airKnockdownPlayer.update({}, gameplayCameraPose, 25);
        const auto driftingKickDownEffect = std::ranges::find_if(
            airKnockdownPlayer.hitEffects(),
            [](const auto& effect) { return effect.effectId == 19; });
        assert(driftingKickDownEffect !=
               airKnockdownPlayer.hitEffects().end());
        assert(std::abs(driftingKickDownEffect->driftOffset.x -
                        kickDownDriftBefore.x -
                        capturedKickDownVelocity.x * 0.025F) < 0.001F);
        assert(std::abs(driftingKickDownEffect->driftOffset.y -
                        kickDownDriftBefore.y -
                        capturedKickDownVelocity.y * 0.025F) < 0.001F);
        assert(std::abs(driftingKickDownEffect->driftOffset.z -
                        kickDownDriftBefore.z -
                        capturedKickDownVelocity.z * 0.025F) < 0.001F);

        // Pressing B/Circle during state 100 selects the two-line 720-degree
        // throw. Native motion 0x82 keeps both lines until authored frame 33,
        // dispatches 280 damage there, then frees the lines.
        usm::game::GameplayPlayer airWebThrowPlayer;
        assert(airWebThrowPlayer.initialize(bootstrap.player(), nullptr,
                                            &playerStateConfigs));
        airWebThrowPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                    {1.0F, 0.0F, 0.0F});
        assert(airWebThrowPlayer.requestJump());
        airWebThrowPlayer.update({}, gameplayCameraPose, 150);
        assert(airWebThrowPlayer.requestWeb(closeGroundTarget));
        for (std::uint32_t elapsed = 0;
             elapsed < 2500 && airWebThrowPlayer.activeStateId() == 99;
             elapsed += 25) {
            airWebThrowPlayer.update({}, gameplayCameraPose, 25);
        }
        assert(airWebThrowPlayer.activeStateId() == 100);
        while (airWebThrowPlayer.consumeMeleeImpact().has_value()) {
        }
        assert(airWebThrowPlayer.requestWeb(closeGroundTarget));
        airWebThrowPlayer.update(
            {}, gameplayCameraPose,
            bootstrap.player().animationBank.clips()[16]
                .durationMilliseconds());
        assert(airWebThrowPlayer.activeStateId() == 103);
        assert(airWebThrowPlayer.webLineCount() == 2);
        assert(!airWebThrowPlayer.consumeMeleeImpact().has_value());
        assert(airWebThrowPlayer.attackTimelineMilliseconds() < 1075);
        airWebThrowPlayer.update(
            {}, gameplayCameraPose,
            1074U - airWebThrowPlayer.attackTimelineMilliseconds());
        assert(!airWebThrowPlayer.consumeMeleeImpact().has_value());
        assert(airWebThrowPlayer.webLineCount() == 2);
        airWebThrowPlayer.update({}, gameplayCameraPose, 1);
        const auto airWebThrowImpact =
            airWebThrowPlayer.consumeMeleeImpact();
        assert(airWebThrowImpact.has_value());
        assert(airWebThrowImpact->stateId == 103);
        assert(airWebThrowImpact->damage == 280.0F);
        assert(airWebThrowImpact->hitType == 130);
        assert(airWebThrowImpact->targetedEnemyObjectId == 1239);
        assert(!airWebThrowPlayer.webLineActive());

        usm::game::GameplayPlayer farGroundWebPlayer;
        assert(farGroundWebPlayer.initialize(bootstrap.player(), nullptr,
                                             &playerStateConfigs));
        farGroundWebPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                     {1.0F, 0.0F, 0.0F});
        assert(farGroundWebPlayer.requestJump());
        farGroundWebPlayer.update({}, gameplayCameraPose, 150);
        const usm::game::PlayerAttackTarget farGroundTarget{
            {500.0F, 0.0F, farGroundWebPlayer.position().z},
            40.0F, 1240, false, 180.0F, true, true};
        assert(farGroundWebPlayer.requestWeb(farGroundTarget));
        assert(farGroundWebPlayer.activeStateId() == 84);

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
        jumpingPlayer.update({}, gameplayCameraPose, 150);
        assert(jumpingPlayer.animatedFootHeight() >
               jumpGroundHeight + 100.0F);
        assert(jumpingPlayer.position().z > jumpGroundHeight + 100.0F);
        assert(std::abs(jumpingPlayer.position().z -
                        jumpingPlayer.animatedFootHeight()) < 0.001F);
        // The physical capsule follows the dummy displacement while the
        // skinned mesh stays anchored and applies that animation only once.
        assert(std::abs(jumpingPlayer.worldTransform()[14] -
                        jumpGroundHeight) < 0.001F);
        jumpingPlayer.update({}, gameplayCameraPose, 150);
        assert(jumpingPlayer.activeStateId() == 14);
        assert(jumpingPlayer.activeAnimation() == "jump_to_fall");
        assert(jumpingPlayer.animatedFootHeight() >
               jumpGroundHeight + 300.0F);
        assert(jumpingPlayer.position().z > jumpGroundHeight + 300.0F);
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

        usm::game::GameplayPlayer shortWebJumpPlayer;
        assert(shortWebJumpPlayer.initialize(bootstrap.player(), nullptr,
                                             &playerStateConfigs));
        shortWebJumpPlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                     {1.0F, 0.0F, 0.0F});
        assert(shortWebJumpPlayer.requestJump());
        shortWebJumpPlayer.update({}, gameplayCameraPose, 150);
        const usm::assets::Vector3 shortWebStart =
            shortWebJumpPlayer.position();
        assert(shortWebStart.z > 100.0F);
        assert(shortWebJumpPlayer.requestJump());
        assert(shortWebJumpPlayer.activeStateId() == 30);
        assert(shortWebJumpPlayer.activeAnimation() ==
               "jump_to_small_web_swing_to_fall");
        assert(shortWebJumpPlayer.airborne());
        assert(shortWebJumpPlayer.consumeEnteredState() ==
               "k_state_jump_start");
        assert(shortWebJumpPlayer.consumeEnteredState() ==
               "k_state_jump_web_jump");
        shortWebJumpPlayer.update({}, gameplayCameraPose, 666);
        assert(shortWebJumpPlayer.position().x > shortWebStart.x + 500.0F);
        assert(shortWebJumpPlayer.position().z < shortWebStart.z - 150.0F);
        assert(shortWebJumpPlayer.activeStateId() == 15);
        assert(shortWebJumpPlayer.activeAnimation() == "fall_idle");
        assert(shortWebJumpPlayer.airborne());

        // UpdateKeyTrigger's StateBasic+0x34 gate also applies to movement
        // states. Jump-start's authored first frame is 2, so none of its
        // second-jump, punch, or web transitions may fire from frame zero.
        usm::game::GameplayPlayer jumpGatePlayer;
        assert(jumpGatePlayer.initialize(bootstrap.player(), nullptr,
                                         &playerStateConfigs));
        jumpGatePlayer.restoreAt({0.0F, 0.0F, 0.0F},
                                 {1.0F, 0.0F, 0.0F});
        assert(jumpGatePlayer.requestJump());
        assert(!jumpGatePlayer.requestJump());
        assert(!jumpGatePlayer.requestPunch());
        assert(!jumpGatePlayer.requestWeb());
        jumpGatePlayer.update({}, gameplayCameraPose, 100);
        assert(jumpGatePlayer.requestPunch());
        assert(jumpGatePlayer.activeStateId() == 81);

        usm::game::GameplayPlayer wallPlayer;
        assert(wallPlayer.initialize(bootstrap.player(),
                                     &collisionFixtureWorld,
                                     &playerStateConfigs, {}, {}, {}, &bootstrap.buttonConfigs()));
        wallPlayer.restoreAt({400.0F, 500.0F, 0.0F},
                             {1.0F, 0.0F, 0.0F});
        usm::game::CameraPose wallCamera;
        wallCamera.position = {0.0F, 500.0F, 70.0F};
        wallCamera.target = {1000.0F, 500.0F, 70.0F};
        wallPlayer.update({0.0F, 1.0F}, wallCamera, 1);
        assert(wallPlayer.onWall());
        assert(wallPlayer.activeStateId() == 6);
        assert(wallPlayer.activeAnimation() == "run_to_wall_climb");
        assert(!wallPlayer.requestPunch());
        wallPlayer.update({0.0F, 1.0F}, wallCamera, 266);
        assert(wallPlayer.onWall());
        assert(wallPlayer.activeStateId() == 1);
        assert(wallPlayer.activeAnimation() == "wall_climb_idle");
        assert(std::abs(wallPlayer.position().x - 450.0F) < 0.1F);
        assert(std::abs(wallPlayer.position().z - 158.594F) < 0.1F);
        const float wallIdleHeight = wallPlayer.position().z;
        {
            using usm::game::WallWebEventKind;
            using usm::game::WallWebPhase;
            using usm::game::WallWebRuntime;
            const auto* webState = playerStateConfigs.findState("k_state_onwall_web_qte_attack");
            assert(webState && webState->motionParameters[1] == 500.0F);
            const usm::assets::Vector3 normal{-1.0F, 0.0F, 0.0F};
            for (const int angle : {0, 45, 90, 135, 225, 270, 315}) {
                const float radians = angle * 0.0174532925F;
                assert(WallWebRuntime::directionAngle(
                    {0.0F, -std::sin(radians), std::cos(radians)}, normal) == angle);
                WallWebRuntime web;
                assert(web.begin(angle, 41355, *webState, bootstrap.player().animationBank,
                                 bootstrap.buttonConfigs()));
                assert(!web.begin(angle, 41355, *webState, bootstrap.player().animationBank,
                                  bootstrap.buttonConfigs()));
                assert(web.playerHandNode() == (angle <= 180 ? "FX_RH" : "FX_LH"));
                const auto* start = bootstrap.player().animationBank.findClip(web.animation());
                web.update(99, false);
                assert(!web.lineActive() && web.consumeEvents().empty());
                web.update(1, false);
                auto events = web.consumeEvents();
                assert(events.size() == 1 && events[0].kind == WallWebEventKind::Capture);
                assert(web.lineActive());
                web.update(start->durationMilliseconds() - 100, false);
                assert(web.phase() == WallWebPhase::Hold && web.promptActive());
                events = web.consumeEvents();
                assert(events.size() == 1 && events[0].kind == WallWebEventKind::Hold);
                web.update(0, true);
                assert(web.phase() == WallWebPhase::Hold && web.progress() == 0.125F);
                web.update(500, false);
                assert(web.progress() == 0.0F);
                for (int tap = 0; tap < 7; ++tap) { web.update(50, true); }
                assert(web.phase() == WallWebPhase::Hold && web.progress() == 0.875F);
                web.update(50, true);
                assert(web.phase() == WallWebPhase::Success && !web.promptActive());
                web.update(299, false);
                assert(web.consumeEvents().empty() && web.lineActive());
                web.update(1, false);
                events = web.consumeEvents();
                assert(events.size() == 1 && events[0].kind == WallWebEventKind::Release &&
                       events[0].success && !web.lineActive());
                web.update(1000, false, false);
                assert(!web.active());
                events = web.consumeEvents();
                assert(events.size() == 1 && events[0].kind == WallWebEventKind::Finish);
            }
            assert(WallWebRuntime::directionAngle({0.0F, -0.001F, -1.0F}, normal) == 135);
            assert(WallWebRuntime::directionAngle({0.0F, 0.001F, -1.0F}, normal) == 225);
            for (const bool interrupted : {false, true}) {
                WallWebRuntime web;
                assert(web.begin(0, 41355, *webState, bootstrap.player().animationBank,
                                 bootstrap.buttonConfigs()));
                web.update(1000, false);
                (void)web.consumeEvents();
                if (interrupted) {
                    web.update(1, false, false);
                    assert(!web.active());
                } else {
                    web.update(3000, false);
                    assert(web.phase() == WallWebPhase::Failure && !web.lineActive());
                    assert(web.consumeEvents().empty());
                    web.update(1000, false);
                    assert(!web.active());
                }
                const auto events = web.consumeEvents();
                assert(events.size() == 2 && events[0].kind == WallWebEventKind::Release &&
                       !events[0].success && events[1].kind == WallWebEventKind::Finish);
                web.cancel();
                assert(web.consumeEvents().empty());
            }
            auto player = wallPlayer;
            usm::game::PlayerAttackTarget target;
            target.position = player.position();
            target.position.z += 300.0F;
            target.objectId = 41355;
            target.onWall = true;
            assert(!player.requestWeb(target));
            target.canEnterWallWeb = true;
            target.position.z += 201.0F;
            assert(!player.requestWeb(target));
            target.position.z -= 201.0F;
            assert(player.requestWeb(target));
            assert(player.activeStateId() == 73 && player.activeAnimation() == "wall_drag_start0");
            assert(!player.requestPunch() && !player.requestJump() && !player.requestWeb(target));
            player.update({}, wallCamera, 100);
            assert(player.webLineActive() && player.webLineTargetObjectId() == 41355);
            const auto hand = player.webLineAttachPosition();
            assert(std::isfinite(hand.x) && std::isfinite(hand.y) && std::isfinite(hand.z));
            player.update({}, wallCamera, 1000);
            assert(player.wallWeb().promptActive());
            for (int tap = 0; tap < 8; ++tap) {
                player.setWallWebInput(true, true);
                player.update({}, wallCamera, 50);
            }
            assert(player.activeAnimation() == "wall_drag_success0");
            player.update({}, wallCamera, 1000);
            assert(player.activeStateId() == 1 && !player.webLineActive());
            assert(player.requestWeb(target));
            player.update({}, wallCamera, 100);
            assert(player.applyDamage(1.0F));
            assert(!player.wallWeb().active() && !player.webLineActive());
            assert(player.activeStateId() == 51);
        }
        for (const auto hitType : {100, 103, 104, 131}) {
            auto hurtWallPlayer = wallPlayer;
            const auto start = hurtWallPlayer.position();
            const auto facing = hurtWallPlayer.facing();
            assert(hurtWallPlayer.requestPunch());
            assert(hurtWallPlayer.applyDamage(50.0F, 0, 0, hitType));
            assert(hurtWallPlayer.onWall());
            assert(hurtWallPlayer.activeStateId() == (hitType > 103 ? 50 : 51));
            assert(!hurtWallPlayer.requestPunch());
            assert(!hurtWallPlayer.punchTransitionReadyAfterImpact());
            const auto reactionTime = hurtWallPlayer.hurtReactionRemainingMilliseconds();
            assert(reactionTime > 0);
            const auto* hurtClip = bootstrap.player().animationBank.findClip(
                hurtWallPlayer.activeAnimation());
            assert(hurtClip != nullptr);
            const auto authoredFirst = bootstrap.player().animationDisplacement.physicalAt(
                hurtClip->startMilliseconds);
            const auto authoredLast = bootstrap.player().animationDisplacement.physicalAt(
                hurtClip->endMilliseconds - 1);
            hurtWallPlayer.update({1.0F, 1.0F}, wallCamera, reactionTime);
            assert(hurtWallPlayer.onWall() && hurtWallPlayer.activeStateId() == 1);
            assert(hurtWallPlayer.activeAnimation() == "wall_climb_idle");
            assert(hurtWallPlayer.position().x == start.x);
            assert(hurtWallPlayer.position().y == start.y);
            assert(std::abs(hurtWallPlayer.position().z - start.z -
                            (authoredLast.z - authoredFirst.z)) < 0.01F);
            assert(hurtWallPlayer.facing().x == facing.x);
            assert(hurtWallPlayer.facing().y == facing.y);
            assert(!hurtWallPlayer.consumeMeleeImpact());
        }
        {
            auto hazardWallPlayer = wallPlayer;
            assert(hazardWallPlayer.applyDamage(30.0F, 0, 1000, 0x85));
            assert(hazardWallPlayer.activeStateId() == 50);
            const auto* clip = bootstrap.player().animationBank.findClip(
                hazardWallPlayer.activeAnimation());
            assert(clip != nullptr && clip->durationMilliseconds() < 1000);
            assert(hazardWallPlayer.hurtReactionRemainingMilliseconds() ==
                   clip->durationMilliseconds());
            hazardWallPlayer.update({}, wallCamera, clip->durationMilliseconds());
            assert(hazardWallPlayer.activeStateId() == 1);
            const float startHeight = hazardWallPlayer.position().z;
            hazardWallPlayer.update({0.0F, 1.0F}, wallCamera, 100);
            assert(hazardWallPlayer.position().z > startHeight);
        }
        for (float horizontal : {-1.0F, 1.0F}) {
            auto wallMover = wallPlayer;
            const auto start = wallMover.position();
            wallMover.update({horizontal, 0.0F}, wallCamera, 100);
            assert(wallMover.onWall());
            assert(std::abs(wallMover.position().x - start.x) < 0.01F);
            assert(std::abs(wallMover.position().y - start.y +
                            horizontal * 35.0F) < 0.01F);
            assert(std::abs(wallMover.position().z - start.z) < 0.01F);
        }
        // Player::UpdateMove state 5 supplies world-up velocity, while the
        // native Bullet cylinder maintains the 0x20 contact manifold. The
        // resulting depenetration must carry the player along a sloped wall
        // instead of leaving the portable controller on a vertical line.
        auto slopedWallFixture = collisionFixture;
        slopedWallFixture.name = "wall_sloped_fixture";
        for (std::size_t vertex = 4;
             vertex < slopedWallFixture.vertices.size(); ++vertex) {
            slopedWallFixture.vertices[vertex].position.x +=
                slopedWallFixture.vertices[vertex].position.z * 0.1F;
        }
        const std::array slopedWallFixtureSet{slopedWallFixture};
        usm::game::LevelCollision slopedWallWorld;
        assert(slopedWallWorld.build(slopedWallFixtureSet));
        usm::game::GameplayPlayer slopedWallPlayer;
        assert(slopedWallPlayer.initialize(
            bootstrap.player(), &slopedWallWorld, &playerStateConfigs,
            {}, {}, {}, &bootstrap.buttonConfigs()));
        slopedWallPlayer.restoreAt({400.0F, 500.0F, 0.0F},
                                   {1.0F, 0.0F, 0.0F});
        slopedWallPlayer.update({0.0F, 1.0F}, wallCamera, 1);
        slopedWallPlayer.update({0.0F, 1.0F}, wallCamera, 266);
        assert(slopedWallPlayer.activeStateId() == 1);
        const auto slopedWallStart = slopedWallPlayer.position();
        slopedWallPlayer.update({0.0F, 1.0F}, wallCamera, 500);
        assert(slopedWallPlayer.onWall());
        assert(slopedWallPlayer.position().z > slopedWallStart.z + 174.0F);
        assert(slopedWallPlayer.position().x > slopedWallStart.x + 15.0F);
        const std::array<usm::assets::Vector3, 4> playerWallDirections{{
            {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -1.0F},
            {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F}}};
        for (std::size_t directionIndex = 0; directionIndex < 4; ++directionIndex) {
            auto wallAttacker = wallPlayer;
            const auto start = wallAttacker.position();
            const auto direction = playerWallDirections[directionIndex];
            const usm::game::PlayerAttackTarget target{
                {start.x + direction.x * 150.0F,
                 start.y + direction.y * 150.0F,
                 start.z + direction.z * 150.0F},
                60.0F, 9999, true, 180.0F};
            assert(wallAttacker.requestPunch(target));
            assert(wallAttacker.activeStateId() == 69 + directionIndex);
            assert(wallAttacker.onWall());
            assert(!wallAttacker.requestPunch(target));
            const auto* wallAttackClip = bootstrap.player().animationBank.findClip(
                wallAttacker.activeAnimation());
            assert(wallAttackClip != nullptr);
            const auto authoredStart = bootstrap.player().animationDisplacement.physicalAt(
                wallAttackClip->startMilliseconds);
            const auto authoredEnd = bootstrap.player().animationDisplacement.physicalAt(
                wallAttackClip->endMilliseconds - 1);
            bool sawWallImpact = false;
            for (std::uint32_t frame = 0; frame < 30; ++frame) {
                wallAttacker.update({}, wallCamera, 50);
                if (const auto impact = wallAttacker.consumeMeleeImpact()) {
                    assert(!sawWallImpact);
                    sawWallImpact = true;
                    assert(impact->wallAttack && !impact->targetedDelivery);
                    assert(impact->damage == 250.0F && impact->maximumReach == 150.0F);
                    assert(impact->wallNormal.x == -1.0F);
                    assert(impact->attackDirection.x == direction.x);
                    assert(impact->attackDirection.y == direction.y);
                    assert(impact->attackDirection.z == direction.z);
                }
            }
            assert(sawWallImpact);
            assert(wallAttacker.activeStateId() == 1);
            assert(wallAttacker.activeAnimation() == "wall_climb_idle");
            assert(wallAttacker.onWall());
            assert(std::abs(wallAttacker.position().x - start.x) < 0.1F);
            assert(std::abs(wallAttacker.position().y - start.y -
                            (authoredEnd.y - authoredStart.y)) < 0.1F);
            assert(std::abs(wallAttacker.position().z - start.z -
                            (authoredEnd.z - authoredStart.z)) < 0.1F);
        }
        wallPlayer.update({0.0F, 1.0F}, wallCamera, 500);
        assert(wallPlayer.onWall());
        assert(wallPlayer.activeStateId() == 5);
        assert(wallPlayer.activeAnimation() == "wall_climb_up");
        assert(wallPlayer.position().z > wallIdleHeight + 174.0F);
        assert(wallPlayer.requestJump());
        assert(wallPlayer.activeStateId() == 8);
        assert(wallPlayer.activeAnimation() == "wall_jump_up");
        assert(wallPlayer.consumeEnteredState() ==
               "k_state_move_climb_wall");
        assert(wallPlayer.consumeEnteredState() ==
               "k_state_idle_onwall");
        assert(wallPlayer.consumeEnteredState() ==
               "k_state_move_jump_wall_up");
        const float wallJumpStartHeight = wallPlayer.position().z;
        const auto* wallJumpClip = bootstrap.player().animationBank.findClip(
            wallPlayer.activeAnimation());
        assert(wallJumpClip != nullptr &&
               wallJumpClip->durationMilliseconds() > 500);
        wallPlayer.update({}, wallCamera, 500);
        assert(wallPlayer.onWall());
        assert(wallPlayer.activeStateId() == 8);
        wallPlayer.update({}, wallCamera,
                          wallJumpClip->durationMilliseconds() - 500);
        assert(wallPlayer.onWall());
        assert(wallPlayer.activeStateId() == 1);
        assert(wallPlayer.activeAnimation() == "wall_climb_idle");
        const float wallJumpHeightDelta =
            wallPlayer.position().z - wallJumpStartHeight;
        assert(wallJumpHeightDelta > 400.0F);

        // Native CheckClimbableWall consumes the complete player-capsule
        // manifold. Model a jump_wall gap whose next 0x20 segment overlaps
        // only the upper portion of the capsule after wall_jump_up. The
        // portable wall test must reacquire that segment, while the same
        // jump without an upper segment must fall instead of inventing a
        // state-7 roof exit.
        usm::assets::ColladaGeometry splitWallGround = collisionFixture;
        splitWallGround.name = "ground_split_wall_fixture";
        splitWallGround.vertices.resize(4);
        splitWallGround.meshBuffers[0].indices = {0, 1, 2, 0, 2, 3};
        const float splitJumpEndHeight = wallIdleHeight + wallJumpHeightDelta;
        const auto makeWallBand = [](std::string name, float lower,
                                     float upper) {
            usm::assets::ColladaGeometry geometry;
            geometry.name = std::move(name);
            geometry.vertices = {
                {{500.0F, 0.0F, lower}},
                {{500.0F, 1000.0F, lower}},
                {{500.0F, 0.0F, upper}},
                {{500.0F, 1000.0F, upper}},
            };
            usm::assets::ColladaMeshBuffer buffer;
            buffer.indices = {0, 2, 3, 0, 3, 1};
            geometry.meshBuffers.push_back(std::move(buffer));
            return geometry;
        };
        const auto splitWallLower = makeWallBand(
            "wall_split_lower", 0.0F, splitJumpEndHeight - 10.0F);
        const auto splitWallUpper = makeWallBand(
            "wall_split_upper", splitJumpEndHeight + 80.0F,
            splitJumpEndHeight + 500.0F);
        const auto runSplitWallJump = [&](const usm::game::LevelCollision& world) {
            usm::game::GameplayPlayer player;
            assert(player.initialize(bootstrap.player(), &world,
                                     &playerStateConfigs, {}, {}, {},
                                     &bootstrap.buttonConfigs()));
            player.restoreAt({400.0F, 500.0F, 0.0F},
                             {1.0F, 0.0F, 0.0F});
            player.update({0.0F, 1.0F}, wallCamera, 1);
            assert(player.activeStateId() == 6);
            player.update({0.0F, 1.0F}, wallCamera, 266);
            assert(player.activeStateId() == 1);
            assert(player.requestJump());
            assert(player.activeStateId() == 8);
            player.update({}, wallCamera,
                          wallJumpClip->durationMilliseconds());
            return player;
        };
        const std::array lowerOnlyWallFixtures{
            splitWallGround, splitWallLower};
        usm::game::LevelCollision lowerOnlyWallWorld;
        assert(lowerOnlyWallWorld.build(lowerOnlyWallFixtures));
        const auto missedWallPlayer = runSplitWallJump(lowerOnlyWallWorld);
        assert(!missedWallPlayer.onWall());
        assert(missedWallPlayer.activeStateId() == 15);
        assert(missedWallPlayer.activeAnimation() == "fall_idle");

        const std::array splitWallFixtures{
            splitWallGround, splitWallLower, splitWallUpper};
        usm::game::LevelCollision splitWallWorld;
        assert(splitWallWorld.build(splitWallFixtures));
        const auto reacquiredWallPlayer = runSplitWallJump(splitWallWorld);
        assert(reacquiredWallPlayer.onWall());
        assert(reacquiredWallPlayer.activeStateId() == 1);
        assert(reacquiredWallPlayer.activeAnimation() == "wall_climb_idle");
        assert(std::abs(reacquiredWallPlayer.position().z -
                        splitJumpEndHeight) < 0.1F);

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
        // CSlider::Update selects the literal 800 cm/s for an ordinary
        // state-14 catch at 0x0031dd18-0x0031dd3a.
        assert(std::abs(slidingPlayer.position().x - slideStartX - 80.0F) <
               0.1F);
        slidingPlayer.update({}, gameplayCameraPose, 2000);
        assert(slidingPlayer.activeStateId() == 15);
        const float terminalSlideX = slidingPlayer.position().x;
        slidingPlayer.update({}, gameplayCameraPose, 50);
        assert(slidingPlayer.position().x > terminalSlideX + 30.0F);

        usm::game::GameplayPlayer slideJumpPlayer;
        assert(slideJumpPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs, {},
            playerSlides, playerSlideWaypoints));
        assert(slideJumpPlayer.requestJump());
        slideJumpPlayer.update({}, gameplayCameraPose, 300);
        assert(slideJumpPlayer.activeStateId() == 14);
        slideJumpPlayer.update({}, gameplayCameraPose, 1);
        assert(slideJumpPlayer.activeStateId() == 23);
        slideJumpPlayer.update({}, gameplayCameraPose, 401);
        assert(slideJumpPlayer.activeStateId() == 22);
        slideJumpPlayer.update({}, gameplayCameraPose, 100);
        assert(!slideJumpPlayer.requestJump());
        slideJumpPlayer.update({}, gameplayCameraPose, 101);
        const auto slideJumpStart = slideJumpPlayer.position();
        assert(slideJumpPlayer.requestJump());
        assert(slideJumpPlayer.activeStateId() == 20);
        assert(slideJumpPlayer.activeAnimation() == "slide_to_jump");
        assert(slideJumpPlayer.consumeEnteredState() ==
               "k_state_jump_start");
        assert(slideJumpPlayer.consumeEnteredState() ==
               "k_state_trigger_slider_move");
        assert(slideJumpPlayer.consumeEnteredState() ==
               "k_state_slider_jumpup");
        slideJumpPlayer.update({}, gameplayCameraPose, 200);
        assert(slideJumpPlayer.position().x > slideJumpStart.x + 100.0F);
        assert(slideJumpPlayer.position().z > slideJumpStart.z);
        slideJumpPlayer.update({}, gameplayCameraPose, 300);
        assert(slideJumpPlayer.activeStateId() == 21);
        assert(slideJumpPlayer.activeAnimation() ==
               bootstrap.player().animationBank.clips()[138].name);
        assert(slideJumpPlayer.consumeEnteredState() ==
               "k_state_slider_jumpfall");

        usm::game::GameplayPlayer directionalSlideJumpPlayer;
        assert(directionalSlideJumpPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs, {},
            playerSlides, playerSlideWaypoints));
        assert(directionalSlideJumpPlayer.requestJump());
        directionalSlideJumpPlayer.update({}, gameplayCameraPose, 300);
        directionalSlideJumpPlayer.update({}, gameplayCameraPose, 1);
        directionalSlideJumpPlayer.update({}, gameplayCameraPose, 401);
        directionalSlideJumpPlayer.update({}, gameplayCameraPose, 201);
        const auto directionalJumpStart =
            directionalSlideJumpPlayer.position();
        assert(directionalSlideJumpPlayer.requestJump(
            {1.0F, 0.0F}, gameplayCameraPose));
        const std::string_view directionalJumpAnimation =
            directionalSlideJumpPlayer.activeAnimation();
        assert(directionalJumpAnimation == "slide_to_jump_left" ||
               directionalJumpAnimation == "slide_to_jump_right");
        directionalSlideJumpPlayer.update({}, gameplayCameraPose, 250);
        assert(directionalSlideJumpPlayer.activeStateId() == 21);
        const std::string_view directionalFallAnimation =
            directionalSlideJumpPlayer.activeAnimation();
        assert(directionalFallAnimation == "slide_jump_to_fall_left" ||
               directionalFallAnimation == "slide_jump_to_fall_right");
        assert(std::abs(directionalSlideJumpPlayer.position().y -
                        directionalJumpStart.y) > 50.0F);
        directionalSlideJumpPlayer.update({}, gameplayCameraPose, 25);
        assert(directionalSlideJumpPlayer.activeStateId() == 21);

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
        testGrabPoint.verticalAngleDegrees = 80.0F;
        testGrabPoint.horizontalAngleDegrees = 15.0F;
        testGrabPoint.exitSpeed = 0.45F;
        auto chainedGrabPoint = testGrabPoint;
        chainedGrabPoint.objectId = 9002;
        chainedGrabPoint.position.x += initialFacing.x * 800.0F;
        chainedGrabPoint.position.y += initialFacing.y * 800.0F;
        const std::array<usm::game::LevelWebGrabPointAsset, 2>
            testGrabPoints{testGrabPoint, chainedGrabPoint};
        assert(swingingPlayer.initialize(bootstrap.player(), nullptr,
                                         &playerStateConfigs,
                                         testGrabPoints));
        assert(swingingPlayer.canEnableTriggerRestore());
        {
            auto deathPose = swingingPlayer;
            assert(deathPose.enterScriptedState(129, false));
            assert(!deathPose.dead() && !deathPose.canEnableTriggerRestore());
        }
        assert(swingingPlayer.requestJump());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_jump_start");
        swingingPlayer.update({}, gameplayCameraPose, 150);
        usm::game::PlayerAttackTarget competingAirTarget;
        competingAirTarget.objectId = 9010;
        competingAirTarget.position = testGrabPoint.position;
        competingAirTarget.canBeTiedUp = true;
        assert(swingingPlayer.requestWeb(competingAirTarget));
        assert(swingingPlayer.activeStateId() == 17);
        assert(!swingingPlayer.canEnableTriggerRestore());
        assert(swingingPlayer.activeAnimation() ==
               "jump_to_throw_web_right");
        assert(swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_web_throw");
        swingingPlayer.update({}, gameplayCameraPose, 267);
        assert(swingingPlayer.activeStateId() == 18);
        assert(!swingingPlayer.canEnableTriggerRestore());
        assert(swingingPlayer.activeAnimation() ==
               "swing_hang_fwd_right");
        assert(swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_hang");
        assert(swingingPlayer.releaseWeb());
        assert(swingingPlayer.activeStateId() == 19);
        assert(swingingPlayer.canEnableTriggerRestore());
        assert(swingingPlayer.activeAnimation() ==
               "swing_hang_right_to_swing_idle_2");
        assert(!swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_idle");
        // Player::UpdateMove (0x0035081c), state 19 at 0x00350a8c, accepts a
        // new web grab while the prior release animation is still active.
        // GetBestWebGrabPoint (0x00344424) excludes the current point, so the
        // second authored point must start a fresh state-17 throw.
        assert(swingingPlayer.requestWeb());
        assert(swingingPlayer.activeStateId() == 17);
        const auto chainedAnchor = swingingPlayer.webLineAnchor();
        assert(std::abs(chainedAnchor.x - chainedGrabPoint.position.x) <
               0.01F);
        assert(std::abs(chainedAnchor.y - chainedGrabPoint.position.y) <
               0.01F);
        assert(std::abs(chainedAnchor.z - chainedGrabPoint.position.z) <
               0.01F);
        assert(swingingPlayer.webLineActive());
        assert(swingingPlayer.consumeEnteredState() ==
               "k_state_swing_web_throw");
        swingingPlayer.update({}, gameplayCameraPose, 100);
        assert(swingingPlayer.airborne());

        // UpdateMCSpeed motion 28 (0x00347e66-0x00347fd0) accumulates a
        // decaying copy of the initial launch vector during an ordinary web
        // release. This is materially faster than integrating only OutSpeed.
        usm::game::GameplayPlayer releaseMomentumPlayer;
        const std::array<usm::game::LevelWebGrabPointAsset, 1>
            releaseMomentumPoints{testGrabPoint};
        assert(releaseMomentumPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs,
            releaseMomentumPoints));
        assert(releaseMomentumPlayer.requestJump());
        releaseMomentumPlayer.update({}, gameplayCameraPose, 150);
        assert(releaseMomentumPlayer.requestWeb());
        releaseMomentumPlayer.update({}, gameplayCameraPose, 267);
        assert(releaseMomentumPlayer.releaseWeb());
        const auto releaseStart = releaseMomentumPlayer.position();
        releaseMomentumPlayer.update({}, gameplayCameraPose, 50);
        const float releaseHorizontalDistance = std::hypot(
            releaseMomentumPlayer.position().x - releaseStart.x,
            releaseMomentumPlayer.position().y - releaseStart.y);
        assert(releaseHorizontalDistance > 20.0F);

        // A linked WayPoint is a forced motion-28 exit, not ordinary air
        // steering. Player::UpdateMove derives a release velocity that reaches
        // it over the animation and caps that velocity at 3000 cm/s.
        usm::game::LevelWebGrabPointAsset forcedGrabPoint = testGrabPoint;
        forcedGrabPoint.objectId = 9003;
        forcedGrabPoint.cannotControl = true;
        forcedGrabPoint.hasTargetWaypoint = true;
        forcedGrabPoint.targetWaypointId = 9004;
        forcedGrabPoint.targetWaypointPosition = {
            bootstrap.player().position.x + initialFacing.x * 2200.0F,
            bootstrap.player().position.y + initialFacing.y * 2200.0F,
            bootstrap.player().position.z + 800.0F,
        };
        const std::array<usm::game::LevelWebGrabPointAsset, 1>
            forcedGrabPoints{forcedGrabPoint};
        usm::game::GameplayPlayer forcedSwingPlayer;
        assert(forcedSwingPlayer.initialize(
            bootstrap.player(), nullptr, &playerStateConfigs,
            forcedGrabPoints));
        assert(forcedSwingPlayer.requestJump());
        forcedSwingPlayer.update({}, gameplayCameraPose, 150);
        assert(forcedSwingPlayer.requestWeb());
        forcedSwingPlayer.update({}, gameplayCameraPose, 267);
        assert(forcedSwingPlayer.releaseWeb());
        assert(forcedSwingPlayer.activeStateId() == 19);
        const auto forcedReleaseStart = forcedSwingPlayer.position();
        const auto* forcedReleaseClip =
            bootstrap.player().animationBank.findClip(
                forcedSwingPlayer.activeAnimation());
        assert(forcedReleaseClip != nullptr);
        const auto forcedRootStart =
            bootstrap.player().animationDisplacement.physicalAt(
                forcedReleaseClip->startMilliseconds);
        const auto forcedRootMiddle =
            bootstrap.player().animationDisplacement.physicalAt(
                forcedReleaseClip->startMilliseconds + 500);
        const auto forcedRootEnd =
            bootstrap.player().animationDisplacement.physicalAt(
                forcedReleaseClip->startMilliseconds +
                forcedReleaseClip->durationMilliseconds());
        assert(forcedRootMiddle.z > forcedRootStart.z + 100.0F);
        assert(forcedRootMiddle.z > forcedRootEnd.z + 100.0F);
        for (std::uint32_t elapsed = 0; elapsed < 500; elapsed += 25) {
            forcedSwingPlayer.update({}, gameplayCameraPose, 25);
        }
        // The release clip's recovered Dummy_center track rises through a
        // large mid-animation arc. Native target setup explicitly zeros its
        // Z speed at 0x00350fb8/0x00350fc0, so vertical movement comes from
        // that root track plus the existing PhysicsContext gravity rather
        // than a straight three-dimensional chord to the WayPoint.
        assert(forcedSwingPlayer.position().z >
               forcedReleaseStart.z + 25.0F);
        for (std::uint32_t elapsed = 500;
             elapsed < forcedReleaseClip->durationMilliseconds();
             elapsed += 25) {
            forcedSwingPlayer.update({}, gameplayCameraPose, 25);
        }
        const auto forcedExitPosition = forcedSwingPlayer.position();
        const float targetForwardTravel =
            (forcedGrabPoint.targetWaypointPosition.x -
             forcedReleaseStart.x) * initialFacing.x +
            (forcedGrabPoint.targetWaypointPosition.y -
             forcedReleaseStart.y) * initialFacing.y;
        const float actualForwardTravel =
            (forcedExitPosition.x - forcedReleaseStart.x) * initialFacing.x +
            (forcedExitPosition.y - forcedReleaseStart.y) * initialFacing.y;
        // SetNextStateId motion 28 retains the release impulse at
        // Player+0x434/+0x438. UpdateMCSpeed 0x00347f5c-0x00347fbc adds its
        // decaying value on top of the target-derived XY velocity every
        // frame, so the linked point aims the launch and is not a homing
        // endpoint that clamps the body to the exact WayPoint coordinate.
        assert(actualForwardTravel > 0.0F);
        assert(std::abs(actualForwardTravel - targetForwardTravel) > 25.0F);

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
        // Native CCameraArea::ProcessAttr squares the authored height. Keep a
        // signed fixture so negative shipped values remain enterable.
        adjacentCameraAreas[1].height = -100.0F;
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

        usm::game::CameraArea followArea =
            makeCameraArea(3, 0.0F, 10.0F);
        followArea.zFollowRate = 0.25F;
        usm::game::GameplayCamera followCamera;
        assert(followCamera.bind(std::span{&followArea, 1}, 3));
        const auto followedPose = followCamera.sample({5.0F, 5.0F, 40.0F});
        assert(std::abs(followedPose.target.z - 130.0F) < 0.001F);
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
        // The shopping-center opening is not a stateful door. It is part of
        // Room1's single static geometry BDAE, with no room morph and no
        // mesh-bearing IRR door object available to swap at impact.
        assert(bootstrap.rooms().front().geometry.morphs().empty());
        assert(std::none_of(
            bootstrap.rooms().front().scene.nodes().begin(),
            bootstrap.rooms().front().scene.nodes().end(),
            [](const usm::assets::IrrSceneNode& node) {
                const auto namesDoor = [](std::string_view value) {
                    return value.find("door") != std::string_view::npos ||
                           value.find("Door") != std::string_view::npos;
                };
                return !node.meshFile.empty() &&
                       (namesDoor(node.name) || namesDoor(node.gameType) ||
                        namesDoor(node.meshFile) ||
                        namesDoor(node.animationFile));
            }));
        const auto crashCinematic = std::find_if(
            bootstrap.cinematics().begin(), bootstrap.cinematics().end(),
            [](const usm::game::LevelCinematicAsset& cinematic) {
                return cinematic.objectId == 1212;
            });
        assert(crashCinematic != bootstrap.cinematics().end());
        assert(crashCinematic->name == "Cinematic_crash");
        assert(crashCinematic->script.commandCount() == 48);
        std::size_t crashVisibilityCommandCount = 0;
        bool hidSandman = false;
        bool showedJumpHint = false;
        for (const auto& thread : crashCinematic->script.threads()) {
            assert(thread.objectId == -1 || thread.objectId == 288 ||
                   thread.objectId == 1210 || thread.objectId == 1211);
            for (const auto& command : thread.commands) {
                if (command.name != "SetVisible") {
                    continue;
                }
                ++crashVisibilityCommandCount;
                const auto* objectId = command.findAttribute("ObjectID");
                const auto* visible = command.findAttribute("Visible");
                assert(objectId != nullptr && visible != nullptr);
                hidSandman |= thread.objectId == 1211 &&
                    objectId->value == "-1" && visible->value == "false";
                showedJumpHint |= thread.objectId == -1 &&
                    objectId->value == "1216" && visible->value == "true";
            }
        }
        assert(crashVisibilityCommandCount == 2);
        assert(hidSandman);
        assert(showedJumpHint);
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
                const usm::game::CinematicThread& thread,
                const usm::game::CinematicCommand& command) {
                if (introStartResult) {
                    introStartResult =
                        introCommandRuntime.applyCommand(thread, command);
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
                        introCommandRuntime.applyCommand(thread, command);
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
        const auto copActor = std::find_if(
            bootstrap.introActors().begin(), bootstrap.introActors().end(),
            [](const usm::game::CinematicActorAsset& actor) {
                return actor.objectId == 1261;
            });
        assert(copActor != bootstrap.introActors().end());
        assert(std::all_of(
            copActor->animation.tracks().begin(),
            copActor->animation.tracks().end(), [](const auto& track) {
                return track.property !=
                           usm::assets::ColladaAnimationProperty::Rotation ||
                       track.componentCount == 4;
            }));
        const auto findCopTrack = [&copActor](std::string_view id) {
            return std::find_if(
                copActor->animation.tracks().begin(),
                copActor->animation.tracks().end(),
                [id](const auto& track) { return track.id == id; });
        };
        const auto leftForearm =
            findCopTrack("Bip01_L_Forearm-node-rotation");
        const auto rightCalf = findCopTrack("Bip01_R_Calf-node-rotation");
        const auto rightForearm =
            findCopTrack("Bip01_R_Forearm-node-rotation");
        assert(leftForearm != copActor->animation.tracks().end());
        assert(rightCalf != copActor->animation.tracks().end());
        assert(rightForearm != copActor->animation.tracks().end());
        assert(leftForearm->property ==
               usm::assets::ColladaAnimationProperty::RotationAngle);
        assert(rightCalf->property ==
               usm::assets::ColladaAnimationProperty::RotationAngle);
        assert(rightForearm->property ==
               usm::assets::ColladaAnimationProperty::RotationAngle);
        assert(leftForearm->componentCount == 1);
        assert(std::abs(leftForearm->values[0] - 0.913822F) < 0.0001F);
        assert(std::abs(rightCalf->values[0] - 0.485579F) < 0.0001F);
        assert(std::abs(rightForearm->values[0] - 0.668310F) < 0.0001F);
        std::vector<usm::assets::ColladaGeometry> copPose;
        assert(usm::assets::evaluateColladaPose(
            copActor->mesh, copActor->animation, 0, copPose));
        assert(copPose.size() == 1);
        const auto copJointVertex = [&](std::string_view nodeId) {
            const auto& skin = copActor->mesh.skins().front();
            std::size_t jointIndex = skin.jointNames.size();
            for (std::size_t index = 0; index < skin.jointNames.size(); ++index) {
                const auto* node =
                    copActor->mesh.findSceneNodeByScopeId(skin.jointNames[index]);
                if (node != nullptr && node->id == nodeId) {
                    jointIndex = index;
                    break;
                }
            }
            assert(jointIndex < skin.jointNames.size());
            std::size_t bestVertex = 0;
            float bestWeight = -1.0F;
            for (std::size_t vertex = 0;
                 vertex < skin.vertexInfluences.size(); ++vertex) {
                for (const auto& influence : skin.vertexInfluences[vertex]) {
                    if (influence.jointIndex == jointIndex &&
                        influence.weight > bestWeight) {
                        bestWeight = influence.weight;
                        bestVertex = vertex;
                    }
                }
            }
            assert(bestWeight > 0.8F);
            return copPose.front().vertices[bestVertex].position;
        };
        const auto leftForearmPoint =
            copJointVertex("Bip01_L_Forearm-node");
        const auto rightForearmPoint =
            copJointVertex("Bip01_R_Forearm-node");
        assert(std::abs(leftForearmPoint.x - 15593.4F) < 0.1F);
        assert(std::abs(leftForearmPoint.y + 9831.05F) < 0.1F);
        assert(std::abs(leftForearmPoint.z - 144.155F) < 0.1F);
        assert(std::abs(rightForearmPoint.x - 15642.5F) < 0.1F);
        assert(std::abs(rightForearmPoint.y + 9807.28F) < 0.1F);
        assert(std::abs(rightForearmPoint.z - 141.827F) < 0.1F);
        assert(std::abs(copPose.front().bounds.minimum.z - 36.6302F) < 0.01F);
        assert(std::abs(copPose.front().bounds.maximum.x - 15654.8F) < 0.1F);
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
        assert(carActor->mesh.morphs().size() == 2);
        assert(carActor->mesh.morphs()[0].controllerId ==
               "car_plice-mesh-morpher");
        assert(carActor->mesh.morphs()[0].sourceGeometryId ==
               "car_plice-mesh");
        assert((carActor->mesh.morphs()[0].targetGeometryIndices ==
                std::vector<std::uint32_t>{2, 3, 4}));
        assert((carActor->mesh.morphs()[0].weights ==
                std::vector<float>{0.0F, 0.0F, 0.0F}));
        assert(carActor->mesh.morphs()[0].method == 0);
        assert(carActor->mesh.sceneNodes().size() == 2);
        assert(carActor->mesh.sceneNodes()[0].geometryIndices.size() == 1);
        assert(carActor->mesh.sceneNodes()[0].geometryIndices[0] == 0);
        assert(carActor->mesh.sceneNodes()[0].geometryControllerIds[0] ==
               "car_plice-mesh-morpher");
        assert(carActor->mesh.sceneNodes()[1].geometryIndices.size() == 1);
        assert(carActor->mesh.sceneNodes()[1].geometryIndices[0] == 1);
        std::vector<usm::assets::ColladaGeometry> carStartPose;
        std::vector<usm::assets::ColladaGeometry> carLaterPose;
        assert(usm::assets::evaluateColladaPose(
            carActor->mesh, carActor->animation, 0, carStartPose));
        assert(usm::assets::evaluateColladaPose(
            carActor->mesh, carActor->animation,
            carActor->animation.durationMilliseconds(), carLaterPose));
        assert(carStartPose.size() == 2);
        assert(carStartPose[0].id == "car_plice-mesh");
        assert(carStartPose[1].id == "bbox-mesh");
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
        const usm::game::CinematicCommand* gameplay3DCommand = nullptr;
        std::size_t gameplay3DSoundCount = 0;
        for (const auto& cinematic : bootstrap.cinematics()) {
            if (!cinematic.scriptAvailable) {
                continue;
            }
            gameplaySoundScripts.push_back(&cinematic.script);
            for (const auto& thread : cinematic.script.threads()) {
                for (const auto& command : thread.commands) {
                    const auto* stop2D = command.findAttribute("Stop2D");
                    const auto* stop = command.findAttribute("Stop");
                    const auto* play3D = command.findAttribute("Play3D");
                    if (command.name == "SoundControl" && play3D != nullptr &&
                        play3D->value == "true") {
                        ++gameplay3DSoundCount;
                        gameplay3DCommand = &command;
                        const bool hasSourceObject =
                            thread.objectId == bootstrap.player().objectId ||
                            std::any_of(
                                bootstrap.enemies().begin(),
                                bootstrap.enemies().end(),
                                [&thread](const auto& enemy) {
                                    return enemy.objectId == thread.objectId;
                                }) ||
                            std::any_of(
                                bootstrap.objects().begin(),
                                bootstrap.objects().end(),
                                [&thread](const auto& object) {
                                    return object.objectId == thread.objectId;
                                });
                        assert(hasSourceObject);
                    }
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
        assert(gameplay3DSoundCount == 8);
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
        assert(gameplay3DCommand != nullptr);
        std::size_t flat3DDispatchCount = 0;
        std::size_t spatialDispatchCount = 0;
        assert(gameplaySounds.dispatch(
            *gameplay3DCommand,
            [&flat3DDispatchCount](std::string_view,
                                   const usm::audio::PcmAudio&, bool) {
                ++flat3DDispatchCount;
                return usm::Result::success();
            },
            {},
            [&spatialDispatchCount](std::string_view,
                                     const usm::audio::PcmAudio&, bool) {
                ++spatialDispatchCount;
                return usm::Result::success();
            }));
        assert(flat3DDispatchCount == 0);
        assert(spatialDispatchCount == 1);

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
