#include "game/GameplayCinematicScheduler.hpp"

#include <algorithm>

namespace usm::game {

void GameplayCinematicScheduler::bind(
    std::span<const LevelCinematicAsset> cinematics) noexcept {
    cinematics_ = cinematics;
    instances_.clear();
    completions_.clear();
}

Result GameplayCinematicScheduler::start(std::int32_t cinematicId) {
    const auto existing = std::find_if(
        instances_.begin(), instances_.end(),
        [cinematicId](const Instance& instance) {
            return instance.playback.asset != nullptr &&
                   instance.playback.asset->objectId == cinematicId;
        });
    if (existing != instances_.end() && !existing->playback.completed) {
        // CCinematicManager::AddCinematic does not add a second copy of an
        // already executing cinematic. While* triggers call it every tick.
        return Result::success();
    }
    if (existing != instances_.end()) {
        instances_.erase(existing);
    }

    const auto asset = std::find_if(
        cinematics_.begin(), cinematics_.end(),
        [cinematicId](const LevelCinematicAsset& candidate) {
            return candidate.objectId == cinematicId &&
                   candidate.scriptAvailable;
        });
    if (asset == cinematics_.end()) {
        return Result::failure(
            "Cinematic command references an unavailable script");
    }

    Instance instance;
    instance.playback.asset = &*asset;
    Result result = instance.player.start(asset->script);
    if (!result) {
        return result;
    }
    instance.playback.durationMilliseconds = std::max(
        instance.player.durationMilliseconds(),
        asset->colladaDurationMilliseconds);
    instances_.push_back(std::move(instance));
    return Result::success();
}

Result GameplayCinematicScheduler::update(
    std::uint32_t deltaMilliseconds,
    const ConditionalCinematicCommandHandler& handler) {
    for (Instance& instance : instances_) {
        if (instance.playback.completed) {
            continue;
        }
        instance.playback.elapsedMilliseconds = std::min(
            instance.playback.elapsedMilliseconds + deltaMilliseconds,
            instance.playback.durationMilliseconds);
        Result result = instance.player.advanceToConditional(
            instance.playback.elapsedMilliseconds, handler);
        if (!result) {
            return result;
        }
        if (instance.player.finished() &&
            instance.playback.elapsedMilliseconds >=
                instance.playback.durationMilliseconds) {
            instance.playback.completed = true;
            completions_.push_back(instance.playback.asset);
        }
    }
    return Result::success();
}

std::vector<const LevelCinematicAsset*>
GameplayCinematicScheduler::consumeCompletions() {
    std::vector<const LevelCinematicAsset*> result;
    result.swap(completions_);
    return result;
}

void GameplayCinematicScheduler::remove(
    std::int32_t cinematicId) noexcept {
    std::erase_if(instances_, [cinematicId](const Instance& instance) {
        return instance.playback.asset != nullptr &&
               instance.playback.asset->objectId == cinematicId;
    });
}

const GameplayCinematicPlayback*
GameplayCinematicScheduler::presentation() const noexcept {
    const auto collada = std::find_if(
        instances_.rbegin(), instances_.rend(), [](const Instance& instance) {
            return instance.playback.asset != nullptr &&
                   instance.playback.asset->hasColladaPlayback();
        });
    if (collada != instances_.rend()) {
        return &collada->playback;
    }
    const auto cameraTrack = std::find_if(
        instances_.rbegin(), instances_.rend(), [](const Instance& instance) {
            return instance.playback.asset != nullptr &&
                   instance.playback.asset->cameraTrack.valid();
        });
    return cameraTrack == instances_.rend() ? nullptr
                                            : &cameraTrack->playback;
}

bool GameplayCinematicScheduler::hasActiveColladaPlayback() const noexcept {
    return std::any_of(
        instances_.begin(), instances_.end(), [](const Instance& instance) {
            return !instance.playback.completed &&
                   instance.playback.asset != nullptr &&
                   instance.playback.asset->hasColladaPlayback();
        });
}

bool GameplayCinematicScheduler::active(
    std::int32_t cinematicId) const noexcept {
    return std::any_of(
        instances_.begin(), instances_.end(),
        [cinematicId](const Instance& instance) {
            return !instance.playback.completed &&
                   instance.playback.asset != nullptr &&
                   instance.playback.asset->objectId == cinematicId;
        });
}

std::vector<std::int32_t> GameplayCinematicScheduler::activeIds() const {
    std::vector<std::int32_t> result;
    for (const Instance& instance : instances_) {
        if (!instance.playback.completed &&
            instance.playback.asset != nullptr) {
            result.push_back(instance.playback.asset->objectId);
        }
    }
    return result;
}

} // namespace usm::game
