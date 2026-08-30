#pragma once

#include "audio/OggAudio.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "audio/VoxSoundTable.hpp"
#include "core/Result.hpp"
#include "game/EnemyBehaviorConfig.hpp"
#include "game/EnemySpecialActionConfig.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <span>

namespace usm::audio {

using PlayEnemyBehaviorSound =
    std::function<Result(const PcmAudio& audio, bool loop)>;

// Predecoded playback for behavior-state and animation-keyframe sounds. The
// native runtime queues original Vox IDs, keeping the gameplay model separate
// from XAudio2 and avoiding decode work during combat.
class EnemyBehaviorSoundBank final {
public:
    [[nodiscard]] Result preload(
        const game::EnemyBehaviorConfigDatabase& behaviorConfigs,
        const game::EnemySpecialActionConfigDatabase& specialActions,
        const VoxSoundTable& voxSounds, const SoundEventCatalog& catalog,
        std::span<const std::int16_t> enemyTypeIds);
    [[nodiscard]] Result dispatch(std::int32_t voxSoundId,
                                  const PlayEnemyBehaviorSound& play) const;

    [[nodiscard]] std::size_t decodedSoundCount() const noexcept {
        return decodedByVoxId_.size();
    }

private:
    std::map<std::int32_t, PcmAudio> decodedByVoxId_;
};

} // namespace usm::audio
