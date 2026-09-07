#pragma once

#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "core/Result.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace usm::assets {

// Evaluates a complete Collada pose. Skin controllers reconstruct
// CColladaSkinnedMesh::prepareSkeletonMtxCache/skin; rigid geometry instances
// follow their animated visual-scene nodes. Output geometry retains source
// topology and materials with animated positions and normals.
[[nodiscard]] Result evaluateColladaPose(
    const ColladaMeshFile& mesh, const ColladaAnimationFile& animation,
    std::uint32_t timestampMilliseconds,
    std::vector<ColladaGeometry>& output);

// Evaluates the same animated scene hierarchy used by skinning and returns a
// named node's model-space transformation. CBehaviorRangeAttack's original
// InitWeaponParent (0x003c0144) resolves projectile attachments by node name
// (not by a hand-authored offset), so gameplay attachment reconstruction must
// consume this hierarchy rather than approximate from collision dimensions.
[[nodiscard]] Result evaluateColladaSceneNodeTransform(
    const ColladaMeshFile& mesh, const ColladaAnimationFile& animation,
    std::uint32_t timestampMilliseconds, std::string_view nodeName,
    std::array<float, 16>& output);

} // namespace usm::assets
