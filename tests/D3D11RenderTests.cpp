#include "renderer/d3d11/D3D11Renderer.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/CinematicPlayer.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelEnemyRuntime.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    const std::filesystem::path dataRoot = USM_TEST_GAME_DATA_ROOT;
    if (std::filesystem::exists(dataRoot / "levelnew_01.pack")) {
        usm::game::LevelOneBootstrap levelOne;
        assert(levelOne.load(dataRoot));
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
        const std::uint32_t captureSize =
            std::getenv("OPENANDROIDUSM_CAPTURE_DIR") == nullptr ? 256U : 1024U;
        assert(gameRenderer.initializeOffscreen(captureSize, captureSize));
        assert(gameRenderer.uploadLevelOneScene(levelOne));
        assert(gameRenderer.updateLevelOneActors(levelOne, 0));
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
        usm::game::GameplayCamera gameplayCamera;
        assert(gameplayCamera.bind(levelOne.cameraAreas(),
                                   levelOne.player().initialCameraAreaId));
        assert(gameRenderer.updateLevelOneActors(
            levelOne,
            levelOne.introCameraAnimation().durationMilliseconds()));
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, 0, levelOne.player().worldTransform));
        assert(gameRenderer.setCamera(
            gameplayCamera.sample(levelOne.player().position)));
        gameRenderer.renderFrame();
        RgbaImage gameplayFrame;
        assert(gameRenderer.readBackImage(gameplayFrame));
        captureIfRequested(gameplayFrame, "gameplay-start.bmp");
        assert(gameplayFrame.pixels.size() == rendered.pixels.size());
        assert(gameRenderer.updateLevelOnePlayer(
            levelOne, *idleClip, idleClip->durationMilliseconds() / 2,
            levelOne.player().worldTransform));
        gameRenderer.renderFrame();
        RgbaImage gameplayMiddleFrame;
        assert(gameRenderer.readBackImage(gameplayMiddleFrame));
        captureIfRequested(gameplayMiddleFrame, "gameplay-idle-middle.bmp");

        usm::game::GameplayPlayer gameplayPlayer;
        usm::game::LevelCollision levelCollision;
        assert(levelCollision.build(levelOne.introRooms()));
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
    }
    return 0;
}
