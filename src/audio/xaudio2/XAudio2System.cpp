#include "audio/xaudio2/XAudio2System.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace usm::audio {
namespace {

assets::Vector3 subtract(const assets::Vector3& first,
                         const assets::Vector3& second) noexcept {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

float length(const assets::Vector3& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y +
                     value.z * value.z);
}

assets::Vector3 normalize(const assets::Vector3& value) noexcept {
    const float magnitude = length(value);
    return magnitude > 0.0001F
               ? assets::Vector3{value.x / magnitude, value.y / magnitude,
                                 value.z / magnitude}
               : assets::Vector3{};
}

assets::Vector3 cross(const assets::Vector3& first,
                      const assets::Vector3& second) noexcept {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

} // namespace

XAudio2System::~XAudio2System() {
    destroyActiveVoices();
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

Result XAudio2System::play(const PcmAudio& audio, bool loop) {
    return playNamed({}, audio, loop);
}

Result XAudio2System::playNamed(std::string_view eventName,
                               const PcmAudio& audio, bool loop) {
    if (engine_ == nullptr || masteringVoice_ == nullptr) {
        return Result::failure("XAudio2 has not been initialized");
    }
    if (audio.sampleRate == 0 || audio.channelCount == 0 ||
        audio.interleavedSamples.empty()) {
        return Result::failure("PCM audio is empty or invalid");
    }

    update();
    activeVoices_.emplace_back();
    ActiveVoice& active = activeVoices_.back();
    active.callback = std::make_unique<VoiceCallback>();
    active.samples = audio.interleavedSamples;
    active.eventName = eventName;

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = audio.channelCount;
    format.nSamplesPerSec = audio.sampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels *
                                           format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HRESULT result = engine_->CreateSourceVoice(
        &active.voice, &format, 0, XAUDIO2_DEFAULT_FREQ_RATIO,
        active.callback.get());
    if (FAILED(result)) {
        activeVoices_.pop_back();
        return Result::failure("IXAudio2::CreateSourceVoice failed");
    }

    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(active.samples.size() *
                                             sizeof(std::int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE*>(active.samples.data());
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
    result = active.voice->SubmitSourceBuffer(&buffer);
    if (SUCCEEDED(result)) {
        result = active.voice->Start();
    }
    if (FAILED(result)) {
        active.voice->DestroyVoice();
        activeVoices_.pop_back();
        return Result::failure("Could not submit or start XAudio2 source voice");
    }
    return Result::success();
}

Result XAudio2System::playNamed3D(std::string_view eventName,
                                 const PcmAudio& audio,
                                 const SpatialSoundSource& source,
                                 bool loop) {
    const SpatialSoundMix mix =
        calculateSpatialSoundMix(listenerPosition_, listenerRight_, source);
    if (mix.culled) {
        return Result::success();
    }
    Result result = playNamed(eventName, audio, loop);
    if (!result) {
        return result;
    }
    ActiveVoice& active = activeVoices_.back();
    active.spatialSource = source;
    active.spatialized = true;
    applySpatialization(active);
    return Result::success();
}

void XAudio2System::setListener(const assets::Vector3& position,
                                const assets::Vector3& target,
                                const assets::Vector3& up) noexcept {
    const assets::Vector3 forward = normalize(subtract(target, position));
    const assets::Vector3 right = normalize(cross(up, forward));
    if (length(forward) <= 0.0001F || length(right) <= 0.0001F) {
        return;
    }
    listenerPosition_ = position;
    listenerRight_ = right;
    for (ActiveVoice& active : activeVoices_) {
        applySpatialization(active);
    }
}

void XAudio2System::applySpatialization(ActiveVoice& active) noexcept {
    if (!active.spatialized || active.voice == nullptr ||
        masteringVoice_ == nullptr) {
        return;
    }
    const SpatialSoundMix mix = calculateSpatialSoundMix(
        listenerPosition_, listenerRight_, active.spatialSource);
    XAUDIO2_VOICE_DETAILS sourceDetails{};
    XAUDIO2_VOICE_DETAILS destinationDetails{};
    active.voice->GetVoiceDetails(&sourceDetails);
    masteringVoice_->GetVoiceDetails(&destinationDetails);
    if (sourceDetails.InputChannels == 1 &&
        destinationDetails.InputChannels == 2) {
        const std::array<float, 2> matrix{
            mix.leftGain,
            mix.rightGain,
        };
        (void)active.voice->SetOutputMatrix(masteringVoice_, 1, 2,
                                            matrix.data());
        return;
    }
    (void)active.voice->SetVolume(mix.attenuation);
}

Result XAudio2System::stopNamed(std::string_view eventName) noexcept {
    for (auto iterator = activeVoices_.begin();
         iterator != activeVoices_.end();) {
        if (iterator->eventName != eventName) {
            ++iterator;
            continue;
        }
        if (iterator->voice != nullptr) {
            iterator->voice->Stop();
            iterator->voice->DestroyVoice();
        }
        iterator = activeVoices_.erase(iterator);
    }
    return Result::success();
}

void XAudio2System::update() {
    for (auto iterator = activeVoices_.begin(); iterator != activeVoices_.end();) {
        if (!iterator->callback->finished.load()) {
            ++iterator;
            continue;
        }
        iterator->voice->DestroyVoice();
        iterator = activeVoices_.erase(iterator);
    }
}

void XAudio2System::destroyActiveVoices() noexcept {
    for (ActiveVoice& active : activeVoices_) {
        if (active.voice != nullptr) {
            active.voice->Stop();
            active.voice->DestroyVoice();
        }
    }
    activeVoices_.clear();
}

} // namespace usm::audio
