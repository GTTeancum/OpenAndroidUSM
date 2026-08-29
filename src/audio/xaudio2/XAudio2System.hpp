#pragma once

#include "audio/IAudioSystem.hpp"

#include <wrl/client.h>
#include <xaudio2.h>

namespace usm::audio {

class XAudio2System final : public IAudioSystem {
public:
    XAudio2System() = default;
    XAudio2System(const XAudio2System&) = delete;
    XAudio2System& operator=(const XAudio2System&) = delete;
    ~XAudio2System() override;

    [[nodiscard]] Result initialize() override;

private:
    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* masteringVoice_{};
};

} // namespace usm::audio

