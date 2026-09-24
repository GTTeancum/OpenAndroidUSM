#include "diagnostics/AutoplayHarness.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

} // namespace

int main() {
    try {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path() /
            "openandroidusm-autoplay-harness-tests";
        std::error_code error;
        std::filesystem::remove_all(root, error);
        error.clear();
        std::filesystem::create_directories(root, error);
        require(!error, "failed to create autoplay harness test directory");

        const std::filesystem::path script = root / "bank-up.usmauto";
        {
            std::ofstream stream(script);
            require(static_cast<bool>(stream),
                    "failed to create autoplay harness test script");
            stream << "fixed_step_ms 50\n"
                      "capture_interval_ms 0\n"
                      "max_time_ms 1000\n"
                      "start_cinematic 467\n"
                      "wait_cinematic_started 500 467\n"
                      "finish\n";
        }

        usm::diagnostics::AutoplayHarness harness;
        const usm::Result init = harness.initialize(script, root / "output");
        require(static_cast<bool>(init), init.message());

        usm::diagnostics::AutoplaySnapshot snapshot;
        snapshot.gameplayActive = true;
        snapshot.controlsEnabled = true;

        const auto start = harness.update(snapshot);
        require(start.cinematicStartRequests.size() == 1,
                "start_cinematic did not emit exactly one request");
        require(start.cinematicStartRequests.front() == 467,
                "start_cinematic emitted the wrong cinematic ID");
        require(!start.teleport.has_value(),
                "authored bank-up command unexpectedly emitted a teleport");

        harness.notifyCinematicStarted(467);
        snapshot.realTimeMilliseconds = 50;
        const auto wait = harness.update(snapshot);
        require(wait.cinematicStartRequests.empty(),
                "wait_cinematic_started re-emitted the start request");
        require(!wait.teleport.has_value(),
                "wait_cinematic_started unexpectedly emitted a teleport");

        snapshot.realTimeMilliseconds = 100;
        (void)harness.update(snapshot);
        require(harness.complete(), "authored cinematic script did not finish");
        require(!harness.failed(), "authored cinematic script failed");

        std::filesystem::remove_all(root, error);
        std::cout << "PASS autoplay authored-cinematic transition\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "FAIL autoplay harness: " << exception.what() << '\n';
        return 1;
    }
}
