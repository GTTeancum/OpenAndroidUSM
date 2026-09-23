#include "audio/xaudio2/XAudio2System.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

} // namespace

int main() {
    try {
        usm::audio::XAudio2System audio;
        const usm::Result init = audio.initialize();
        if (!init) {
            if (std::string_view(init.message()) ==
                "IXAudio2::CreateMasteringVoice failed (HRESULT 0x80070490)") {
                std::cout << "SKIP XAudio2 backend: no default audio endpoint available\n";
                return 77;
            }
            throw std::runtime_error(std::string(init.message()));
        }

        usm::audio::PcmAudio clip;
        clip.sampleRate = 48000;
        clip.channelCount = 1;
        clip.interleavedSamples.assign(12000, 0); // 250 ms of silence.

        require(static_cast<bool>(audio.playNamed("ci.oneshot", clip, false)),
                "XAudio2 one-shot failed to start");
        require(audio.isNamedPlaying("ci.oneshot"),
                "Live one-shot was not reported as playing");
        require(!audio.isNamedPlaying("ci.missing"),
                "Unrelated event was reported as playing");

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (audio.isNamedPlaying("ci.oneshot") &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            audio.update();
        }
        require(!audio.isNamedPlaying("ci.oneshot"),
                "Completed one-shot remained marked as playing");

        require(static_cast<bool>(audio.playNamed("ci.loop", clip, true)),
                "XAudio2 loop failed to start");
        require(audio.isNamedPlaying("ci.loop"),
                "Live loop was not reported as playing");
        require(static_cast<bool>(audio.stopNamed("ci.loop")),
                "XAudio2 named stop failed");
        require(!audio.isNamedPlaying("ci.loop"),
                "Stopped voice remained marked as playing");

        std::cout << "PASS XAudio2 live playback query/stop/completion\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL XAudio2 backend: " << error.what() << '\n';
        return 1;
    }
}
