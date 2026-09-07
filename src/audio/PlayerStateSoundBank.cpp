#include "audio/PlayerStateSoundBank.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace usm::audio {

Result PlayerStateSoundBank::preload(
    const game::PlayerStateConfigDatabase& states,
    const VoxSoundTable& voxSounds, const SoundEventCatalog& catalog,
    std::span<const std::string_view> stateNames,
    game::NativeRandomizer* nativeRandomizer) {
    states_ = nullptr;
    nativeRandomizer_ = nativeRandomizer != nullptr
        ? nativeRandomizer
        : &ownedNativeRandomizer_;
    decodedByConfig_.clear();
    activeConfigIds_.clear();
    std::set<std::int16_t> configIds;
    for (const std::string_view stateName : stateNames) {
        const game::PlayerStateDefinition* state = states.findState(stateName);
        if (state == nullptr) {
            return Result::failure("Player sound state is missing: " +
                                   std::string(stateName));
        }
        configIds.insert(state->enterSoundConfigIds.begin(),
                         state->enterSoundConfigIds.end());
        configIds.insert(state->frameSoundConfigIds.begin(),
                         state->frameSoundConfigIds.end());
    }

    for (const std::int16_t configId : configIds) {
        const game::PlayerSoundConfig* config =
            states.findSoundConfig(configId);
        if (config == nullptr || config->voxSoundIds.empty()) {
            return Result::failure("Player SoundConfig is missing or empty");
        }
        std::vector<DecodedVariant> variants;
        variants.reserve(config->voxSoundIds.size());
        for (const std::int16_t voxId : config->voxSoundIds) {
            if (voxId < 0 ||
                static_cast<std::size_t>(voxId) >= voxSounds.records().size()) {
                return Result::failure("Player SoundConfig has invalid Vox ID");
            }
            const VoxSoundRecord& record =
                voxSounds.records()[static_cast<std::size_t>(voxId)];
            DecodedVariant variant;
            variant.voxSoundId = voxId;
            variant.eventName = record.eventName;
            Result result = catalog.decode(record.eventName, variant.audio);
            if (!result) {
                return Result::failure("Could not decode player sound " +
                                       record.eventName + ": " +
                                       result.message());
            }
            // Player::PlaySound passes SoundConfig field +0x04 == 2 as the
            // looping argument to PlayerSFX; the Vox record's flag at +0x18
            // is a separate native-media property.
            variant.looping = config->selectionMode == 2;
            variants.push_back(std::move(variant));
        }
        decodedByConfig_.emplace(configId, std::move(variants));
    }
    states_ = &states;
    return Result::success();
}

Result PlayerStateSoundBank::dispatchStateEnter(
    std::string_view stateName, const PlayPlayerStateSound& play,
    const StopPlayerStateSound& stop) {
    if (states_ == nullptr) {
        return Result::failure("Player state sound bank is not loaded");
    }
    if (stop) {
        Result cleanResult = cleanActive(stop);
        if (!cleanResult) {
            return cleanResult;
        }
    } else {
        activeConfigIds_.clear();
    }
    const game::PlayerStateDefinition* state = states_->findState(stateName);
    return state == nullptr
               ? Result::failure("Player sound state is missing")
               : dispatchConfigs(state->enterSoundConfigIds, play);
}

Result PlayerStateSoundBank::dispatchStateFrame(
    std::string_view stateName, const PlayPlayerStateSound& play) {
    if (states_ == nullptr) {
        return Result::failure("Player state sound bank is not loaded");
    }
    const game::PlayerStateDefinition* state = states_->findState(stateName);
    return state == nullptr
               ? Result::failure("Player sound state is missing")
               : dispatchConfigs(state->frameSoundConfigIds, play);
}

Result PlayerStateSoundBank::dispatchEmitter(
    std::int16_t configId, std::size_t emitterIndex,
    const PlayPlayerStateSound& play) {
    if (states_ == nullptr) {
        return Result::failure("Player state sound bank is not loaded");
    }
    const game::PlayerSoundConfig* config =
        states_->findSoundConfig(configId);
    if (config == nullptr ||
        emitterIndex >= config->activeEmitterIds.size() ||
        config->activeEmitterIds[emitterIndex] <= 0) {
        return Result::failure("Player SoundConfig emitter is invalid");
    }
    const auto variants = decodedByConfig_.find(configId);
    if (variants == decodedByConfig_.end() || variants->second.empty()) {
        return Result::failure("Player SoundConfig was not predecoded");
    }
    std::size_t variantIndex{};
    if (config->playbackType == 0) {
        // Player::UpdateSound (0x003429f4-0x00342a42) passes the first and
        // last Vox IDs to PlayerSFX. VoxSoundManager::Play2DRandom
        // (0x003dada0) samples that inclusive range through random(count).
        variantIndex = variants->second.size() == 1
            ? 0
            : static_cast<std::size_t>(nativeRandomizer_->bounded(
                  static_cast<std::int32_t>(variants->second.size())));
    } else if (config->playbackType == 1 &&
               emitterIndex < variants->second.size()) {
        // Type 1 pairs each emitter frame with the Vox ID at the same index.
        variantIndex = emitterIndex;
    } else {
        return Result::failure("Player SoundConfig emitter mapping is invalid");
    }
    return dispatchVariant(configId, variantIndex, play);
}

Result PlayerStateSoundBank::dispatchConfigs(
    std::span<const std::int16_t> configIds,
    const PlayPlayerStateSound& play) {
    if (!play) {
        return Result::failure("Player sound callback is missing");
    }
    for (const std::int16_t configId : configIds) {
        const game::PlayerSoundConfig* config =
            states_ == nullptr ? nullptr : states_->findSoundConfig(configId);
        auto variants = decodedByConfig_.find(configId);
        if (config == nullptr || variants == decodedByConfig_.end() ||
            variants->second.empty()) {
            return Result::failure("Player SoundConfig was not predecoded");
        }
        const std::int16_t firstEmitter = config->activeEmitterIds.empty()
                                              ? -1
                                              : config->activeEmitterIds.front();
        const bool retainedByNative = config->playbackType == 1 ||
                                      config->selectionMode != 1 ||
                                      firstEmitter >= 0;
        if (retainedByNative &&
            std::find(activeConfigIds_.begin(), activeConfigIds_.end(),
                      configId) == activeConfigIds_.end()) {
            activeConfigIds_.push_back(configId);
        }
        // Player::PlaySound (0x0034905c) plays only type-0 configs whose
        // first emitter is -1/0 immediately. Positive emitter frames and
        // type-1 per-frame lists are consumed by Player::UpdateSound.
        if (config->playbackType != 0 || firstEmitter >= 1) {
            continue;
        }
        // Player::PlaySound (0x003490f8-0x0034914c) passes a range only
        // when a type-0 config contains at least two Vox IDs. The native
        // Play2D/3DRandom routines at 0x003dada0/0x003dafa8 then consume one
        // global random(count) call.
        const std::size_t variantIndex = variants->second.size() == 1
            ? 0
            : static_cast<std::size_t>(nativeRandomizer_->bounded(
                  static_cast<std::int32_t>(variants->second.size())));
        Result result = dispatchVariant(configId, variantIndex, play);
        if (!result) {
            return result;
        }
    }
    return Result::success();
}

Result PlayerStateSoundBank::cleanActive(
    const StopPlayerStateSound& stop) {
    if (!stop) {
        activeConfigIds_.clear();
        return Result::success();
    }
    for (const std::int16_t configId : activeConfigIds_) {
        const game::PlayerSoundConfig* config =
            states_ == nullptr ? nullptr : states_->findSoundConfig(configId);
        const auto variants = decodedByConfig_.find(configId);
        if (config == nullptr || variants == decodedByConfig_.end()) {
            return Result::failure("Player SoundConfig was not predecoded");
        }
        // Player::StopSound (0x00341e10) leaves selection-mode 1 one-shots
        // alone; every other retained config stops each Vox entry.
        if (config->selectionMode == 1) {
            continue;
        }
        for (const DecodedVariant& variant : variants->second) {
            Result result = stop(variant.voxSoundId, variant.eventName);
            if (!result) {
                return result;
            }
        }
    }
    activeConfigIds_.clear();
    return Result::success();
}

Result PlayerStateSoundBank::dispatchVariant(
    std::int16_t configId, std::size_t variantIndex,
    const PlayPlayerStateSound& play) {
    if (!play) {
        return Result::failure("Player sound callback is missing");
    }
    const auto variants = decodedByConfig_.find(configId);
    if (variants == decodedByConfig_.end() ||
        variantIndex >= variants->second.size()) {
        return Result::failure("Player SoundConfig variant is invalid");
    }
    const DecodedVariant& variant = variants->second[variantIndex];
    return play(variant.voxSoundId, variant.eventName, variant.audio,
                variant.looping);
}

std::size_t PlayerStateSoundBank::decodedVariantCount() const noexcept {
    std::size_t count = 0;
    for (const auto& entry : decodedByConfig_) {
        count += entry.second.size();
    }
    return count;
}

} // namespace usm::audio
