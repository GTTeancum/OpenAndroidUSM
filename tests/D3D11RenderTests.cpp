#include "renderer/d3d11/D3D11Renderer.hpp"
#include "game/LevelOneBootstrap.hpp"

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
    }
    return 0;
}
