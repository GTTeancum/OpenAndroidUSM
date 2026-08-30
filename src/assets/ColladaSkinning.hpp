#pragma once

#include "assets/ColladaAnimation.hpp"
#include "assets/ColladaMesh.hpp"
#include "core/Result.hpp"

#include <cstdint>
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

} // namespace usm::assets
