#include "assets/DdsAtcTexture.hpp"
#include "assets/SpriteAtlas.hpp"
#include "assets/TgaTexture.hpp"
#include "game/CinematicUiRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "renderer/d3d11/D3D11Renderer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

std::byte byte(std::uint8_t value) {
    return static_cast<std::byte>(value);
}

void writeU16(std::vector<std::byte>& bytes, std::size_t offset,
              std::uint16_t value) {
    bytes.at(offset) = byte(static_cast<std::uint8_t>(value & 0xffU));
    bytes.at(offset + 1) =
        byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void writeU32(std::vector<std::byte>& bytes, std::size_t offset,
              std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.at(offset + index) = byte(static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xffU));
    }
}

void appendU8(std::vector<std::byte>& bytes, std::uint8_t value) {
    bytes.push_back(byte(value));
}

void appendU16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(byte(static_cast<std::uint8_t>(value & 0xffU)));
    bytes.push_back(
        byte(static_cast<std::uint8_t>((value >> 8U) & 0xffU)));
}

std::vector<std::byte> makeInterfaceDds() {
    // Two 4x4 ATCA blocks: opaque red on the left, opaque green on the right.
    // The production DdsAtcTexture decoder turns this into an 8x4 RGBA image.
    std::vector<std::byte> bytes(128U + 32U);
    bytes[0] = byte('D');
    bytes[1] = byte('D');
    bytes[2] = byte('S');
    bytes[3] = byte(' ');
    writeU32(bytes, 4, 124);
    writeU32(bytes, 12, 4);
    writeU32(bytes, 16, 8);
    writeU32(bytes, 28, 1);
    writeU32(bytes, 76, 32);
    bytes[84] = byte('A');
    bytes[85] = byte('T');
    bytes[86] = byte('C');
    bytes[87] = byte('A');

    const auto writeBlock = [&bytes](std::size_t offset,
                                     std::uint16_t highColor565) {
        for (std::size_t index = 0; index < 8; ++index) {
            bytes[offset + index] = byte(0xff);
        }
        writeU16(bytes, offset + 8, 0);
        writeU16(bytes, offset + 10, highColor565);
        for (std::size_t index = 12; index < 16; ++index) {
            // Color index 3 selects highColor565 for every texel.
            bytes[offset + index] = byte(0xff);
        }
    };
    writeBlock(128, 0xf800); // RGB565 red.
    writeBlock(144, 0x07e0); // RGB565 green.
    return bytes;
}

std::vector<std::byte> makeAuxiliaryDds() {
    // uploadHud validates all shipped HUD texture slots. The unrelated slots
    // use this single opaque white 4x4 synthetic ATCA block.
    std::vector<std::byte> bytes(128U + 16U);
    bytes[0] = byte('D');
    bytes[1] = byte('D');
    bytes[2] = byte('S');
    bytes[3] = byte(' ');
    writeU32(bytes, 4, 124);
    writeU32(bytes, 12, 4);
    writeU32(bytes, 16, 4);
    writeU32(bytes, 28, 1);
    writeU32(bytes, 76, 32);
    bytes[84] = byte('A');
    bytes[85] = byte('T');
    bytes[86] = byte('C');
    bytes[87] = byte('A');
    for (std::size_t index = 0; index < 8; ++index) {
        bytes[128 + index] = byte(0xff);
    }
    writeU16(bytes, 136, 0);
    writeU16(bytes, 138, 0xffff);
    for (std::size_t index = 140; index < 144; ++index) {
        bytes[index] = byte(0xff);
    }
    return bytes;
}

std::vector<std::byte> makeAuxiliaryTga() {
    std::vector<std::byte> bytes(18U + 4U * 4U * 4U);
    bytes[2] = byte(2); // Uncompressed true color.
    writeU16(bytes, 12, 4);
    writeU16(bytes, 14, 4);
    bytes[16] = byte(32);
    bytes[17] = byte(0x20); // Top origin.
    for (std::size_t offset = 18; offset < bytes.size(); offset += 4) {
        bytes[offset] = byte(0xff);
        bytes[offset + 1] = byte(0xff);
        bytes[offset + 2] = byte(0xff);
        bytes[offset + 3] = byte(0xff);
    }
    return bytes;
}

std::vector<std::byte> makeInterfaceAtlas() {
    // Real SpriteAtlas binary layout, minimized to the two recovered completed
    // mash frames used here: frame 85 samples the red block and frame 93 the
    // green block. Both occupy the same 4x4 destination rectangle.
    constexpr std::uint16_t frameCount = 94;
    std::vector<std::byte> bytes;
    appendU16(bytes, 0xa9d1);
    appendU16(bytes, 0); // flags
    appendU16(bytes, 2); // modules
    appendU16(bytes, 2); // frame modules
    appendU16(bytes, frameCount);
    appendU16(bytes, 0); // animation frames
    appendU16(bytes, 0); // animations

    appendU8(bytes, 0);
    appendU8(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 4);
    appendU16(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 4);
    appendU16(bytes, 4);
    appendU16(bytes, 4);
    appendU16(bytes, 4);

    appendU16(bytes, 0);
    appendU16(bytes, 1);
    appendU8(bytes, 0);
    appendU8(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 0);
    appendU16(bytes, 0);

    for (std::uint16_t index = 0; index < frameCount; ++index) {
        appendU8(bytes, index == 85 || index == 93 ? 1 : 0);
    }
    for (std::uint16_t index = 0; index < frameCount; ++index) {
        appendU16(bytes, index == 93 ? 1 : 0);
    }
    return bytes;
}

usm::game::LevelHudAsset makeSyntheticHud() {
    usm::game::LevelHudAsset hud;
    const auto atlasBytes = makeInterfaceAtlas();
    const usm::Result atlasResult = hud.interfaceAtlas.load(
        std::span<const std::byte>(atlasBytes.data(), atlasBytes.size()));
    require(static_cast<bool>(atlasResult), atlasResult.message());

    const auto interfaceBytes = makeInterfaceDds();
    const usm::Result interfaceResult = hud.interfaceTexture.load(
        std::span<const std::byte>(interfaceBytes.data(),
                                   interfaceBytes.size()));
    require(static_cast<bool>(interfaceResult), interfaceResult.message());

    const auto ddsBytes = makeAuxiliaryDds();
    const auto loadDds = [&ddsBytes](usm::assets::DdsAtcTexture& texture) {
        const usm::Result result = texture.load(
            std::span<const std::byte>(ddsBytes.data(), ddsBytes.size()));
        require(static_cast<bool>(result), result.message());
    };
    loadDds(hud.tutorialTexture);
    loadDds(hud.transportTexture);
    loadDds(hud.mainMenuTexture);
    loadDds(hud.normalWhiteFontTexture);
    loadDds(hud.outlineSmallFontTexture);
    loadDds(hud.outlineBigFontTexture);

    const auto tgaBytes = makeAuxiliaryTga();
    const usm::Result tgaResult = hud.backgroundSuitTexture.load(
        std::span<const std::byte>(tgaBytes.data(), tgaBytes.size()));
    require(static_cast<bool>(tgaResult), tgaResult.message());
    return hud;
}

void checkPixelNear(const std::array<std::uint8_t, 4>& rgba,
                    const std::array<int, 4>& expected, int tolerance,
                    std::string_view message) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const int delta =
            std::abs(static_cast<int>(rgba[i]) - expected[i]);
        require(delta <= tolerance, message);
    }
}

void checkClearPixel(const std::array<std::uint8_t, 4>& rgba) {
    checkPixelNear(rgba, {6, 11, 22, 255}, 1,
                   "D3D11 clear-color readback differs from expected UNORM value");
}

std::array<std::uint8_t, 4> readPixel(
    usm::renderer::D3D11Renderer& renderer, std::uint32_t x,
    std::uint32_t y) {
    std::array<std::uint8_t, 4> rgba{};
    const usm::Result result = renderer.readBackPixel(x, y, rgba);
    require(static_cast<bool>(result), result.message());
    return rgba;
}

void renderFeedback(usm::renderer::D3D11Renderer& renderer,
                    const usm::game::LevelHudAsset& hud,
                    const usm::game::QteFeedbackFrame& feedback) {
    usm::game::CinematicUiFrame frame;
    frame.quickTimeFeedback = feedback;
    const usm::Result update = renderer.updateCinematicUi(hud, frame);
    require(static_cast<bool>(update), update.message());
    renderer.renderFrame();
}

} // namespace

int main() {
    try {
        usm::renderer::D3D11Renderer renderer;
        const usm::Result init = renderer.initializeOffscreen(480, 320);
        require(static_cast<bool>(init), init.message());

        renderer.renderFrame();
        const auto first = readPixel(renderer, 0, 0);
        const auto center = readPixel(renderer, 240, 160);
        checkClearPixel(first);
        checkClearPixel(center);
        require(first == center, "Empty WARP frame was not spatially uniform");

        const usm::game::LevelHudAsset hud = makeSyntheticHud();
        const usm::Result hudUpload = renderer.uploadHud(hud);
        require(static_cast<bool>(hudUpload), hudUpload.message());

        usm::game::QteFeedbackFrame feedback;
        feedback.count = 1;
        feedback.sprites[0] = {85, {100, 100}, 0, 255};
        renderFeedback(renderer, hud, feedback);
        checkPixelNear(readPixel(renderer, 101, 101), {255, 0, 0, 255}, 1,
                       "Opaque QTE feedback did not sample the expected atlas module");
        checkClearPixel(readPixel(renderer, 99, 99));

        feedback.sprites[0] = {93, {100, 100}, 0, 255};
        renderFeedback(renderer, hud, feedback);
        checkPixelNear(readPixel(renderer, 101, 101), {0, 255, 0, 255}, 1,
                       "QTE feedback did not preserve atlas UV selection");

        // CQTEManager::Draw paints completed-mash frame 85 followed by 93.
        // Overlap them here so GPU readback proves the retained draw order.
        feedback.count = 2;
        feedback.sprites[0] = {85, {100, 100}, 0, 255};
        feedback.sprites[1] = {93, {100, 100}, 0, 255};
        renderFeedback(renderer, hud, feedback);
        checkPixelNear(readPixel(renderer, 101, 101), {0, 255, 0, 255}, 1,
                       "QTE feedback GPU draw order does not match native sprite order");

        // Failure feedback uses vertex alpha. With the production
        // SRC_ALPHA/INV_SRC_ALPHA state, 128-alpha red over the native clear
        // color resolves to approximately (131,5,11,191) in UNORM readback.
        feedback.count = 1;
        feedback.sprites[0] = {85, {100, 100}, 0, 128};
        renderFeedback(renderer, hud, feedback);
        checkPixelNear(readPixel(renderer, 101, 101), {131, 5, 11, 191}, 2,
                       "QTE feedback alpha compositing differs from D3D11 native blend state");

        std::cout << "PASS D3D11 WARP render/readback + QTE feedback backend\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL D3D11 backend: " << error.what() << '\n';
        return 1;
    }
}
