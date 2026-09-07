#include "assets/ColladaMesh.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <unordered_map>
#include <string_view>

namespace usm::assets {
namespace {

constexpr std::uint32_t kColladaRootPointerField = 0x1c;
constexpr std::uint32_t kImageCountOffset = 0x34;
constexpr std::uint32_t kImageArrayOffset = 0x38;
constexpr std::uint32_t kImageSize = 0x14;
constexpr std::uint32_t kEffectCountOffset = 0x3c;
constexpr std::uint32_t kEffectArrayOffset = 0x40;
constexpr std::uint32_t kEffectSize = 0x5c;
constexpr std::uint32_t kMaterialCountOffset = 0x44;
constexpr std::uint32_t kMaterialArrayOffset = 0x48;
constexpr std::uint32_t kMaterialSize = 0x40;
constexpr std::uint32_t kGeometryCountOffset = 0x4c;
constexpr std::uint32_t kGeometryArrayOffset = 0x50;
constexpr std::uint32_t kGeometrySize = 0x10;
constexpr std::uint32_t kMeshBufferSize = 0x3c;
constexpr std::uint32_t kVisualSceneCountOffset = 0x6c;
constexpr std::uint32_t kVisualSceneArrayOffset = 0x70;
constexpr std::uint32_t kVisualSceneSize = 0x10;
constexpr std::uint32_t kSceneNodeSize = 0x50;
constexpr std::uint32_t kSceneInstanceSize = 0x08;
constexpr std::uint32_t kControllerInstanceType = 2;
constexpr std::uint32_t kGeometryInstanceType = 3;
constexpr std::uint32_t kControllerCountOffset = 0x54;
constexpr std::uint32_t kControllerArrayOffset = 0x58;
constexpr std::uint32_t kControllerSize = 0x0c;
constexpr std::uint32_t kSkinControllerType = 0;
constexpr std::uint32_t kMorphControllerType = 1;

class BinaryView final {
public:
    explicit BinaryView(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <typename Integer>
    [[nodiscard]] std::optional<Integer> integer(std::uint32_t offset) const {
        if (offset > bytes_.size() || sizeof(Integer) > bytes_.size() - offset) {
            return std::nullopt;
        }
        Integer result{};
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            result |= static_cast<Integer>(std::to_integer<unsigned char>(
                          bytes_[offset + index]))
                      << (index * 8);
        }
        return result;
    }

    [[nodiscard]] std::optional<float> floating(std::uint32_t offset) const {
        const auto bits = integer<std::uint32_t>(offset);
        return bits ? std::optional(std::bit_cast<float>(*bits)) : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> string(
        std::uint32_t offset) const {
        if (offset >= bytes_.size()) {
            return std::nullopt;
        }
        const char* begin = reinterpret_cast<const char*>(bytes_.data() + offset);
        const char* end = reinterpret_cast<const char*>(bytes_.data() + bytes_.size());
        const char* terminator = std::find(begin, end, '\0');
        if (terminator == end) {
            return std::nullopt;
        }
        return std::string(begin, terminator);
    }

    [[nodiscard]] bool contains(std::uint64_t offset,
                                std::uint64_t size) const noexcept {
        return offset <= bytes_.size() && size <= bytes_.size() - offset;
    }

private:
    std::span<const std::byte> bytes_;
};

std::optional<Vector3> readVector3(const BinaryView& view,
                                   std::uint32_t offset) {
    const auto x = view.floating(offset);
    const auto y = view.floating(offset + 4);
    const auto z = view.floating(offset + 8);
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return Vector3{*x, *y, *z};
}

std::optional<AxisAlignedBounds> readBounds(const BinaryView& view,
                                            std::uint32_t offset) {
    const auto minimum = readVector3(view, offset);
    const auto maximum = readVector3(view, offset + 12);
    if (!minimum || !maximum) {
        return std::nullopt;
    }
    return AxisAlignedBounds{*minimum, *maximum};
}

struct AffineTransform {
    // Row-major 3x3 storage matching Irrlicht CMatrix4. The original
    // transformVect (0x002c2924) treats points as row vectors:
    // point * linear + translation.
    std::array<float, 9> linear{1.0F, 0.0F, 0.0F,
                                0.0F, 1.0F, 0.0F,
                                0.0F, 0.0F, 1.0F};
    Vector3 translation{};
};

Vector3 multiply(const std::array<float, 9>& matrix,
                 const Vector3& value) noexcept {
    return {
        matrix[0] * value.x + matrix[3] * value.y + matrix[6] * value.z,
        matrix[1] * value.x + matrix[4] * value.y + matrix[7] * value.z,
        matrix[2] * value.x + matrix[5] * value.y + matrix[8] * value.z,
    };
}

std::array<float, 9> multiply(const std::array<float, 9>& left,
                              const std::array<float, 9>& right) noexcept {
    std::array<float, 9> result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t component = 0; component < 3; ++component) {
                result[row * 3 + column] +=
                    left[row * 3 + component] *
                    right[component * 3 + column];
            }
        }
    }
    return result;
}

AffineTransform combine(const AffineTransform& parent,
                        const AffineTransform& local) noexcept {
    return {
        // ISceneNode::updateAbsolutePosition calls parent.mult34(local), and
        // mult34 (0x002bac54) emits local * parent for this row-vector layout.
        multiply(local.linear, parent.linear),
        [&] {
            const Vector3 translated = multiply(parent.linear, local.translation);
            return Vector3{translated.x + parent.translation.x,
                           translated.y + parent.translation.y,
                           translated.z + parent.translation.z};
        }(),
    };
}

std::optional<AffineTransform> readNodeTransform(const BinaryView& view,
                                                 std::uint32_t nodeOffset) {
    const auto position = readVector3(view, nodeOffset + 0x0c);
    const auto scale = readVector3(view, nodeOffset + 0x28);
    const auto x = view.floating(nodeOffset + 0x18);
    const auto y = view.floating(nodeOffset + 0x1c);
    const auto z = view.floating(nodeOffset + 0x20);
    const auto w = view.floating(nodeOffset + 0x24);
    if (!position || !scale || !x || !y || !z || !w ||
        !std::isfinite(position->x) || !std::isfinite(position->y) ||
        !std::isfinite(position->z) || !std::isfinite(scale->x) ||
        !std::isfinite(scale->y) || !std::isfinite(scale->z) ||
        !std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) ||
        !std::isfinite(*w)) {
        return std::nullopt;
    }

    // The shipped path copies SNode::Rotation unchanged in
    // ISceneNode::setRotation (0x00408978). The raw components then enter
    // quaternion::getMatrix_transposed (0x00305f48); it does not normalize.
    const float qx = *x;
    const float qy = *y;
    const float qz = *z;
    const float qw = *w;
    const float xx = qx * qx;
    const float yy = qy * qy;
    const float zz = qz * qz;
    const float xy = qx * qy;
    const float xz = qx * qz;
    const float yz = qy * qz;
    const float wx = qw * qx;
    const float wy = qw * qy;
    const float wz = qw * qz;

    AffineTransform result;
    // quaternion::getMatrix_transposed (0x00305f48) emits these rows, then
    // ISceneNode::getRelativeTransformation (0x004083c4) scales each complete
    // row by the matching local-axis scale.
    result.linear = {
        (1.0F - 2.0F * (yy + zz)) * scale->x,
        (2.0F * (xy - wz)) * scale->x,
        (2.0F * (xz + wy)) * scale->x,
        (2.0F * (xy + wz)) * scale->y,
        (1.0F - 2.0F * (xx + zz)) * scale->y,
        (2.0F * (yz - wx)) * scale->y,
        (2.0F * (xz - wy)) * scale->z,
        (2.0F * (yz + wx)) * scale->z,
        (1.0F - 2.0F * (xx + yy)) * scale->z,
    };
    result.translation = *position;
    return result;
}

std::optional<std::array<float, 9>> normalMatrix(
    const std::array<float, 9>& matrix) noexcept {
    const float determinant =
        matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
        matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
        matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
    if (std::abs(determinant) <= std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }
    const float inverse = 1.0F / determinant;
    // Inverse transpose of the source matrix.
    return std::array<float, 9>{
        (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * inverse,
        (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * inverse,
        (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * inverse,
        (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * inverse,
        (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * inverse,
        (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * inverse,
        (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * inverse,
        (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * inverse,
        (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * inverse,
    };
}

void updateBounds(ColladaGeometry& geometry) {
    if (geometry.vertices.empty()) {
        geometry.bounds = {};
        return;
    }
    geometry.bounds = {geometry.vertices.front().position,
                       geometry.vertices.front().position};
    for (const ColladaVertex& vertex : geometry.vertices) {
        geometry.bounds.minimum.x =
            std::min(geometry.bounds.minimum.x, vertex.position.x);
        geometry.bounds.minimum.y =
            std::min(geometry.bounds.minimum.y, vertex.position.y);
        geometry.bounds.minimum.z =
            std::min(geometry.bounds.minimum.z, vertex.position.z);
        geometry.bounds.maximum.x =
            std::max(geometry.bounds.maximum.x, vertex.position.x);
        geometry.bounds.maximum.y =
            std::max(geometry.bounds.maximum.y, vertex.position.y);
        geometry.bounds.maximum.z =
            std::max(geometry.bounds.maximum.z, vertex.position.z);
    }
}

void transformGeometry(ColladaGeometry& geometry,
                       const AffineTransform& transform) {
    const auto normals = normalMatrix(transform.linear);
    for (ColladaVertex& vertex : geometry.vertices) {
        const Vector3 transformed = multiply(transform.linear, vertex.position);
        vertex.position = {transformed.x + transform.translation.x,
                           transformed.y + transform.translation.y,
                           transformed.z + transform.translation.z};
        if (normals) {
            vertex.normal = multiply(*normals, vertex.normal);
            const float length = std::sqrt(vertex.normal.x * vertex.normal.x +
                                           vertex.normal.y * vertex.normal.y +
                                           vertex.normal.z * vertex.normal.z);
            if (length > std::numeric_limits<float>::epsilon()) {
                vertex.normal.x /= length;
                vertex.normal.y /= length;
                vertex.normal.z /= length;
            }
        }
    }
    updateBounds(geometry);
}

std::optional<ColladaPrimitive> decodePrimitive(std::uint32_t value) {
    switch (value) {
    case 0:
        return ColladaPrimitive::Triangles;
    case 1:
        return ColladaPrimitive::TriangleStrip;
    case 2:
        return ColladaPrimitive::Lines;
    case 3:
        return ColladaPrimitive::LineStrip;
    case 4:
        return ColladaPrimitive::LineLoop;
    default:
        return std::nullopt;
    }
}

std::optional<std::uint32_t> componentOffset(const BinaryView& view,
                                             std::uint32_t componentOffsets,
                                             std::uint32_t componentCount,
                                             std::int8_t componentIndex) {
    if (componentIndex < 0 ||
        static_cast<std::uint32_t>(componentIndex) >= componentCount) {
        return std::nullopt;
    }
    return view.integer<std::uint32_t>(
        componentOffsets + static_cast<std::uint32_t>(componentIndex) * 4);
}

std::optional<std::uint32_t> effectTextureImageIndex(
    const BinaryView& view, std::uint32_t effectOffset,
    std::uint32_t textureEntryOffset, std::uint32_t imageCount) {
    // CResFileManager::postLoadProcess (0x00425d88) resolves each serialized
    // texture entry through two effect-local index tables before it reaches
    // the SCollada image library. Keep those indices immutable here.
    const auto mapIndex = view.integer<std::uint32_t>(textureEntryOffset);
    const auto mapCount = view.integer<std::uint32_t>(effectOffset + 0x4c);
    const auto mapArray = view.integer<std::uint32_t>(effectOffset + 0x50);
    const auto imageReferenceCount =
        view.integer<std::uint32_t>(effectOffset + 0x54);
    const auto imageReferenceArray =
        view.integer<std::uint32_t>(effectOffset + 0x58);
    if (!mapIndex || !mapCount || !mapArray || !imageReferenceCount ||
        !imageReferenceArray || *mapIndex >= *mapCount ||
        !view.contains(*mapArray,
                       static_cast<std::uint64_t>(*mapCount) * 4) ||
        !view.contains(*imageReferenceArray,
                       static_cast<std::uint64_t>(*imageReferenceCount) * 4)) {
        return std::nullopt;
    }
    const auto imageReferenceIndex =
        view.integer<std::uint32_t>(*mapArray + *mapIndex * 4);
    if (!imageReferenceIndex ||
        *imageReferenceIndex >= *imageReferenceCount) {
        return std::nullopt;
    }
    const auto imageIndex = view.integer<std::uint32_t>(
        *imageReferenceArray + *imageReferenceIndex * 4);
    return imageIndex && *imageIndex < imageCount
               ? std::optional(*imageIndex)
               : std::nullopt;
}

Result parseGeometry(const BinaryView& view, std::uint32_t geometryOffset,
                     ColladaGeometry& output) {
    const auto idOffset = view.integer<std::uint32_t>(geometryOffset);
    const auto nameOffset = view.integer<std::uint32_t>(geometryOffset + 4);
    const auto meshOffset = view.integer<std::uint32_t>(geometryOffset + 12);
    if (!idOffset || !nameOffset || !meshOffset) {
        return Result::failure("BDAE geometry record is truncated");
    }
    const auto id = view.string(*idOffset);
    const auto name = view.string(*nameOffset);
    if (!id || !name) {
        return Result::failure("BDAE geometry has an invalid name pointer");
    }
    output.id = *id;
    output.name = *name;

    const auto storageMode = view.integer<std::uint32_t>(*meshOffset);
    const auto vertexCount = view.integer<std::uint32_t>(*meshOffset + 4);
    const auto vertexDataOffset = view.integer<std::uint32_t>(*meshOffset + 8);
    const auto bufferCount = view.integer<std::uint32_t>(*meshOffset + 12);
    const auto buffersOffset = view.integer<std::uint32_t>(*meshOffset + 16);
    const auto bounds = readBounds(view, *meshOffset + 20);
    if (!storageMode || !vertexCount || !vertexDataOffset || !bufferCount ||
        !buffersOffset || !bounds) {
        return Result::failure("BDAE mesh record is truncated");
    }
    if (*storageMode != 1) {
        return Result::failure("Unsupported non-interleaved BDAE vertex layout");
    }
    output.bounds = *bounds;

    const auto vertexStride = view.integer<std::uint32_t>(*vertexDataOffset);
    const auto componentCount =
        view.integer<std::uint32_t>(*vertexDataOffset + 4);
    const auto componentOffsets =
        view.integer<std::uint32_t>(*vertexDataOffset + 8);
    const auto vertexBytes = view.integer<std::uint32_t>(*vertexDataOffset + 28);
    if (!vertexStride || !componentCount || !componentOffsets || !vertexBytes ||
        *vertexStride == 0 ||
        !view.contains(*vertexBytes,
                       static_cast<std::uint64_t>(*vertexStride) * *vertexCount)) {
        return Result::failure("BDAE interleaved vertex stream is invalid");
    }

    output.meshBuffers.reserve(*bufferCount);
    std::int8_t positionComponent = -1;
    std::int8_t normalComponent = -1;
    std::int8_t textureComponent = -1;
    std::int8_t secondaryTextureComponent = -1;
    std::int8_t colorComponent = -1;
    for (std::uint32_t index = 0; index < *bufferCount; ++index) {
        const std::uint32_t bufferOffset =
            *buffersOffset + index * kMeshBufferSize;
        const auto primitiveValue = view.integer<std::uint32_t>(bufferOffset);
        const auto materialOffset = view.integer<std::uint32_t>(bufferOffset + 4);
        const auto indexCount = view.integer<std::uint32_t>(bufferOffset + 24);
        const auto indicesOffset = view.integer<std::uint32_t>(bufferOffset + 28);
        const auto firstVertex = view.integer<std::uint16_t>(bufferOffset + 32);
        const auto lastVertex = view.integer<std::uint16_t>(bufferOffset + 34);
        const auto bufferBounds = readBounds(view, bufferOffset + 36);
        if (!primitiveValue || !materialOffset || !indexCount || !indicesOffset ||
            !firstVertex || !lastVertex || !bufferBounds ||
            !view.contains(*indicesOffset,
                           static_cast<std::uint64_t>(*indexCount) * 2)) {
            return Result::failure("BDAE mesh buffer is invalid");
        }
        const auto primitive = decodePrimitive(*primitiveValue);
        const auto material = view.string(*materialOffset);
        if (!primitive || !material) {
            return Result::failure("BDAE mesh buffer metadata is invalid");
        }

        if (index == 0) {
            positionComponent = static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 12));
            normalComponent = static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 13));
            textureComponent = static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 16));
            secondaryTextureComponent = static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 19));
            colorComponent = static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 22));
        }

        ColladaMeshBuffer buffer;
        buffer.primitive = *primitive;
        buffer.materialName = *material;
        buffer.firstVertex = *firstVertex;
        buffer.lastVertex = *lastVertex;
        buffer.usesSecondaryTextureCoordinates =
            static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 19)) != -1;
        buffer.bounds = *bufferBounds;
        buffer.indices.reserve(*indexCount);
        for (std::uint32_t indexPosition = 0; indexPosition < *indexCount;
             ++indexPosition) {
            const auto vertexIndex = view.integer<std::uint16_t>(
                *indicesOffset + indexPosition * sizeof(std::uint16_t));
            if (!vertexIndex || *vertexIndex >= *vertexCount) {
                return Result::failure("BDAE index is outside the vertex stream");
            }
            buffer.indices.push_back(*vertexIndex);
        }
        output.meshBuffers.push_back(std::move(buffer));
    }

    const auto positionOffset = componentOffset(
        view, *componentOffsets, *componentCount, positionComponent);
    const auto normalOffset = componentOffset(
        view, *componentOffsets, *componentCount, normalComponent);
    const auto textureOffset = componentOffset(
        view, *componentOffsets, *componentCount, textureComponent);
    const auto secondaryTextureOffset = componentOffset(
        view, *componentOffsets, *componentCount, secondaryTextureComponent);
    const auto colorOffset = componentOffset(
        view, *componentOffsets, *componentCount, colorComponent);
    if (!positionOffset) {
        return Result::failure("BDAE geometry has no position component");
    }

    output.vertices.reserve(*vertexCount);
    for (std::uint32_t vertexIndex = 0; vertexIndex < *vertexCount; ++vertexIndex) {
        const std::uint32_t vertexOffset =
            *vertexBytes + vertexIndex * *vertexStride;
        const auto position = readVector3(view, vertexOffset + *positionOffset);
        if (!position) {
            return Result::failure("BDAE position stream is truncated");
        }

        ColladaVertex vertex;
        vertex.position = *position;
        if (normalOffset) {
            const auto normal = readVector3(view, vertexOffset + *normalOffset);
            if (!normal) {
                return Result::failure("BDAE normal stream is truncated");
            }
            vertex.normal = *normal;
        }
        if (textureOffset) {
            const auto u = view.floating(vertexOffset + *textureOffset);
            const auto v = view.floating(vertexOffset + *textureOffset + 4);
            if (!u || !v) {
                return Result::failure("BDAE texture-coordinate stream is truncated");
            }
            vertex.textureCoordinate = {*u, *v};
        }
        if (secondaryTextureOffset) {
            const auto u =
                view.floating(vertexOffset + *secondaryTextureOffset);
            const auto v =
                view.floating(vertexOffset + *secondaryTextureOffset + 4);
            if (!u || !v) {
                return Result::failure(
                    "BDAE secondary texture-coordinate stream is truncated");
            }
            vertex.secondaryTextureCoordinate = {*u, *v};
        }
        if (colorOffset) {
            const auto color =
                view.integer<std::uint32_t>(vertexOffset + *colorOffset);
            if (!color) {
                return Result::failure("BDAE color stream is truncated");
            }
            vertex.color = *color;
        }
        output.vertices.push_back(vertex);
    }

    return Result::success();
}

Result parseImageLibrary(const BinaryView& view, std::uint32_t rootOffset,
                         std::vector<ColladaImage>& output) {
    const auto count = view.integer<std::uint32_t>(rootOffset + kImageCountOffset);
    const auto array = view.integer<std::uint32_t>(rootOffset + kImageArrayOffset);
    if (!count || !array) {
        return Result::failure("BDAE image-library fields are truncated");
    }
    if (*count == 0) {
        return Result::success();
    }
    if (!view.contains(*array, static_cast<std::uint64_t>(*count) * kImageSize)) {
        return Result::failure("BDAE image library is invalid");
    }

    output.reserve(*count);
    for (std::uint32_t index = 0; index < *count; ++index) {
        const std::uint32_t entry = *array + index * kImageSize;
        const auto idOffset = view.integer<std::uint32_t>(entry);
        const auto nameOffset = view.integer<std::uint32_t>(entry + 4);
        const auto sourceOffset = view.integer<std::uint32_t>(entry + 8);
        if (!idOffset || !nameOffset || !sourceOffset) {
            return Result::failure("BDAE image record is truncated");
        }
        const auto id = view.string(*idOffset);
        const auto name = view.string(*nameOffset);
        const auto source = view.string(*sourceOffset);
        if (!id || !name || !source) {
            return Result::failure("BDAE image has an invalid string pointer");
        }
        output.push_back({*id, *name, *source});
    }
    return Result::success();
}

Result parseMaterialLibrary(const BinaryView& view, std::uint32_t rootOffset,
                            const std::vector<ColladaImage>& images,
                            std::vector<ColladaMaterial>& output) {
    const std::uint32_t imageCount =
        static_cast<std::uint32_t>(images.size());
    const auto imageArray =
        view.integer<std::uint32_t>(rootOffset + kImageArrayOffset);
    const auto effectCount =
        view.integer<std::uint32_t>(rootOffset + kEffectCountOffset);
    const auto effectArray =
        view.integer<std::uint32_t>(rootOffset + kEffectArrayOffset);
    const auto materialCount =
        view.integer<std::uint32_t>(rootOffset + kMaterialCountOffset);
    const auto materialArray =
        view.integer<std::uint32_t>(rootOffset + kMaterialArrayOffset);
    if (!imageArray || !effectCount || !effectArray || !materialCount ||
        !materialArray) {
        return Result::failure("BDAE material-library fields are truncated");
    }
    if ((*effectCount != 0 &&
         !view.contains(*effectArray,
                        static_cast<std::uint64_t>(*effectCount) * kEffectSize)) ||
        (*materialCount != 0 &&
         !view.contains(*materialArray,
                        static_cast<std::uint64_t>(*materialCount) *
                            kMaterialSize))) {
        return Result::failure("BDAE material library is invalid");
    }

    output.reserve(*materialCount);
    for (std::uint32_t index = 0; index < *materialCount; ++index) {
        const std::uint32_t entry = *materialArray + index * kMaterialSize;
        const auto idOffset = view.integer<std::uint32_t>(entry);
        const auto nameOffset = view.integer<std::uint32_t>(entry + 4);
        const auto effectIdOffset = view.integer<std::uint32_t>(entry + 12);
        const auto effectIndex = view.integer<std::uint32_t>(entry + 24);
        if (!idOffset || !nameOffset || !effectIdOffset || !effectIndex) {
            return Result::failure("BDAE material record is truncated");
        }
        const auto id = view.string(*idOffset);
        const auto name = view.string(*nameOffset);
        const auto effectId = view.string(*effectIdOffset);
        if (!id || !name || !effectId || *effectIndex >= *effectCount) {
            return Result::failure("BDAE material metadata is invalid");
        }

        const auto secondaryTextureMode =
            view.integer<std::uint32_t>(entry + 0x30);
        const auto additiveBlend =
            view.integer<std::uint32_t>(entry + 0x1c);
        const auto backFaceCulling =
            view.integer<std::uint32_t>(entry + 0x38);
        const auto frontFaceCulling =
            view.integer<std::uint32_t>(entry + 0x3c);
        if (!secondaryTextureMode || !additiveBlend || !backFaceCulling ||
            !frontFaceCulling) {
            return Result::failure("BDAE material mode is truncated");
        }
        ColladaMaterial material{*id, *name, *effectId, std::nullopt,
                                 std::nullopt, std::nullopt,
                                 *secondaryTextureMode,
                                 *additiveBlend != 0,
                                 *backFaceCulling != 0,
                                 *frontFaceCulling != 0, false};
        const auto imageIndexFromReference =
            [imageCount, imageArray](std::uint32_t pointer)
            -> std::optional<std::uint32_t> {
            if (pointer < imageCount) {
                return pointer;
            }
            if (pointer < *imageArray) {
                return std::nullopt;
            }
            const std::uint32_t byteOffset = pointer - *imageArray;
            if (byteOffset % kImageSize != 0) {
                return std::nullopt;
            }
            const std::uint32_t imageIndex = byteOffset / kImageSize;
            return imageIndex < imageCount
                       ? std::optional(imageIndex)
                       : std::nullopt;
        };
        const auto primaryImage = view.integer<std::uint32_t>(entry + 0x24);
        const auto secondaryImage = view.integer<std::uint32_t>(entry + 0x28);
        if (!primaryImage || !secondaryImage) {
            return Result::failure("BDAE material texture fields are truncated");
        }
        material.diffuseImageIndex = imageIndexFromReference(*primaryImage);
        material.secondaryImageIndex = imageIndexFromReference(*secondaryImage);
        const std::uint32_t effect = *effectArray + *effectIndex * kEffectSize;
        const auto materialTypeParameter = view.floating(effect + 0x2c);
        if (!materialTypeParameter) {
            return Result::failure(
                "BDAE material type parameter is truncated");
        }
        material.materialTypeParameter = *materialTypeParameter;
        const auto ambientUsesTextures =
            view.integer<std::uint8_t>(effect + 0x08);
        const auto ambientPayload =
            view.integer<std::uint32_t>(effect + 0x0c);
        if (!ambientUsesTextures || !ambientPayload) {
            return Result::failure("BDAE material ambient fields are truncated");
        }
        if (*ambientUsesTextures == 0 && *ambientPayload != 0) {
            for (std::size_t component = 0;
                 component < material.ambientColor.size(); ++component) {
                const auto value = view.integer<std::uint8_t>(
                    *ambientPayload + static_cast<std::uint32_t>(component));
                if (!value) {
                    return Result::failure(
                        "BDAE material ambient color is truncated");
                }
                material.ambientColor[component] =
                    static_cast<float>(*value) / 255.0F;
            }
        }
        const auto diffuseUsesTextures =
            view.integer<std::uint8_t>(effect + 0x10);
        const auto diffusePayload =
            view.integer<std::uint32_t>(effect + 0x14);
        if (diffuseUsesTextures && *diffuseUsesTextures == 1 &&
            diffusePayload && *diffusePayload != 0) {
            const auto textureCount =
                view.integer<std::uint32_t>(*diffusePayload);
            const auto textureArray =
                view.integer<std::uint32_t>(*diffusePayload + 4);
            if (textureCount && textureArray &&
                view.contains(*textureArray,
                              static_cast<std::uint64_t>(*textureCount) *
                                  0x1c)) {
                if (*textureCount != 0) {
                    const auto translationU =
                        view.floating(*textureArray + 0x08);
                    const auto translationV =
                        view.floating(*textureArray + 0x0c);
                    const auto rotation =
                        view.floating(*textureArray + 0x10);
                    const auto scaleU =
                        view.floating(*textureArray + 0x14);
                    const auto scaleV =
                        view.floating(*textureArray + 0x18);
                    if (!translationU || !translationV || !rotation ||
                        !scaleU || !scaleV ||
                        !std::isfinite(*translationU) ||
                        !std::isfinite(*translationV) ||
                        !std::isfinite(*rotation) ||
                        !std::isfinite(*scaleU) ||
                        !std::isfinite(*scaleV)) {
                        return Result::failure(
                            "BDAE diffuse texture transform is invalid");
                    }
                    // buildTextureTransform at 0x00419260 receives a zero
                    // center from CMaterial::prepareMaterial. Retain the
                    // resulting 2D affine portion exactly; notably the knife
                    // thug authors translation U=-0.498 to select a different
                    // quadrant of the shared 2x2 thug atlas.
                    const float cosine = std::cos(*rotation);
                    const float sine = std::sin(*rotation);
                    material.diffuseTextureTransform = {
                        cosine * *scaleU,
                        sine * *scaleU,
                        -sine * *scaleV,
                        cosine * *scaleV,
                        *translationU,
                        *translationV,
                    };
                }
                if (!material.diffuseImageIndex) {
                    std::vector<std::uint32_t> layerImages;
                    for (std::uint32_t layer = 0; layer < *textureCount;
                         ++layer) {
                        const auto descriptorOwner =
                            view.integer<std::uint32_t>(
                                *textureArray + layer * 0x1c);
                        const auto descriptor =
                            descriptorOwner && *descriptorOwner != 0
                                ? view.integer<std::uint32_t>(*descriptorOwner)
                                : std::nullopt;
                        const auto imageIdOffset =
                            descriptor && *descriptor != 0
                                ? view.integer<std::uint32_t>(*descriptor)
                                : std::nullopt;
                        const auto sourceType =
                            descriptor && *descriptor != 0
                                ? view.integer<std::uint32_t>(*descriptor + 0x0c)
                                : std::nullopt;
                        if (!imageIdOffset || !sourceType || *sourceType != 1) {
                            continue;
                        }
                        const auto imageId = view.string(*imageIdOffset);
                        if (!imageId) {
                            continue;
                        }
                        const auto image = std::find_if(
                            images.begin(), images.end(),
                            [&imageId](const ColladaImage& candidate) {
                                return candidate.id == *imageId;
                            });
                        if (image != images.end()) {
                            layerImages.push_back(static_cast<std::uint32_t>(
                                image - images.begin()));
                        }
                    }
                    if (!layerImages.empty()) {
                        material.diffuseImageIndex = layerImages[0];
                    }
                    if (layerImages.size() > 1) {
                        material.secondaryImageIndex = layerImages[1];
                    }
                }
            }
        }
        // SEffect's final pointer resolves to its diffuse SImage index for
        // textured effects. Color-only effects use a different payload.
        const auto imageIndexPointer = view.integer<std::uint32_t>(effect + 0x58);
        if (!material.diffuseImageIndex && imageIndexPointer) {
            const auto imageIndex = view.integer<std::uint32_t>(*imageIndexPointer);
            if (imageIndex && *imageIndex < imageCount) {
                material.diffuseImageIndex = *imageIndex;
            }
        }
        // CMaterial::prepareMaterial (0x0041c750) treats the effect channel at
        // +0x38/+0x3c as a lightmap only when its referenced image name
        // contains "lightmap". Merely having a TEXCOORD1 component does not
        // select the lightmap renderer; many ordinary level meshes carry
        // unrelated secondary coordinates.
        const auto lightmapUsesTextures =
            view.integer<std::uint8_t>(effect + 0x38);
        const auto lightmapPayload =
            view.integer<std::uint32_t>(effect + 0x3c);
        if (lightmapUsesTextures && *lightmapUsesTextures == 1 &&
            lightmapPayload && *lightmapPayload != 0) {
            const auto textureCount =
                view.integer<std::uint32_t>(*lightmapPayload);
            const auto textureArray =
                view.integer<std::uint32_t>(*lightmapPayload + 4);
            if (textureCount && textureArray && *textureCount != 0 &&
                view.contains(*textureArray,
                              static_cast<std::uint64_t>(*textureCount) *
                                  0x1c)) {
                for (std::uint32_t layer = 0; layer < *textureCount; ++layer) {
                    const auto imageIndex = effectTextureImageIndex(
                        view, effect, *textureArray + layer * 0x1c,
                        imageCount);
                    if (imageIndex &&
                        images[*imageIndex].sourcePath.find("lightmap") !=
                            std::string::npos) {
                        material.lightmapImageIndex = *imageIndex;
                        break;
                    }
                }
            }
        }
        const auto effectOpacity = view.floating(effect + 0x40);
        if (!lightmapUsesTextures || !effectOpacity) {
            return Result::failure(
                "BDAE material transparency fields are truncated");
        }
        // CMaterial::prepareMaterial (0x0041c750) selects native renderer
        // index 0x0e, TRANSPARENT_ALPHA_CHANNEL, for a single-layer material
        // when the effect's transparent-texture channel is present, the
        // secondary mode requests alpha, or the effect opacity is not one.
        // A channel already classified as a named lightmap is handled by the
        // two-layer lightmap renderer instead.
        material.transparentAlphaChannel =
            !material.secondaryImageIndex &&
            ((!material.lightmapImageIndex && *lightmapUsesTextures == 1) ||
             material.secondaryTextureMode == 1 ||
             *effectOpacity != 1.0F);
        output.push_back(std::move(material));
    }
    return Result::success();
}

Result parseSceneNodes(
    const BinaryView& view, std::uint32_t nodeArray, std::uint32_t nodeCount,
    const AffineTransform& parentTransform,
    const std::unordered_map<std::string, std::uint32_t>& geometryIndices,
    const std::unordered_map<std::string, std::uint32_t>&
        controllerGeometryIndices,
    const std::vector<ColladaGeometry>& geometries,
    std::vector<ColladaGeometry>& output,
    std::vector<ColladaSceneNode>& sceneNodes, std::int32_t parentIndex,
    std::uint32_t depth) {
    if (depth > 64 ||
        !view.contains(nodeArray,
                       static_cast<std::uint64_t>(nodeCount) * kSceneNodeSize)) {
        return Result::failure("BDAE visual-scene node hierarchy is invalid");
    }
    for (std::uint32_t nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex) {
        const std::uint32_t nodeOffset =
            nodeArray + nodeIndex * kSceneNodeSize;
        const auto idOffset = view.integer<std::uint32_t>(nodeOffset);
        const auto nameOffset = view.integer<std::uint32_t>(nodeOffset + 4);
        const auto scopeIdOffset = view.integer<std::uint32_t>(nodeOffset + 8);
        const auto localTransform = readNodeTransform(view, nodeOffset);
        const auto childCount = view.integer<std::uint32_t>(nodeOffset + 0x38);
        const auto childArray = view.integer<std::uint32_t>(nodeOffset + 0x3c);
        const auto instanceCount = view.integer<std::uint32_t>(nodeOffset + 0x40);
        const auto instanceArray = view.integer<std::uint32_t>(nodeOffset + 0x44);
        if (!idOffset || !nameOffset || !scopeIdOffset || !localTransform ||
            !childCount || !childArray || !instanceCount || !instanceArray) {
            return Result::failure("BDAE visual-scene node is truncated");
        }
        const auto id = view.string(*idOffset);
        const auto name = view.string(*nameOffset);
        const auto scopeId = *scopeIdOffset == 0
                                 ? std::optional(std::string{})
                                 : view.string(*scopeIdOffset);
        const auto position = readVector3(view, nodeOffset + 0x0c);
        const auto scale = readVector3(view, nodeOffset + 0x28);
        const auto rotationX = view.floating(nodeOffset + 0x18);
        const auto rotationY = view.floating(nodeOffset + 0x1c);
        const auto rotationZ = view.floating(nodeOffset + 0x20);
        const auto rotationW = view.floating(nodeOffset + 0x24);
        if (!id || !name || !scopeId || !position || !scale || !rotationX ||
            !rotationY || !rotationZ || !rotationW ||
            (*instanceCount != 0 &&
             !view.contains(*instanceArray,
                            static_cast<std::uint64_t>(*instanceCount) *
                                kSceneInstanceSize))) {
            return Result::failure("BDAE visual-scene node metadata is invalid");
        }

        const std::int32_t currentNodeIndex =
            static_cast<std::int32_t>(sceneNodes.size());
        const AffineTransform worldTransform =
            combine(parentTransform, *localTransform);
        sceneNodes.push_back({*id,
                              *name,
                              *scopeId,
                              parentIndex,
                              *position,
                              {*rotationX, *rotationY, *rotationZ, *rotationW},
                              *scale,
                              worldTransform.linear,
                              worldTransform.translation,
                              {}});
        for (std::uint32_t instanceIndex = 0;
             instanceIndex < *instanceCount; ++instanceIndex) {
            const std::uint32_t instanceOffset =
                *instanceArray + instanceIndex * kSceneInstanceSize;
            const auto type = view.integer<std::uint32_t>(instanceOffset);
            const auto payload = view.integer<std::uint32_t>(instanceOffset + 4);
            if (!type || !payload) {
                return Result::failure("BDAE scene instance is truncated");
            }
            if (*type != kGeometryInstanceType &&
                *type != kControllerInstanceType) {
                continue;
            }
            const auto externalFile = view.integer<std::uint32_t>(*payload);
            const auto geometryUrlOffset =
                view.integer<std::uint32_t>(*payload + 4);
            if (!externalFile || !geometryUrlOffset) {
                return Result::failure("BDAE geometry instance is truncated");
            }
            // External geometry references are resolved by the owning level
            // scene, not by this self-contained BDAE resource.
            if (*externalFile != 0) {
                continue;
            }
            const auto geometryUrl = view.string(*geometryUrlOffset);
            if (!geometryUrl || geometryUrl->empty() ||
                geometryUrl->front() != '#') {
                return Result::failure("BDAE geometry instance URL is invalid");
            }
            const auto& instanceIndices =
                *type == kGeometryInstanceType ? geometryIndices
                                               : controllerGeometryIndices;
            const auto geometry =
                instanceIndices.find(geometryUrl->substr(1));
            if (geometry == instanceIndices.end() ||
                geometry->second >= geometries.size()) {
                // Some animation-only resources retain scene instances for a
                // geometry library stripped into another BDAE. They do not
                // contribute renderable geometry in this file.
                continue;
            }
            sceneNodes[static_cast<std::size_t>(currentNodeIndex)]
                .geometryIndices.push_back(geometry->second);
            ColladaGeometry instance = geometries[geometry->second];
            instance.name = *name;
            transformGeometry(instance, worldTransform);
            output.push_back(std::move(instance));
        }

        if (*childCount != 0) {
            Result result = parseSceneNodes(
                view, *childArray, *childCount, worldTransform,
                geometryIndices, controllerGeometryIndices, geometries,
                output, sceneNodes,
                currentNodeIndex, depth + 1);
            if (!result) {
                return result;
            }
        }
    }
    return Result::success();
}

Result parseVisualScenes(const BinaryView& view, std::uint32_t rootOffset,
                         const std::vector<ColladaGeometry>& geometries,
                         const std::vector<ColladaSkin>& skins,
                         const std::vector<ColladaMorph>& morphs,
                         std::vector<ColladaGeometry>& output,
                         std::vector<ColladaSceneNode>& sceneNodes) {
    const auto sceneCount =
        view.integer<std::uint32_t>(rootOffset + kVisualSceneCountOffset);
    const auto sceneArray =
        view.integer<std::uint32_t>(rootOffset + kVisualSceneArrayOffset);
    if (!sceneCount || !sceneArray ||
        (*sceneCount != 0 &&
         !view.contains(*sceneArray,
                        static_cast<std::uint64_t>(*sceneCount) *
                            kVisualSceneSize))) {
        return Result::failure("BDAE visual-scene library is invalid");
    }
    if (*sceneCount == 0) {
        output = geometries;
        return Result::success();
    }

    std::unordered_map<std::string, std::uint32_t> geometryIndices;
    geometryIndices.reserve(geometries.size());
    for (std::uint32_t index = 0; index < geometries.size(); ++index) {
        geometryIndices.emplace(geometries[index].id, index);
    }
    std::unordered_map<std::string, std::uint32_t>
        controllerGeometryIndices;
    controllerGeometryIndices.reserve(skins.size() + morphs.size());
    const auto addControllerGeometry =
        [&geometryIndices, &controllerGeometryIndices](
            const std::string& controllerId,
            const std::string& geometryId) {
            const auto geometry = geometryIndices.find(geometryId);
            if (geometry != geometryIndices.end()) {
                controllerGeometryIndices.emplace(controllerId,
                                                  geometry->second);
            }
        };
    for (const ColladaSkin& skin : skins) {
        addControllerGeometry(skin.controllerId, skin.geometryId);
    }
    for (const ColladaMorph& morph : morphs) {
        addControllerGeometry(morph.controllerId, morph.sourceGeometryId);
    }
    const AffineTransform identity;
    for (std::uint32_t sceneIndex = 0; sceneIndex < *sceneCount; ++sceneIndex) {
        const std::uint32_t sceneOffset =
            *sceneArray + sceneIndex * kVisualSceneSize;
        const auto nodeCount = view.integer<std::uint32_t>(sceneOffset + 8);
        const auto nodeArray = view.integer<std::uint32_t>(sceneOffset + 12);
        if (!nodeCount || !nodeArray) {
            return Result::failure("BDAE visual scene is truncated");
        }
        Result result = parseSceneNodes(
            view, *nodeArray, *nodeCount, identity, geometryIndices,
            controllerGeometryIndices, geometries, output, sceneNodes, -1, 0);
        if (!result) {
            output.clear();
            return result;
        }
    }
    if (output.empty()) {
        output = geometries;
    }
    return Result::success();
}

Result parseMorphControllers(const BinaryView& view,
                             std::uint32_t rootOffset,
                             std::uint32_t geometryCount,
                             std::vector<ColladaMorph>& output) {
    const auto controllerCount =
        view.integer<std::uint32_t>(rootOffset + kControllerCountOffset);
    const auto controllerArray =
        view.integer<std::uint32_t>(rootOffset + kControllerArrayOffset);
    if (!controllerCount || !controllerArray ||
        (*controllerCount != 0 &&
         !view.contains(*controllerArray,
                        static_cast<std::uint64_t>(*controllerCount) *
                            kControllerSize))) {
        return Result::failure("BDAE controller library is invalid");
    }
    for (std::uint32_t controllerIndex = 0;
         controllerIndex < *controllerCount; ++controllerIndex) {
        const std::uint32_t controllerOffset =
            *controllerArray + controllerIndex * kControllerSize;
        const auto type = view.integer<std::uint32_t>(controllerOffset);
        const auto idOffset =
            view.integer<std::uint32_t>(controllerOffset + 4);
        const auto morphOffset =
            view.integer<std::uint32_t>(controllerOffset + 8);
        if (!type || !idOffset || !morphOffset) {
            return Result::failure("BDAE controller record is truncated");
        }
        if (*type != kMorphControllerType) {
            continue;
        }
        const auto controllerId = view.string(*idOffset);
        const auto sourceUrlOffset =
            view.integer<std::uint32_t>(*morphOffset);
        const auto targetCount =
            view.integer<std::uint32_t>(*morphOffset + 0x10);
        const auto targetIndices =
            view.integer<std::uint32_t>(*morphOffset + 0x14);
        const auto weightCount =
            view.integer<std::uint32_t>(*morphOffset + 0x18);
        const auto weights =
            view.integer<std::uint32_t>(*morphOffset + 0x1c);
        if (!controllerId || !sourceUrlOffset || !targetCount ||
            !targetIndices || !weightCount || !weights ||
            *targetCount != *weightCount ||
            (*targetCount != 0 &&
             (!view.contains(*targetIndices,
                             static_cast<std::uint64_t>(*targetCount) * 4) ||
              !view.contains(*weights,
                             static_cast<std::uint64_t>(*weightCount) * 4)))) {
            return Result::failure("BDAE morph payload is invalid");
        }
        const auto sourceUrl = view.string(*sourceUrlOffset);
        if (!sourceUrl || sourceUrl->empty() || sourceUrl->front() != '#') {
            return Result::failure("BDAE morph source URL is invalid");
        }
        ColladaMorph morph;
        morph.controllerId = *controllerId;
        morph.sourceGeometryId = sourceUrl->substr(1);
        morph.targetGeometryIndices.reserve(*targetCount);
        morph.weights.reserve(*weightCount);
        for (std::uint32_t target = 0; target < *targetCount; ++target) {
            const auto geometryIndex =
                view.integer<std::uint32_t>(*targetIndices + target * 4);
            const auto weight = view.floating(*weights + target * 4);
            if (!geometryIndex || *geometryIndex >= geometryCount || !weight ||
                !std::isfinite(*weight)) {
                return Result::failure("BDAE morph target is invalid");
            }
            morph.targetGeometryIndices.push_back(*geometryIndex);
            morph.weights.push_back(*weight);
        }
        output.push_back(std::move(morph));
    }
    return Result::success();
}

Result parseSkinControllers(const BinaryView& view, std::uint32_t rootOffset,
                            std::vector<ColladaSkin>& output) {
    const auto controllerCount =
        view.integer<std::uint32_t>(rootOffset + kControllerCountOffset);
    const auto controllerArray =
        view.integer<std::uint32_t>(rootOffset + kControllerArrayOffset);
    if (!controllerCount || !controllerArray ||
        (*controllerCount != 0 &&
         !view.contains(*controllerArray,
                        static_cast<std::uint64_t>(*controllerCount) *
                            kControllerSize))) {
        return Result::failure("BDAE controller library is invalid");
    }

    for (std::uint32_t controllerIndex = 0;
         controllerIndex < *controllerCount; ++controllerIndex) {
        const std::uint32_t controllerOffset =
            *controllerArray + controllerIndex * kControllerSize;
        const auto type = view.integer<std::uint32_t>(controllerOffset);
        const auto idOffset = view.integer<std::uint32_t>(controllerOffset + 4);
        const auto skinOffset = view.integer<std::uint32_t>(controllerOffset + 8);
        if (!type || !idOffset || !skinOffset) {
            return Result::failure("BDAE controller record is truncated");
        }
        if (*type != kSkinControllerType) {
            continue;
        }
        const auto controllerId = view.string(*idOffset);
        const auto geometryUrlOffset = view.integer<std::uint32_t>(*skinOffset);
        const auto jointCount = view.integer<std::uint32_t>(*skinOffset + 0x44);
        const auto jointNamesOffset =
            view.integer<std::uint32_t>(*skinOffset + 0x48);
        const auto inverseBindOffset =
            view.integer<std::uint32_t>(*skinOffset + 0x50);
        const auto weightCount = view.integer<std::uint32_t>(*skinOffset + 0x54);
        const auto weightsOffset = view.integer<std::uint32_t>(*skinOffset + 0x58);
        const auto vertexCount = view.integer<std::uint32_t>(*skinOffset + 0x5c);
        const auto vertexInfluenceCountsOffset =
            view.integer<std::uint32_t>(*skinOffset + 0x60);
        const auto influenceIndexCount =
            view.integer<std::uint32_t>(*skinOffset + 0x64);
        const auto influencesOffset =
            view.integer<std::uint32_t>(*skinOffset + 0x68);
        if (!controllerId || !geometryUrlOffset || !jointCount ||
            !jointNamesOffset || !inverseBindOffset || !weightCount ||
            !weightsOffset || !vertexCount || !vertexInfluenceCountsOffset ||
            !influenceIndexCount || !influencesOffset || *jointCount == 0 ||
            *weightCount == 0 || *vertexCount == 0 ||
            (*influenceIndexCount & 1U) != 0 ||
            !view.contains(*jointNamesOffset,
                           static_cast<std::uint64_t>(*jointCount) * 4) ||
            !view.contains(*inverseBindOffset,
                           static_cast<std::uint64_t>(*jointCount) * 64) ||
            !view.contains(*weightsOffset,
                           static_cast<std::uint64_t>(*weightCount) * 4) ||
            !view.contains(*vertexInfluenceCountsOffset, *vertexCount) ||
            !view.contains(*influencesOffset,
                           static_cast<std::uint64_t>(*influenceIndexCount) *
                               sizeof(std::uint16_t)) ||
            !view.contains(*skinOffset + 4, 64)) {
            return Result::failure("BDAE skin payload is invalid");
        }
        const auto geometryUrl = view.string(*geometryUrlOffset);
        if (!geometryUrl || geometryUrl->empty() ||
            geometryUrl->front() != '#') {
            return Result::failure("BDAE skin geometry URL is invalid");
        }

        ColladaSkin skin;
        skin.controllerId = *controllerId;
        skin.geometryId = geometryUrl->substr(1);
        for (std::uint32_t component = 0; component < 16; ++component) {
            const auto value = view.floating(*skinOffset + 4 + component * 4);
            if (!value || !std::isfinite(*value)) {
                return Result::failure("BDAE bind-shape matrix is invalid");
            }
            skin.bindShapeMatrix[component] = *value;
        }
        skin.jointNames.reserve(*jointCount);
        skin.inverseBindMatrices.resize(*jointCount);
        for (std::uint32_t joint = 0; joint < *jointCount; ++joint) {
            const auto nameOffset =
                view.integer<std::uint32_t>(*jointNamesOffset + joint * 4);
            if (!nameOffset) {
                return Result::failure("BDAE skin joint name is truncated");
            }
            const auto name = view.string(*nameOffset);
            if (!name) {
                return Result::failure("BDAE skin joint name is invalid");
            }
            skin.jointNames.push_back(*name);
            for (std::uint32_t component = 0; component < 16; ++component) {
                const auto value = view.floating(
                    *inverseBindOffset + joint * 64 + component * 4);
                if (!value || !std::isfinite(*value)) {
                    return Result::failure(
                        "BDAE inverse-bind matrix is invalid");
                }
                skin.inverseBindMatrices[joint][component] = *value;
            }
        }

        std::vector<float> weights;
        weights.reserve(*weightCount);
        for (std::uint32_t weight = 0; weight < *weightCount; ++weight) {
            const auto value = view.floating(*weightsOffset + weight * 4);
            if (!value || !std::isfinite(*value) || *value < 0.0F) {
                return Result::failure("BDAE skin weight is invalid");
            }
            weights.push_back(*value);
        }
        skin.vertexInfluences.reserve(*vertexCount);
        const std::uint32_t influenceCount = *influenceIndexCount / 2;
        std::uint32_t influenceCursor = 0;
        for (std::uint32_t vertex = 0; vertex < *vertexCount; ++vertex) {
            const auto count = view.integer<std::uint8_t>(
                *vertexInfluenceCountsOffset + vertex);
            if (!count || influenceCursor + *count > influenceCount) {
                return Result::failure(
                    "BDAE vertex influence range is invalid");
            }
            std::vector<ColladaVertexInfluence> influences;
            influences.reserve(*count);
            float totalWeight = 0.0F;
            for (std::uint32_t influence = 0; influence < *count; ++influence) {
                const std::uint32_t pairOffset =
                    *influencesOffset + (influenceCursor + influence) * 4;
                const auto jointIndex = view.integer<std::uint16_t>(pairOffset);
                const auto weightIndex =
                    view.integer<std::uint16_t>(pairOffset + 2);
                if (!jointIndex || !weightIndex || *jointIndex >= *jointCount ||
                    *weightIndex >= weights.size()) {
                    return Result::failure("BDAE vertex influence is invalid");
                }
                totalWeight += weights[*weightIndex];
                influences.push_back({*jointIndex, weights[*weightIndex]});
            }
            if (influences.empty() ||
                totalWeight <= std::numeric_limits<float>::epsilon()) {
                return Result::failure("BDAE vertex has no skin weight");
            }
            for (ColladaVertexInfluence& influence : influences) {
                influence.weight /= totalWeight;
            }
            influenceCursor += *count;
            skin.vertexInfluences.push_back(std::move(influences));
        }
        if (influenceCursor != influenceCount) {
            return Result::failure("BDAE skin influence stream has trailing data");
        }
        output.push_back(std::move(skin));
    }
    return Result::success();
}

} // namespace

Result ColladaMeshFile::load(std::span<const std::byte> bytes) {
    images_.clear();
    materials_.clear();
    geometries_.clear();
    sceneGeometries_.clear();
    skins_.clear();
    morphs_.clear();
    sceneNodes_.clear();
    Result result = resource_.load(bytes);
    if (!result) {
        return result;
    }

    const auto rootOffset = resource_.resolvePointer(kColladaRootPointerField);
    if (!rootOffset) {
        return Result::failure("BDAE file has no SCollada root pointer");
    }
    const BinaryView view(resource_.bytes());
    result = parseImageLibrary(view, *rootOffset, images_);
    if (!result) {
        return result;
    }
    result = parseMaterialLibrary(view, *rootOffset,
                                  images_, materials_);
    if (!result) {
        images_.clear();
        return result;
    }
    const auto geometryCount =
        view.integer<std::uint32_t>(*rootOffset + kGeometryCountOffset);
    const auto geometryArray =
        view.integer<std::uint32_t>(*rootOffset + kGeometryArrayOffset);
    if (!geometryCount || !geometryArray ||
        !view.contains(*geometryArray,
                       static_cast<std::uint64_t>(*geometryCount) * kGeometrySize)) {
        return Result::failure("BDAE geometry library is invalid");
    }

    geometries_.reserve(*geometryCount);
    for (std::uint32_t index = 0; index < *geometryCount; ++index) {
        ColladaGeometry geometry;
        result = parseGeometry(view, *geometryArray + index * kGeometrySize,
                               geometry);
        if (!result) {
            geometries_.clear();
            return result;
        }
        geometries_.push_back(std::move(geometry));
    }
    result = parseSkinControllers(view, *rootOffset, skins_);
    if (!result) {
        skins_.clear();
        return result;
    }
    result = parseMorphControllers(view, *rootOffset, *geometryCount,
                                   morphs_);
    if (!result) {
        morphs_.clear();
        return result;
    }
    result = parseVisualScenes(view, *rootOffset, geometries_, skins_, morphs_,
                               sceneGeometries_, sceneNodes_);
    if (!result) {
        sceneGeometries_.clear();
        return result;
    }
    return Result::success();
}

const ColladaMaterial* ColladaMeshFile::findMaterial(
    std::string_view name) const noexcept {
    const auto iterator = std::find_if(
        materials_.begin(), materials_.end(),
        [name](const ColladaMaterial& material) {
            return material.id == name || material.name == name;
        });
    return iterator == materials_.end() ? nullptr : &*iterator;
}

const ColladaSceneNode* ColladaMeshFile::findSceneNodeByScopeId(
    std::string_view scopeId) const noexcept {
    const auto match = std::find_if(
        sceneNodes_.begin(), sceneNodes_.end(),
        [scopeId](const ColladaSceneNode& node) {
            return node.scopeId == scopeId;
        });
    return match == sceneNodes_.end() ? nullptr : &*match;
}

const ColladaSceneNode* ColladaMeshFile::findSceneNodeById(
    std::string_view id) const noexcept {
    const auto match = std::find_if(
        sceneNodes_.begin(), sceneNodes_.end(),
        [id](const ColladaSceneNode& node) { return node.id == id; });
    return match == sceneNodes_.end() ? nullptr : &*match;
}

} // namespace usm::assets
