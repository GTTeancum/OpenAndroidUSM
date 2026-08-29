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
    [[nodiscard]] const ColladaMaterial* findMaterial(
        std::string_view name) const noexcept;

private:
    BresFile resource_;
    std::vector<ColladaImage> images_;
    std::vector<ColladaMaterial> materials_;
    std::vector<ColladaGeometry> geometries_;
    std::vector<ColladaGeometry> sceneGeometries_;
};

} // namespace usm::assets
