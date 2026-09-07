#pragma once

#include "assets/BresFile.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <optional>
#include <vector>

namespace usm::assets {

struct AxisAlignedBounds {
    Vector3 minimum;
    Vector3 maximum;
};

struct ColladaVertex {
    Vector3 position;
    Vector3 normal;
    std::array<float, 2> textureCoordinate{};
    std::uint32_t color{0xffffffff};
    std::array<float, 2> secondaryTextureCoordinate{};
};

enum class ColladaPrimitive {
    Triangles,
    TriangleStrip,
    Lines,
    LineStrip,
    LineLoop,
};

struct ColladaMeshBuffer {
    ColladaPrimitive primitive{ColladaPrimitive::Triangles};
    std::string materialName;
    std::vector<std::uint16_t> indices;
    std::uint16_t firstVertex{};
    std::uint16_t lastVertex{};
    bool usesSecondaryTextureCoordinates{};
    AxisAlignedBounds bounds;
};

struct ColladaGeometry {
    std::string id;
    std::string name;
    std::vector<ColladaVertex> vertices;
    std::vector<ColladaMeshBuffer> meshBuffers;
    AxisAlignedBounds bounds;
};

struct ColladaImage {
    std::string id;
    std::string name;
    std::string sourcePath;
};

struct ColladaMaterial {
    std::string id;
    std::string name;
    std::string effectId;
    std::optional<std::uint32_t> diffuseImageIndex;
    std::optional<std::uint32_t> secondaryImageIndex;
    std::optional<std::uint32_t> lightmapImageIndex;
    std::uint32_t secondaryTextureMode{};
    bool additiveBlend{};
    bool backFaceCulling{};
    bool frontFaceCulling{};
    bool transparentAlphaChannel{};
    // SEffect ambient is copied byte-for-byte into SMaterial::AmbientColor.
    // The game's custom combat-effect renderers use it as GL_TEXTURE_ENV_COLOR.
    std::array<float, 4> ambientColor{};
};

struct ColladaVertexInfluence {
    std::uint16_t jointIndex{};
    float weight{};
};

struct ColladaSkin {
    std::string controllerId;
    std::string geometryId;
    std::array<float, 16> bindShapeMatrix{};
    std::vector<std::string> jointNames;
    std::vector<std::array<float, 16>> inverseBindMatrices;
    std::vector<std::vector<ColladaVertexInfluence>> vertexInfluences;
};

struct ColladaMorph {
    std::string controllerId;
    std::string sourceGeometryId;
    std::vector<std::uint32_t> targetGeometryIndices;
    std::vector<float> weights;
};

struct ColladaSceneNode {
    std::string id;
    std::string name;
    std::string scopeId;
    std::int32_t parentIndex{-1};
    Vector3 position;
    Quaternion rotation;
    Vector3 scale{1.0F, 1.0F, 1.0F};
    std::array<float, 9> worldLinear{1.0F, 0.0F, 0.0F,
                                     0.0F, 1.0F, 0.0F,
                                     0.0F, 0.0F, 1.0F};
    Vector3 worldPosition;
    std::vector<std::uint32_t> geometryIndices;
};

// Typed native view of the SGeometry/SMesh/SMeshBuffer graph consumed by
// CColladaMesh and CColladaMeshBuffer in the original engine.
class ColladaMeshFile final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const std::vector<ColladaGeometry>& geometries() const noexcept {
        return geometries_;
    }
    // Geometry instances after applying the SVisualScene/SNode hierarchy.
    // The raw geometry library remains available through geometries().
    [[nodiscard]] const std::vector<ColladaGeometry>& sceneGeometries() const
        noexcept {
        return sceneGeometries_;
    }
    [[nodiscard]] const std::vector<ColladaImage>& images() const noexcept {
        return images_;
    }
    [[nodiscard]] const std::vector<ColladaMaterial>& materials() const noexcept {
        return materials_;
    }
    [[nodiscard]] const std::vector<ColladaSkin>& skins() const noexcept {
        return skins_;
    }
    [[nodiscard]] const std::vector<ColladaMorph>& morphs() const noexcept {
        return morphs_;
    }
    [[nodiscard]] const std::vector<ColladaSceneNode>& sceneNodes() const
        noexcept {
        return sceneNodes_;
    }
    [[nodiscard]] const ColladaSceneNode* findSceneNodeByScopeId(
        std::string_view scopeId) const noexcept;
    [[nodiscard]] const ColladaSceneNode* findSceneNodeById(
        std::string_view id) const noexcept;
    [[nodiscard]] const ColladaMaterial* findMaterial(
        std::string_view name) const noexcept;

private:
    BresFile resource_;
    std::vector<ColladaImage> images_;
    std::vector<ColladaMaterial> materials_;
    std::vector<ColladaGeometry> geometries_;
    std::vector<ColladaGeometry> sceneGeometries_;
    std::vector<ColladaSkin> skins_;
    std::vector<ColladaMorph> morphs_;
    std::vector<ColladaSceneNode> sceneNodes_;
};

} // namespace usm::assets
