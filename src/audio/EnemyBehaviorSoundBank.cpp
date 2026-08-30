#include "audio/EnemyBehaviorSoundBank.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace usm::audio {

Result EnemyBehaviorSoundBank::preload(
    const game::EnemyBehaviorConfigDatabase& behaviorConfigs,
    const game::EnemySpecialActionConfigDatabase& specialActions,
    const VoxSoundTable& voxSounds, const SoundEventCatalog& catalog,
    std::span<const std::int16_t> enemyTypeIds) {
    decodedByVoxId_.clear();
    std::set<std::int32_t> voxSoundIds;
    constexpr std::string_view stateNames[]{
        "ENEMY_BEHAVIOR_HURT_STATE_COMMON", "ENEMY_BEHAVIOR_DEAD_STATE"};
    for (const std::int16_t enemyTypeId : enemyTypeIds) {
        for (const std::string_view stateName : stateNames) {
            const auto stateIds = behaviorConfigs.resolveStateSoundIds(
                stateName, enemyTypeId);
            voxSoundIds.insert(stateIds.begin(), stateIds.end());
        }
        for (const game::EnemyAnimationSpecialAction& action :
             specialActions.actions()) {
            if (action.enemyTypeId != enemyTypeId) {
                continue;
            }
            for (const std::int16_t soundMapId : action.soundMapIds) {
                const std::int32_t voxSoundId =
                    behaviorConfigs.resolveSoundMap(soundMapId, enemyTypeId);
                if (voxSoundId >= 0) {
                    voxSoundIds.insert(voxSoundId);
                }
            }
        }
    }
    for (const std::int32_t voxSoundId : voxSoundIds) {
        if (voxSoundId < 0 ||
            static_cast<std::size_t>(voxSoundId) >=
                voxSounds.records().size()) {
            decodedByVoxId_.clear();
            return Result::failure("Enemy behavior has an invalid Vox ID");
        }
        const VoxSoundRecord& record =
            voxSounds.records()[static_cast<std::size_t>(voxSoundId)];
        PcmAudio decoded;
        Result result = catalog.decode(record.eventName, decoded);
        if (!result) {
            decodedByVoxId_.clear();
            return Result::failure("Could not decode enemy sound " +
                                   record.eventName + ": " +
                                   result.message());
        }
        decodedByVoxId_.emplace(voxSoundId, std::move(decoded));
    }
    return Result::success();
}

Result EnemyBehaviorSoundBank::dispatch(
    std::int32_t voxSoundId, const PlayEnemyBehaviorSound& play) const {
    if (!play) {
        return Result::failure("Enemy sound callback is missing");
    }
    const auto decoded = decodedByVoxId_.find(voxSoundId);
    if (decoded == decodedByVoxId_.end()) {
        return Result::failure("Enemy sound was not predecoded");
    }
    return play(decoded->second, false);
}

} // namespace usm::audio
