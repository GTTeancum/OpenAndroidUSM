#include "game/QuickTimeEventRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

namespace usm::game {
namespace {

bool integerAttribute(const CinematicCommand& command, std::string_view name,
                      std::int32_t& value) noexcept {
    const CinematicAttribute* attribute = command.findAttribute(name);
    if (attribute == nullptr) {
        return false;
    }
    const char* begin = attribute->value.data();
    const char* end = begin + attribute->value.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

} // namespace

void QuickTimeEventRuntime::bind(
    const ButtonConfigDatabase& configs) noexcept {
    configs_ = &configs;
    configId_ = -1;
    sourceCinematicId_ = -1;
    successCinematicId_ = -1;
    failureCinematicId_ = -1;
    elapsedMilliseconds_ = 0;
    durationMilliseconds_ = 0;
    cinematicRequest_.reset();
    buttonProgress_.reset();
    active_ = false;
}

Result QuickTimeEventRuntime::applyCommand(const CinematicCommand& command,
                                          std::int32_t sourceCinematicId) {
    if (command.name != "StartQTE") {
        return Result::success();
    }
    if (configs_ == nullptr) {
        return Result::failure("Quick-time-event runtime is not bound");
    }
    std::int32_t configId = -1;
    std::int32_t successId = -1;
    std::int32_t failureId = -1;
    if (!integerAttribute(command, "QTEID", configId) ||
        !integerAttribute(command, "^ID^Cinematic^Success", successId) ||
        !integerAttribute(command, "^ID^Cinematic^Fail", failureId)) {
        return Result::failure("StartQTE has invalid attributes");
    }
    const ButtonConfigDefinition* definition = configs_->find(configId);
    if (definition == nullptr || definition->durationMilliseconds <= 0.0F ||
        definition->durationMilliseconds >
            static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
        return Result::failure("StartQTE references an invalid button config");
    }

    configId_ = configId;
    sourceCinematicId_ = sourceCinematicId;
    successCinematicId_ = successId;
    failureCinematicId_ = failureId;
    elapsedMilliseconds_ = 0;
    durationMilliseconds_ = static_cast<std::uint32_t>(
        std::lround(definition->durationMilliseconds));
    cinematicRequest_.reset();
    buttonProgress_.reset();
    active_ = true;
    return Result::success();
}

void QuickTimeEventRuntime::update(std::uint32_t elapsedMilliseconds,
                                   bool actionPressed) noexcept {
    if (!active_ || cinematicRequest_.has_value()) {
        return;
    }
    elapsedMilliseconds_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        durationMilliseconds_, static_cast<std::uint64_t>(elapsedMilliseconds_) +
                                   elapsedMilliseconds));
    const auto* definition = configs_ == nullptr ? nullptr : configs_->find(configId_);
    const bool succeeded = definition != nullptr && buttonProgress_.update(
        elapsedMilliseconds, actionPressed, definition->requiredActionCount,
        definition->interactionType == 3);
    if (succeeded) {
        cinematicRequest_ = successCinematicId_;
        active_ = false;
    } else if (elapsedMilliseconds_ >= durationMilliseconds_) {
        cinematicRequest_ = failureCinematicId_;
        active_ = false;
    }
}

std::optional<std::int32_t>
QuickTimeEventRuntime::consumeCinematicRequest() noexcept {
    std::optional<std::int32_t> request = cinematicRequest_;
    cinematicRequest_.reset();
    return request;
}

} // namespace usm::game
