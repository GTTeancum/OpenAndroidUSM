#include "game/CinematicUiRuntime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string_view>
#include <utility>

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

bool booleanAttribute(const CinematicCommand& command, std::string_view name,
                      bool& value) noexcept {
    const CinematicAttribute* attribute = command.findAttribute(name);
    if (attribute == nullptr) {
        return false;
    }
    if (attribute->value == "true" || attribute->value == "1") {
        value = true;
        return true;
    }
    if (attribute->value == "false" || attribute->value == "0") {
        value = false;
        return true;
    }
    return false;
}

std::u16string controllerText(std::u16string value) {
    constexpr std::array replacements{
        std::pair{u"^J", u"[A]"}, std::pair{u"^X", u"[A]"},
        std::pair{u"^K", u"[X]"}, std::pair{u"^u", u"[X]"},
        std::pair{u"^L", u"[B]"}, std::pair{u"^C", u"[B]"},
    };
    for (const auto& [source, replacement] : replacements) {
        std::size_t offset = 0;
        while ((offset = value.find(source, offset)) !=
               std::u16string::npos) {
            value.replace(offset, std::char_traits<char16_t>::length(source),
                          replacement);
            offset += std::char_traits<char16_t>::length(replacement);
        }
    }
    return value;
}

void advanceTimer(std::int32_t elapsedMilliseconds,
                  std::int32_t& remainingMilliseconds,
                  bool& visible) noexcept {
    if (!visible || remainingMilliseconds < 0) {
        return;
    }
    remainingMilliseconds =
        std::max(0, remainingMilliseconds - elapsedMilliseconds);
    visible = remainingMilliseconds != 0;
}

} // namespace

void CinematicUiRuntime::bind(const LevelTextCatalog& strings) noexcept {
    strings_ = &strings;
    tutorialText_.clear();
    messageText_.clear();
    tutorialRemainingMilliseconds_ = 0;
    messageRemainingMilliseconds_ = 0;
    tutorialVisible_ = false;
    messageVisible_ = false;
    tutorialDimBackground_ = false;
    letterboxVisible_ = false;
}

Result CinematicUiRuntime::applyCommand(const CinematicCommand& command) {
    if (command.name == "InterfaceControl") {
        bool black = false;
        if (!booleanAttribute(command, "BlackEnable", black)) {
            return Result::failure("InterfaceControl has no valid BlackEnable");
        }
        letterboxVisible_ = black;
        return Result::success();
    }
    if (command.name == "Tutorial") {
        if (strings_ == nullptr) {
            return Result::failure("Cinematic UI runtime is not bound");
        }
        const CinematicAttribute* content =
            command.findAttribute("Content$Tutorial_STRINGID");
        std::int32_t timer = -1;
        bool blackScreen = false;
        if (content == nullptr || !integerAttribute(command, "Timer", timer) ||
            !booleanAttribute(command, "blackScreen", blackScreen)) {
            return Result::failure("Tutorial has invalid attributes");
        }
        const std::u16string* localized =
            strings_->findTutorialString(content->value);
        if (localized == nullptr) {
            return Result::failure("Tutorial references an unknown string");
        }
        tutorialText_ = controllerText(*localized);
        tutorialRemainingMilliseconds_ = timer;
        tutorialDimBackground_ = blackScreen;
        tutorialVisible_ = true;
        return Result::success();
    }
    if (command.name == "ShowMessage") {
        if (strings_ == nullptr) {
            return Result::failure("Cinematic UI runtime is not bound");
        }
        const CinematicAttribute* message =
            command.findAttribute("$LEVEL_STRINGID");
        std::int32_t timer = 0;
        if (message == nullptr || !integerAttribute(command, "Timer", timer) ||
            timer < 0) {
            return Result::failure("ShowMessage has invalid attributes");
        }
        const std::u16string* localized =
            strings_->findLevelString(message->value);
        if (localized == nullptr) {
            return Result::failure("ShowMessage references an unknown string");
        }
        messageText_ = *localized;
        messageRemainingMilliseconds_ = timer;
        messageVisible_ = timer != 0;
        return Result::success();
    }
    return Result::success();
}

void CinematicUiRuntime::update(std::uint32_t elapsedMilliseconds,
                                bool dismissPressed) noexcept {
    const std::int32_t elapsed = static_cast<std::int32_t>(
        std::min<std::uint32_t>(
            elapsedMilliseconds,
            static_cast<std::uint32_t>(
                std::numeric_limits<std::int32_t>::max())));
    if (dismissPressed && tutorialVisible_ &&
        tutorialRemainingMilliseconds_ < 0) {
        tutorialVisible_ = false;
    }
    advanceTimer(elapsed, tutorialRemainingMilliseconds_, tutorialVisible_);
    advanceTimer(elapsed, messageRemainingMilliseconds_, messageVisible_);
}

CinematicUiFrame CinematicUiRuntime::frame(
    bool quickTimeEventVisible, float quickTimeEventProgress) const {
    CinematicUiFrame result;
    result.letterboxVisible = letterboxVisible_;
    result.quickTimeEventVisible = quickTimeEventVisible;
    result.quickTimeEventProgress =
        std::clamp(quickTimeEventProgress, 0.0F, 1.0F);
    if (tutorialVisible_) {
        result.text = tutorialText_;
        result.textVisible = true;
        result.dimBackground = tutorialDimBackground_;
    } else if (messageVisible_) {
        result.text = messageText_;
        result.textVisible = true;
    } else if (quickTimeEventVisible) {
        result.text = u"Press [A]";
        result.textVisible = true;
    }
    return result;
}

} // namespace usm::game
