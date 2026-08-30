#include "assets/ColladaSkinning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace usm::assets {
namespace {

struct Matrix4 {
    // Column-major, matching Irrlicht's CMatrix4 indexing in the preserved
    // CColladaSkinnedMesh::skin implementation. Scene-node quaternions use
    // quaternion::getMatrix_transposed before hierarchy multiplication.
    std::array<float, 16> value{1.0F, 0.0F, 0.0F, 0.0F,
                                0.0F, 1.0F, 0.0F, 0.0F,
                                0.0F, 0.0F, 1.0F, 0.0F,
                                0.0F, 0.0F, 0.0F, 1.0F};
};

Matrix4 multiply(const Matrix4& left, const Matrix4& right) noexcept {
    Matrix4 result{{}};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t component = 0; component < 4; ++component) {
                result.value[column * 4 + row] +=
                    left.value[component * 4 + row] *
                    right.value[column * 4 + component];
            }
        }
    }
    return result;
}

Matrix4 localMatrix(const Vector3& position, Quaternion rotation,
                    const Vector3& scale) noexcept {
    const float length = std::sqrt(
        rotation.x * rotation.x + rotation.y * rotation.y +
        rotation.z * rotation.z + rotation.w * rotation.w);
    if (length > std::numeric_limits<float>::epsilon()) {
        rotation.x /= length;
        rotation.y /= length;
        rotation.z /= length;
        rotation.w /= length;
    } else {
        rotation = {};
    }
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Matrix4 result;
    result.value = {
        (1.0F - 2.0F * (yy + zz)) * scale.x,
        (2.0F * (xy - wz)) * scale.x,
        (2.0F * (xz + wy)) * scale.x,
        0.0F,
        (2.0F * (xy + wz)) * scale.y,
        (1.0F - 2.0F * (xx + zz)) * scale.y,
        (2.0F * (yz - wx)) * scale.y,
        0.0F,
        (2.0F * (xz - wy)) * scale.z,
        (2.0F * (yz + wx)) * scale.z,
        (1.0F - 2.0F * (xx + yy)) * scale.z,
        0.0F,
        position.x,
        position.y,
        position.z,
        1.0F,
    };
    return result;
}

Vector3 transformPoint(const Matrix4& matrix, const Vector3& point) noexcept {
    return {
        point.x * matrix.value[0] + point.y * matrix.value[4] +
            point.z * matrix.value[8] + matrix.value[12],
        point.x * matrix.value[1] + point.y * matrix.value[5] +
            point.z * matrix.value[9] + matrix.value[13],
        point.x * matrix.value[2] + point.y * matrix.value[6] +
            point.z * matrix.value[10] + matrix.value[14],
    };
}

Vector3 transformVector(const Matrix4& matrix, const Vector3& vector) noexcept {
    return {
        vector.x * matrix.value[0] + vector.y * matrix.value[4] +
            vector.z * matrix.value[8],
        vector.x * matrix.value[1] + vector.y * matrix.value[5] +
            vector.z * matrix.value[9],
        vector.x * matrix.value[2] + vector.y * matrix.value[6] +
            vector.z * matrix.value[10],
    };
}

void updateBounds(ColladaGeometry& geometry) {
    if (geometry.vertices.empty()) {
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

void transformGeometry(ColladaGeometry& geometry, const Matrix4& transform) {
    for (ColladaVertex& vertex : geometry.vertices) {
        vertex.position = transformPoint(transform, vertex.position);
        vertex.normal = transformVector(transform, vertex.normal);
        const float length = std::sqrt(
            vertex.normal.x * vertex.normal.x +
            vertex.normal.y * vertex.normal.y +
            vertex.normal.z * vertex.normal.z);
        if (length > std::numeric_limits<float>::epsilon()) {
            vertex.normal.x /= length;
            vertex.normal.y /= length;
            vertex.normal.z /= length;
        }
    }
    updateBounds(geometry);
}

const ColladaAnimationTrack* findTrack(
    const ColladaAnimationFile& animation, std::string_view nodeId,
    ColladaAnimationProperty property) noexcept {
    const auto match = std::find_if(
        animation.tracks().begin(), animation.tracks().end(),
        [nodeId, property](const ColladaAnimationTrack& track) {
            return track.targetNode == nodeId && track.property == property;
        });
    return match == animation.tracks().end() ? nullptr : &*match;
}

Quaternion withAnimatedAngle(Quaternion base, float angle) noexcept {
    const float vectorLength =
        std::sqrt(base.x * base.x + base.y * base.y + base.z * base.z);
    Vector3 axis{0.0F, 1.0F, 0.0F};
    if (vectorLength > 1.0e-6F) {
        axis = {base.x / vectorLength, base.y / vectorLength,
                base.z / vectorLength};
    }
    const float halfAngle = angle * 0.5F;
    const float sine = std::sin(halfAngle);
    return {axis.x * sine, axis.y * sine, axis.z * sine,
            std::cos(halfAngle)};
}

Result buildWorldMatrices(const ColladaMeshFile& mesh,
                          const ColladaAnimationFile& animation,
                          std::uint32_t timestampMilliseconds,
                          std::vector<Matrix4>& worldMatrices) {
    worldMatrices.clear();
    worldMatrices.reserve(mesh.sceneNodes().size());
    for (std::size_t nodeIndex = 0; nodeIndex < mesh.sceneNodes().size();
         ++nodeIndex) {
        const ColladaSceneNode& node = mesh.sceneNodes()[nodeIndex];
        Vector3 position = node.position;
        Quaternion rotation = node.rotation;
        if (const ColladaAnimationTrack* translation = findTrack(
                animation, node.id, ColladaAnimationProperty::Translation)) {
            const ColladaAnimationSample sample =
                translation->sample(timestampMilliseconds);
            position = {sample.value[0], sample.value[1], sample.value[2]};
        }
        for (const auto [property, component] :
             {std::pair{ColladaAnimationProperty::TranslationX, 0U},
              std::pair{ColladaAnimationProperty::TranslationY, 1U},
              std::pair{ColladaAnimationProperty::TranslationZ, 2U}}) {
            if (const ColladaAnimationTrack* translation =
                    findTrack(animation, node.id, property)) {
                const ColladaAnimationSample sample =
                    translation->sample(timestampMilliseconds);
                (&position.x)[component] = sample.value[0];
            }
        }
        if (const ColladaAnimationTrack* rotationTrack = findTrack(
                animation, node.id, ColladaAnimationProperty::Rotation)) {
            const ColladaAnimationSample sample =
                rotationTrack->sample(timestampMilliseconds);
            rotation = {sample.value[0], sample.value[1], sample.value[2],
                        sample.value[3]};
        } else if (const ColladaAnimationTrack* angleTrack = findTrack(
                       animation, node.id,
                       ColladaAnimationProperty::RotationAngle)) {
            // CQuaternionAngleEx::getKeyBasedValueEx (0x004190e0) extracts
            // the axis from the node's existing quaternion and rebuilds it
            // from the sampled scalar angle. Its zero-axis fallback is +Y.
            rotation = withAnimatedAngle(
                rotation, angleTrack->sample(timestampMilliseconds).value[0]);
        }
        const Matrix4 local = localMatrix(position, rotation, node.scale);
        if (node.parentIndex < 0) {
            worldMatrices.push_back(local);
        } else if (static_cast<std::size_t>(node.parentIndex) <
                   worldMatrices.size()) {
            worldMatrices.push_back(
                multiply(worldMatrices[node.parentIndex], local));
        } else {
            return Result::failure("BDAE skeleton parent index is invalid");
        }
    }
    return Result::success();
}

Result buildJointMatrices(const ColladaMeshFile& mesh,
                          const std::vector<Matrix4>& worldMatrices,
                          const ColladaSkin& skin,
                          std::vector<Matrix4>& output) {

    output.clear();
    output.reserve(skin.jointNames.size());
    const Matrix4 bindShape{skin.bindShapeMatrix};
    for (std::size_t joint = 0; joint < skin.jointNames.size(); ++joint) {
        const ColladaSceneNode* node =
            mesh.findSceneNodeByScopeId(skin.jointNames[joint]);
        if (node == nullptr) {
            return Result::failure("BDAE skin joint has no scene node");
        }
        const auto nodeIterator = std::find_if(
            mesh.sceneNodes().begin(), mesh.sceneNodes().end(),
            [node](const ColladaSceneNode& candidate) {
                return &candidate == node;
            });
        const std::size_t nodeIndex = static_cast<std::size_t>(
            nodeIterator - mesh.sceneNodes().begin());
        const Matrix4 inverseBind{skin.inverseBindMatrices[joint]};
        output.push_back(multiply(
            multiply(worldMatrices[nodeIndex], inverseBind), bindShape));
    }
    return Result::success();
}

} // namespace

Result evaluateColladaPose(
    const ColladaMeshFile& mesh, const ColladaAnimationFile& animation,
    std::uint32_t timestampMilliseconds,
    std::vector<ColladaGeometry>& output) {
    std::vector<Matrix4> worldMatrices;
    Result result = buildWorldMatrices(mesh, animation, timestampMilliseconds,
                                       worldMatrices);
    if (!result) {
        output.clear();
        return result;
    }
    if (mesh.skins().empty()) {
        output.clear();
        for (std::size_t nodeIndex = 0;
             nodeIndex < mesh.sceneNodes().size(); ++nodeIndex) {
            const ColladaSceneNode& node = mesh.sceneNodes()[nodeIndex];
            for (const std::uint32_t geometryIndex : node.geometryIndices) {
                if (geometryIndex >= mesh.geometries().size()) {
                    output.clear();
                    return Result::failure(
                        "BDAE scene node geometry index is invalid");
                }
                output.push_back(mesh.geometries()[geometryIndex]);
                ColladaGeometry& geometry = output.back();
                geometry.name = node.name;
                transformGeometry(geometry, worldMatrices[nodeIndex]);
            }
        }
        if (output.empty()) {
            output = mesh.geometries();
            if (!worldMatrices.empty()) {
                for (ColladaGeometry& geometry : output) {
                    transformGeometry(geometry, worldMatrices.front());
                }
            }
        }
        return Result::success();
    }
    output.clear();
    output.reserve(mesh.skins().size());
    for (const ColladaSkin& skin : mesh.skins()) {
        const auto sourceGeometry = std::find_if(
            mesh.geometries().begin(), mesh.geometries().end(),
            [&skin](const ColladaGeometry& item) {
                return item.id == skin.geometryId;
            });
        if (sourceGeometry == mesh.geometries().end() ||
            sourceGeometry->vertices.size() != skin.vertexInfluences.size()) {
            output.clear();
            return Result::failure(
                "BDAE skin does not match its source geometry");
        }
        output.push_back(*sourceGeometry);
        ColladaGeometry& geometry = output.back();
        std::vector<Matrix4> jointMatrices;
        result = buildJointMatrices(mesh, worldMatrices, skin, jointMatrices);
        if (!result) {
            output.clear();
            return result;
        }
        const std::vector<ColladaVertex> sourceVertices = geometry.vertices;
        for (std::size_t vertexIndex = 0;
             vertexIndex < geometry.vertices.size(); ++vertexIndex) {
            Vector3 position{};
            Vector3 normal{};
            for (const ColladaVertexInfluence& influence :
                 skin.vertexInfluences[vertexIndex]) {
                const Vector3 jointPosition = transformPoint(
                    jointMatrices[influence.jointIndex],
                    sourceVertices[vertexIndex].position);
                const Vector3 jointNormal = transformVector(
                    jointMatrices[influence.jointIndex],
                    sourceVertices[vertexIndex].normal);
                position.x += jointPosition.x * influence.weight;
                position.y += jointPosition.y * influence.weight;
                position.z += jointPosition.z * influence.weight;
                normal.x += jointNormal.x * influence.weight;
                normal.y += jointNormal.y * influence.weight;
                normal.z += jointNormal.z * influence.weight;
            }
            const float normalLength = std::sqrt(
                normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            if (normalLength > std::numeric_limits<float>::epsilon()) {
                normal.x /= normalLength;
                normal.y /= normalLength;
                normal.z /= normalLength;
            }
            geometry.vertices[vertexIndex].position = position;
            geometry.vertices[vertexIndex].normal = normal;
        }
        updateBounds(geometry);
    }
    return Result::success();
}

} // namespace usm::assets
