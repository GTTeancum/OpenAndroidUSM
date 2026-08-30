#include "audio/PlayerStateSoundBank.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace usm::audio {

Result PlayerStateSoundBank::preload(
    const game::PlayerStateConfigDatabase& states,
    const VoxSoundTable& voxSounds, const SoundEventCatalog& catalog,
    std::span<const std::string_view> stateNames) {
    states_ = nullptr;
    decodedByConfig_.clear();
    nextVariantByConfig_.clear();
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
        nextVariantByConfig_.emplace(configId, 0);
    }
    states_ = &states;
    return Result::success();
}

Result PlayerStateSoundBank::dispatchStateEnter(
    std::string_view stateName, const PlayPlayerStateSound& play) {
    if (states_ == nullptr) {
        return Result::failure("Player state sound bank is not loaded");
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

Result PlayerStateSoundBank::dispatchConfigs(
    std::span<const std::int16_t> configIds,
    const PlayPlayerStateSound& play) {
    if (!play) {
        return Result::failure("Player sound callback is missing");
    }
    for (const std::int16_t configId : configIds) {
        auto variants = decodedByConfig_.find(configId);
        auto cursor = nextVariantByConfig_.find(configId);
        if (variants == decodedByConfig_.end() || variants->second.empty() ||
            cursor == nextVariantByConfig_.end()) {
            return Result::failure("Player SoundConfig was not predecoded");
        }
        const std::size_t variantIndex = cursor->second % variants->second.size();
        cursor->second = (variantIndex + 1) % variants->second.size();
        const DecodedVariant& variant = variants->second[variantIndex];
        Result result = play(variant.audio, variant.looping);
        if (!result) {
            return result;
        }
    }
    return Result::success();
}

std::size_t PlayerStateSoundBank::decodedVariantCount() const noexcept {
    std::size_t count = 0;
    for (const auto& entry : decodedByConfig_) {
        count += entry.second.size();
    }
    return count;
}

} // namespace usm::audio
