#include "audio/xaudio2/XAudio2System.hpp"

namespace usm::audio {

XAudio2System::~XAudio2System() {
    if (masteringVoice_ != nullptr) {
        masteringVoice_->DestroyVoice();
    }
}

Result XAudio2System::initialize() {
    HRESULT result = XAudio2Create(&engine_);
    if (FAILED(result)) {
        return Result::failure("XAudio2Create failed");
    }
    result = engine_->CreateMasteringVoice(&masteringVoice_);
    if (FAILED(result)) {
        return Result::failure("IXAudio2::CreateMasteringVoice failed");
    }
    return Result::success();
}

} // namespace usm::audio

