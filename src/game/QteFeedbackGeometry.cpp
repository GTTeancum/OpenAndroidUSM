#include "game/QteFeedbackGeometry.hpp"

#include <algorithm>

namespace usm::game {
Result buildQteFeedbackGeometry(
    const QteFeedbackFrame& frame, const assets::SpriteAtlas& atlas,
    std::uint32_t textureWidth, std::uint32_t textureHeight,
    std::uint32_t viewportWidth, std::uint32_t viewportHeight,
    std::vector<QteFeedbackVertex>& output) {
    output.clear();
    if (frame.count > frame.sprites.size()) { return Result::failure("Invalid QTE feedback sprite count"); }
    if (frame.count == 0 || viewportWidth == 0 || viewportHeight == 0) { return Result::success(); }
    if (textureWidth == 0 || textureHeight == 0) { return Result::failure("QTE feedback has no texture"); }
    // Same aspect-preserving 480x320 PC viewport mapping as the HUD. This is
    // host presentation, not a change to original frame coordinates/UVs.
    const float scale = std::min(viewportWidth / 480.0F, viewportHeight / 320.0F);
    const float dx = (viewportWidth - 480.0F * scale) * 0.5F;
    const float dy = (viewportHeight - 320.0F * scale) * 0.5F;
    std::vector<QteFeedbackVertex> vertices;
    for (std::size_t i = 0; i < frame.count; ++i) {
        const auto& draw = frame.sprites[i];
        if (draw.frameIndex >= atlas.frames().size() || draw.flags != 0) {
            return Result::failure("Unsupported native QTE feedback frame/transform");
        }
        for (const auto& fm : atlas.modulesForFrame(draw.frameIndex)) {
            if (fm.moduleIndex >= atlas.modules().size() || fm.flags != 0) {
                return Result::failure("Unsupported native QTE feedback module/transform");
            }
            const auto& module = atlas.modules()[fm.moduleIndex];
            if (module.imageIndex != 0 || module.width == 0 || module.height == 0 ||
                static_cast<std::uint32_t>(module.x) + module.width > textureWidth ||
                static_cast<std::uint32_t>(module.y) + module.height > textureHeight) {
                return Result::failure("Native QTE feedback module exceeds texture");
            }
            const float left = dx + static_cast<float>(static_cast<std::int64_t>(draw.position.x) + fm.x) * scale;
            const float top = dy + static_cast<float>(static_cast<std::int64_t>(draw.position.y) + fm.y) * scale;
            const float right = left + module.width * scale;
            const float bottom = top + module.height * scale;
            const float x0 = 2.0F * left / viewportWidth - 1.0F;
            const float x1 = 2.0F * right / viewportWidth - 1.0F;
            const float y0 = 1.0F - 2.0F * top / viewportHeight;
            const float y1 = 1.0F - 2.0F * bottom / viewportHeight;
            const float u0 = static_cast<float>(module.x) / textureWidth;
            const float u1 = static_cast<float>(module.x + module.width) / textureWidth;
            const float v0 = static_cast<float>(module.y) / textureHeight;
            const float v1 = static_cast<float>(module.y + module.height) / textureHeight;
            const std::uint32_t rgba = 0x00ffffffU | (static_cast<std::uint32_t>(draw.alpha) << 24U);
            const QteFeedbackVertex tl{x0,y0,u0,v0,rgba}, tr{x1,y0,u1,v0,rgba};
            const QteFeedbackVertex bl{x0,y1,u0,v1,rgba}, br{x1,y1,u1,v1,rgba};
            vertices.insert(vertices.end(), {tl,tr,bl,tr,br,bl});
        }
    }
    output.swap(vertices); // No half-built geometry after an unsupported case.
    return Result::success();
}
} // namespace usm::game
