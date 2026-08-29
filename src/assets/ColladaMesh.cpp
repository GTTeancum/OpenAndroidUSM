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
constexpr std::uint32_t kGeometryInstanceType = 3;

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
    // Row-major 3x3 linear transform. Points use linear * point + translation.
    std::array<float, 9> linear{1.0F, 0.0F, 0.0F,
                                0.0F, 1.0F, 0.0F,
                                0.0F, 0.0F, 1.0F};
    Vector3 translation{};
};

Vector3 multiply(const std::array<float, 9>& matrix,
                 const Vector3& value) noexcept {
    return {
        matrix[0] * value.x + matrix[1] * value.y + matrix[2] * value.z,
        matrix[3] * value.x + matrix[4] * value.y + matrix[5] * value.z,
        matrix[6] * value.x + matrix[7] * value.y + matrix[8] * value.z,
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
        multiply(parent.linear, local.linear),
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

    const float length = std::sqrt(*x * *x + *y * *y + *z * *z + *w * *w);
    if (length <= std::numeric_limits<float>::epsilon()) {
        return std::nullopt;
    }
    const float qx = *x / length;
    const float qy = *y / length;
    const float qz = *z / length;
    const float qw = *w / length;
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
    result.linear = {
        (1.0F - 2.0F * (yy + zz)) * scale->x,
        (2.0F * (xy - wz)) * scale->y,
        (2.0F * (xz + wy)) * scale->z,
        (2.0F * (xy + wz)) * scale->x,
        (1.0F - 2.0F * (xx + zz)) * scale->y,
        (2.0F * (yz - wx)) * scale->z,
        (2.0F * (xz - wy)) * scale->x,
        (2.0F * (yz + wx)) * scale->y,
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
            colorComponent = static_cast<std::int8_t>(
                *view.integer<std::uint8_t>(bufferOffset + 22));
        }

        ColladaMeshBuffer buffer;
        buffer.primitive = *primitive;
        buffer.materialName = *material;
        buffer.firstVertex = *firstVertex;
        buffer.lastVertex = *lastVertex;
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
                            std::uint32_t imageCount,
                            std::vector<ColladaMaterial>& output) {
    const auto effectCount =
        view.integer<std::uint32_t>(rootOffset + kEffectCountOffset);
    const auto effectArray =
        view.integer<std::uint32_t>(rootOffset + kEffectArrayOffset);
    const auto materialCount =
        view.integer<std::uint32_t>(rootOffset + kMaterialCountOffset);
    const auto materialArray =
        view.integer<std::uint32_t>(rootOffset + kMaterialArrayOffset);
    if (!effectCount || !effectArray || !materialCount || !materialArray) {
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

        ColladaMaterial material{*id, *name, *effectId, std::nullopt};
        // SEffect's final pointer resolves to its diffuse SImage index for
        // textured effects. Color-only effects use a different payload.
        const std::uint32_t effect = *effectArray + *effectIndex * kEffectSize;
        const auto imageIndexPointer = view.integer<std::uint32_t>(effect + 0x58);
        if (imageIndexPointer) {
            const auto imageIndex = view.integer<std::uint32_t>(*imageIndexPointer);
            if (imageIndex && *imageIndex < imageCount) {
                material.diffuseImageIndex = *imageIndex;
            }
        }
        output.push_back(std::move(material));
    }
    return Result::success();
}

Result parseSceneNodes(
    const BinaryView& view, std::uint32_t nodeArray, std::uint32_t nodeCount,
    const AffineTransform& parentTransform,
    const std::unordered_map<std::string, std::uint32_t>& geometryIndices,
    const std::vector<ColladaGeometry>& geometries,
    std::vector<ColladaGeometry>& output, std::uint32_t depth) {
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
        const auto localTransform = readNodeTransform(view, nodeOffset);
        const auto childCount = view.integer<std::uint32_t>(nodeOffset + 0x38);
        const auto childArray = view.integer<std::uint32_t>(nodeOffset + 0x3c);
        const auto instanceCount = view.integer<std::uint32_t>(nodeOffset + 0x40);
        const auto instanceArray = view.integer<std::uint32_t>(nodeOffset + 0x44);
        if (!idOffset || !nameOffset || !localTransform || !childCount ||
            !childArray || !instanceCount || !instanceArray) {
            return Result::failure("BDAE visual-scene node is truncated");
        }
        const auto id = view.string(*idOffset);
        const auto name = view.string(*nameOffset);
        if (!id || !name ||
            (*instanceCount != 0 &&
             !view.contains(*instanceArray,
                            static_cast<std::uint64_t>(*instanceCount) *
                                kSceneInstanceSize))) {
            return Result::failure("BDAE visual-scene node metadata is invalid");
        }

        const AffineTransform worldTransform =
            combine(parentTransform, *localTransform);
        for (std::uint32_t instanceIndex = 0;
             instanceIndex < *instanceCount; ++instanceIndex) {
            const std::uint32_t instanceOffset =
                *instanceArray + instanceIndex * kSceneInstanceSize;
            const auto type = view.integer<std::uint32_t>(instanceOffset);
            const auto payload = view.integer<std::uint32_t>(instanceOffset + 4);
            if (!type || !payload) {
                return Result::failure("BDAE scene instance is truncated");
            }
            if (*type != kGeometryInstanceType) {
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
            const auto geometry = geometryIndices.find(geometryUrl->substr(1));
            if (geometry == geometryIndices.end() ||
                geometry->second >= geometries.size()) {
                // Some animation-only resources retain scene instances for a
                // geometry library stripped into another BDAE. They do not
                // contribute renderable geometry in this file.
                continue;
            }
            ColladaGeometry instance = geometries[geometry->second];
            instance.name = *name;
            transformGeometry(instance, worldTransform);
            output.push_back(std::move(instance));
        }

        if (*childCount != 0) {
            Result result = parseSceneNodes(
                view, *childArray, *childCount, worldTransform,
                geometryIndices, geometries, output, depth + 1);
            if (!result) {
                return result;
            }
        }
    }
    return Result::success();
}

Result parseVisualScenes(const BinaryView& view, std::uint32_t rootOffset,
                         const std::vector<ColladaGeometry>& geometries,
                         std::vector<ColladaGeometry>& output) {
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
            geometries, output, 0);
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

} // namespace

Result ColladaMeshFile::load(std::span<const std::byte> bytes) {
    images_.clear();
    materials_.clear();
    geometries_.clear();
    sceneGeometries_.clear();
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
                                  static_cast<std::uint32_t>(images_.size()),
                                  materials_);
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
    result = parseVisualScenes(view, *rootOffset, geometries_, sceneGeometries_);
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

} // namespace usm::assets
