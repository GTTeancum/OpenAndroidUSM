#include "assets/ColladaMesh.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <optional>
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

} // namespace

Result ColladaMeshFile::load(std::span<const std::byte> bytes) {
    images_.clear();
    materials_.clear();
    geometries_.clear();
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
