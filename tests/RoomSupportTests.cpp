#include "game/LevelCollision.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

usm::assets::ColladaGeometry makeGroundSquare() {
    usm::assets::ColladaGeometry geometry;
    geometry.id = "support";
    geometry.name = "support";
    geometry.vertices = {
        {{0.0F, 0.0F, 0.0F}},
        {{100.0F, 0.0F, 0.0F}},
        {{0.0F, 100.0F, 0.0F}},
        {{100.0F, 100.0F, 0.0F}},
    };
    usm::assets::ColladaMeshBuffer buffer;
    buffer.primitive = usm::assets::ColladaPrimitive::Triangles;
    buffer.indices = {0, 1, 2, 2, 1, 3};
    geometry.meshBuffers.push_back(std::move(buffer));
    return geometry;
}

} // namespace

int main() {
    try {
        const auto geometry = makeGroundSquare();
        const std::array<usm::assets::ColladaGeometry, 1> geometries{
            geometry};

        usm::game::LevelCollision roomCollision;
        const usm::Result roomBuild = roomCollision.build(geometries, 7);
        require(static_cast<bool>(roomBuild), roomBuild.message());

        float height = -999.0F;
        std::int32_t objectId = 1234;
        std::int32_t roomId = -1;
        require(roomCollision.groundHeight(
                    {25.0F, 25.0F, 10.0F}, 20.0F, 20.0F,
                    height, 0U, &objectId, &roomId),
                "synthetic moving-room ground was not found");
        require(std::abs(height) < 1.0e-4F,
                "room support height differs from authored plane");
        require(objectId == -1,
                "static room collision must not report an object support ID");
        require(roomId == 7,
                "groundHeight discarded the supporting room identity");

        height = -999.0F;
        objectId = 1234;
        roomId = 1234;
        require(!roomCollision.groundHeight(
                    {250.0F, 250.0F, 10.0F}, 20.0F, 20.0F,
                    height, 0U, &objectId, &roomId),
                "out-of-footprint support probe unexpectedly succeeded");
        require(objectId == -1 && roomId == -1,
                "failed support probe must clear both support identities");

        usm::game::LevelCollision anonymousCollision;
        const usm::Result anonymousBuild =
            anonymousCollision.build(geometries);
        require(static_cast<bool>(anonymousBuild), anonymousBuild.message());
        height = -999.0F;
        objectId = 1234;
        roomId = 1234;
        require(anonymousCollision.groundHeight(
                    {25.0F, 25.0F, 10.0F}, 20.0F, 20.0F,
                    height, 0U, &objectId, &roomId),
                "anonymous synthetic ground was not found");
        require(objectId == -1 && roomId == -1,
                "roomless collision must preserve the roomless support identity");

        std::cout << "PASS moving-room ground support identity\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL moving-room support: " << error.what() << '\n';
        return 1;
    }
}
