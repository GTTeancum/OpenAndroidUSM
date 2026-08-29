#include "assets/BresFile.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "filesystem/GbmpArchive.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <cassert>
#include <filesystem>
#include <vector>

int main() {
    const auto success = usm::Result::success();
    assert(static_cast<bool>(success));

    const auto failure = usm::Result::failure("expected failure");
    assert(!static_cast<bool>(failure));
    assert(failure.message() == "expected failure");

    using namespace usm::reconstructed;
    XperiaKeyRouter router;
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, true},
                 InputContext::Gameplay);
    assert(router.state().jump.pressed);
    assert(router.state().jump.held);
    assert(router.state().quickTimeEvent.pressed);

    router.beginFrame();
    assert(!router.state().jump.pressed);
    assert(router.state().jump.held);
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, false},
                 InputContext::Gameplay);
    assert(router.state().jump.released);
    assert(!router.state().jump.held);

    router.beginFrame();
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, true},
                 InputContext::UpgradeMenu);
    assert(router.state().upgrade.pressed);
    assert(router.state().upgradeProceed.pressed);
    assert(!router.state().jump.pressed);

    const std::filesystem::path dataRoot = USM_TEST_GAME_DATA_ROOT;
    const std::filesystem::path configArchive = dataRoot / "configs.pack";
    if (std::filesystem::exists(configArchive)) {
        usm::filesystem::GbmpArchive archive;
        assert(archive.open(configArchive));
        assert(archive.entries().size() == 40);

        std::vector<std::byte> resource;
        assert(archive.read("GS_BossRushEndLevel.json", resource));
        assert(resource.size() == 5300);

        usm::filesystem::GbmpArchive levelOne;
        assert(levelOne.open(dataRoot / "levelnew_01.pack"));
        assert(levelOne.entries().size() == 250);
        assert(levelOne.find("meshes_bin/camera_Lv1_beforeboss.bdae") != nullptr);

        std::vector<std::byte> meshResource;
        assert(levelOne.read("meshes_bin/geometry01.bdae", meshResource));
        usm::assets::BresFile meshFile;
        assert(meshFile.load(meshResource));
        assert(meshFile.header().fileSize == 441000);
        assert(meshFile.header().relocationCount == 4173);
        assert(meshFile.resolvePointer(0x14) == 0x20);
        assert(meshFile.resolvePointer(0x18) == 0x4154);
        assert(meshFile.resolvePointer(0x1c) == 0x8c0c);

        std::vector<std::byte> roomResource;
        assert(levelOne.read("levelnew_01_0_Room1.irr", roomResource));
        usm::assets::IrrScene roomScene;
        assert(roomScene.load(roomResource));
        assert(roomScene.nodes().size() == 60);
        assert(roomScene.nodes().front().name == "Room1");
        assert(roomScene.nodes().front().gameType == "Geometry");
        assert(roomScene.nodes().front().meshFile ==
               "meshes_bin/geometry01.bdae");
    }
    return 0;
}
