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
    }
    return 0;
}
