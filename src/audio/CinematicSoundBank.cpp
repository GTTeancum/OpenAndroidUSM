#include "audio/CinematicSoundBank.hpp"

#include <algorithm>
#include <string_view>

namespace usm::audio {
namespace {

constexpr std::string_view kSoundCommand = "SoundControl";
constexpr std::string_view kEventAttribute = "$VoxSounds";
constexpr std::string_view kLoopAttribute = "Loop";
constexpr std::string_view kStopAttribute = "Stop";
constexpr std::string_view kStop2DAttribute = "Stop2D";
constexpr std::string_view kStop3DAttribute = "Stop3D";

bool booleanAttribute(const game::CinematicCommand& command,
                      std::string_view name) noexcept {
    const game::CinematicAttribute* attribute = command.findAttribute(name);
    return attribute != nullptr &&
           (attribute->value == "true" || attribute->value == "1");
}

} // namespace

Result CinematicSoundBank::preload(const game::CinematicScript& script,
                                   const SoundEventCatalog& catalog) {
    const game::CinematicScript* scripts[]{&script};
    return preload(scripts, catalog);
}

Result CinematicSoundBank::preload(
    std::span<const game::CinematicScript* const> scripts,
    const SoundEventCatalog& catalog) {
    decodedByEvent_.clear();
    unresolvedEvents_.clear();
    for (const game::CinematicScript* script : scripts) {
        if (script == nullptr) {
            return Result::failure("Cinematic sound preload has a null script");
        }
        for (const game::CinematicThread& thread : script->threads()) {
            for (const game::CinematicCommand& command : thread.commands) {
                if (command.name != kSoundCommand) {
                    continue;
                }
                const game::CinematicAttribute* event =
                    command.findAttribute(kEventAttribute);
                if (event == nullptr || event->value.empty()) {
                    return Result::failure(
                        "CFF SoundControl command has no $VoxSounds event");
                }
                if (decodedByEvent_.contains(event->value) ||
                    std::find(unresolvedEvents_.begin(),
                              unresolvedEvents_.end(), event->value) !=
                        unresolvedEvents_.end()) {
                    continue;
                }
                if (catalog.resolve(event->value) == nullptr) {
                    unresolvedEvents_.push_back(event->value);
                    continue;
                }
                PcmAudio decoded;
                Result result = catalog.decode(event->value, decoded);
                if (!result) {
                    decodedByEvent_.clear();
                    unresolvedEvents_.clear();
                    return result;
                }
                decodedByEvent_.emplace(event->value, std::move(decoded));
            }
        }
    }
    return decodedByEvent_.empty()
               ? Result::failure("Cinematic contains no resolvable sound events")
               : Result::success();
}

Result CinematicSoundBank::dispatch(
    const game::CinematicCommand& command,
    const PlayCinematicSound& play,
    const StopCinematicSound& stop) const {
    if (command.name != kSoundCommand) {
        return Result::success();
    }
    const game::CinematicAttribute* event =
        command.findAttribute(kEventAttribute);
    if (event == nullptr) {
        return Result::failure("CFF SoundControl event is missing");
    }
    if (booleanAttribute(command, kStopAttribute) ||
        booleanAttribute(command, kStop2DAttribute) ||
        booleanAttribute(command, kStop3DAttribute)) {
        return stop ? stop(event->value) : Result::success();
    }
    const auto decoded = decodedByEvent_.find(event->value);
    if (decoded == decodedByEvent_.end()) {
        // Missing VoxSound table aliases remain explicit in unresolvedEvents;
        // no arbitrary replacement sound is played.
        return Result::success();
    }
    return play(event->value, decoded->second,
                booleanAttribute(command, kLoopAttribute));
}

} // namespace usm::audio
