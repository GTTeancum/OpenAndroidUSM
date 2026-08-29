#pragma once

#include "audio/IAudioSystem.hpp"
#include "audio/OggAudio.hpp"

#include <atomic>
#include <list>
#include <memory>
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
    [[nodiscard]] Result play(const PcmAudio& audio, bool loop = false);
    void update();

private:
    class VoiceCallback final : public IXAudio2VoiceCallback {
    public:
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override { finished.store(true); }
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {
            finished.store(true);
        }

        std::atomic_bool finished{false};
    };

    struct ActiveVoice final {
        IXAudio2SourceVoice* voice{};
        std::unique_ptr<VoiceCallback> callback;
        std::vector<std::int16_t> samples;
    };

    void destroyActiveVoices() noexcept;

    Microsoft::WRL::ComPtr<IXAudio2> engine_;
    IXAudio2MasteringVoice* masteringVoice_{};
    std::list<ActiveVoice> activeVoices_;
};

} // namespace usm::audio
