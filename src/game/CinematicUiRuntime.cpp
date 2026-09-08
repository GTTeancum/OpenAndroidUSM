#include "game/CinematicUiRuntime.hpp"

#include <algorithm>
#include <charconv>
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

void advanceTimer(std::int32_t elapsedMilliseconds,
                  std::int32_t& remainingMilliseconds,
                  bool& visible) noexcept {
    if (!visible || remainingMilliseconds < 1) {
        return;
    }
    remainingMilliseconds =
        std::max(0, remainingMilliseconds - elapsedMilliseconds);
    visible = remainingMilliseconds != 0;
}

std::u16string stripFontColorControls(std::u16string_view text) {
    std::u16string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == u'^' && index + 1U < text.size() &&
            text[index + 1U] >= u'0' && text[index + 1U] <= u'9') {
            ++index;
            continue;
        }
        result.push_back(text[index]);
    }
    return result;
}

std::u16string controllerTutorialText(std::string_view stringId,
                                      const std::u16string& localized) {
    // The shipped English table describes both the Android touch overlay and
    // the Xperia Play controls. The Windows reconstruction has neither UI,
    // so present the same actions with the XInput bindings that are actually
    // routed by XperiaKeyRouter. The renderer replaces supported bracket
    // tokens with the supplied Xbox atlas and retains the labels as fallback.
    if (stringId == "STR_MOVE") {
        return u"Use the left stick to move.";
    }
    if (stringId == "STR_JUMP") {
        return u"Press [A] once to jump.";
    }
    if (stringId == "STR_SHORT_SWING") {
        return u"Press [A] in the air to use the SHORT WEB SWING.";
    }
    if (stringId == "STR_COMBAT") {
        return u"Press [X] for a NORMAL ATTACK!";
    }
    if (stringId == "STR_WEB_ATTACK") {
        return u"Press [B] for a WEB ATTACK!\nWEB ATTACKS cost web power.";
    }
    if (stringId == "STR_LAUNCH") {
        return u"During a NORMAL ATTACK, press or hold [A] to launch enemies "
               u"into the air.";
    }
    if (stringId == "STR_SAVE_HOSTAGE") {
        return u"When close to a hostage, press [X] to rescue them and "
               u"acquire restorative orbs.";
    }
    if (stringId == "STR_SPIDER_SENSE") {
        return u"When SPIDER-SENSE appears, press [LB] to avoid or counter "
               u"the enemy's attack! Try it now!";
    }
    if (stringId == "STR_WEB_SWING") {
        return u"When a web-swing point appears, press [A] in the air to use "
               u"the LONG WEB SWING.";
    }
    if (stringId == "STR_JUMP_WALL") {
        return u"Use the left stick and press [A] to jump over the gap.";
    }
    if (stringId == "STR_SLIDE") {
        return u"Jump on the rope to slide.\nWhile sliding, press [A] to "
               u"jump up.";
    }
    if (stringId == "STR_ULTIMATE_COMBO") {
        return u"Press [Y] when the web power gauge blinks to activate the "
               u"ULTIMATE WEB COMBO.";
    }
    return localized;
}

} // namespace

void CinematicUiRuntime::bind(const LevelTextCatalog& strings) noexcept {
    strings_ = &strings;
    tutorialText_.clear();
    messageText_.clear();
    tutorialRemainingMilliseconds_ = 0;
    tutorialButton_ = -1;
    messageRemainingMilliseconds_ = 0;
    messageDurationMilliseconds_ = 0;
    messageFace_ = -1;
    tutorialVisible_ = false;
    messageVisible_ = false;
    messagePanelVisible_ = false;
    informationPanel_ = InformationPanel::None;
    tutorialDimBackground_ = false;
    letterboxVisible_ = false;
    comicCoverTipShown_ = false;
    interfaceEffectAlpha_ = 0.0F;
    interfaceEffectRatePerMillisecond_ = 0.0F;
    interfaceEffectFrame_ = -1;
    interfaceEffectJustStarted_ = false;
}

Result CinematicUiRuntime::showComicCover(std::int32_t comicIndex) {
    if (strings_ == nullptr || comicIndex < 0) {
        return Result::failure("Comic cover notice has invalid state");
    }
    // CComicCover::RenderTip (0x00304228) uses Main string 0x24b for
    // the first five-second explanation in a session, then 0x24a for each
    // three-second reminder. Profile persistence is intentionally outside
    // this single-level target, while the session behavior is retained.
    const std::size_t stringIndex = comicCoverTipShown_ ? 0x24aU : 0x24bU;
    const std::u16string* localized = strings_->main().at(stringIndex);
    if (localized == nullptr) {
        return Result::failure("Comic cover notice string is missing");
    }
    // RenderComicCoverInfo (0x0038c434) draws Main[0x24a/0x24b] verbatim;
    // AddCoverInfo stores the collected cover index at +0x20d0 but does not
    // append or substitute it into the localized sentence. Native CFont
    // consumes ^N color controls. Until font palettes are reconstructed,
    // remove those nonprinting controls instead of exposing them.
    messageText_ = stripFontColorControls(*localized);
    messageRemainingMilliseconds_ = comicCoverTipShown_ ? 3000 : 5000;
    messageDurationMilliseconds_ = messageRemainingMilliseconds_;
    messageFace_ = -1;
    messageVisible_ = true;
    messagePanelVisible_ = false;
    // RenderComicCoverInfo uses tutorial.bsprite frame 0 for Main[0x24b]
    // and frame 0xf for Main[0x24a], not the portrait/speech frame or a
    // generic subtitle rectangle. Keep this a timed notice, not a tutorial.
    informationPanel_ = comicCoverTipShown_ ? InformationPanel::Compact
                                          : InformationPanel::Expanded;
    comicCoverTipShown_ = true;
    return Result::success();
}

Result CinematicUiRuntime::applyCommand(const CinematicCommand& command) {
    if (command.name == "PlayDAECamera") {
        setColladaMovieUi(true);
        return Result::success();
    }
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
        // IAttributes::getAttributeAsInt/getAttributeAsBool return zero/false
        // for absent attributes in CCinematicThread::OnTutorial (0x003710d4).
        // Several authored traversal tutorials intentionally omit Timer;
        // CTutorial::AddInfo (0x0038dea0) treats that zero as modal.
        std::int32_t timer = 0;
        std::int32_t tutorialButton = 0;
        bool blackScreen = false;
        if (content == nullptr ||
            (command.findAttribute("Timer") != nullptr &&
             !integerAttribute(command, "Timer", timer)) ||
            (command.findAttribute("$TutorialButton") != nullptr &&
             !integerAttribute(command, "$TutorialButton", tutorialButton)) ||
            (command.findAttribute("blackScreen") != nullptr &&
             !booleanAttribute(command, "blackScreen", blackScreen))) {
            return Result::failure("Tutorial has invalid attributes");
        }
        const std::u16string* localized =
            strings_->findTutorialString(content->value);
        if (localized == nullptr) {
            return Result::failure("Tutorial references an unknown string");
        }
        tutorialText_ = controllerTutorialText(content->value, *localized);
        tutorialRemainingMilliseconds_ = timer;
        tutorialButton_ = tutorialButton;
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
        std::int32_t face = 0;
        if (message == nullptr || !integerAttribute(command, "Timer", timer) ||
            !integerAttribute(command, "$MessageFace", face) || timer < 0) {
            return Result::failure("ShowMessage has invalid attributes");
        }
        const std::u16string* localized =
            strings_->findLevelString(message->value);
        if (localized == nullptr) {
            return Result::failure("ShowMessage references an unknown string");
        }
        messageText_ = *localized;
        messageRemainingMilliseconds_ = timer;
        messageDurationMilliseconds_ = timer;
        messageFace_ = face;
        messageVisible_ = timer != 0;
        messagePanelVisible_ = messageVisible_;
        informationPanel_ = InformationPanel::None;
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
        tutorialRemainingMilliseconds_ < 1) {
        tutorialVisible_ = false;
    }
    advanceTimer(elapsed, tutorialRemainingMilliseconds_, tutorialVisible_);
    advanceTimer(elapsed, messageRemainingMilliseconds_, messageVisible_);
    if (interfaceEffectJustStarted_) {
        // StartInterfaceEffect records the current Application clock. The
        // first Render2DInterface paints the full starting alpha before
        // UpdateInterfaceEffect sees a zero clock delta.
        interfaceEffectJustStarted_ = false;
    } else if (interfaceEffectAlpha_ >= 0.0F) {
        interfaceEffectAlpha_ -=
            static_cast<float>(elapsed) * interfaceEffectRatePerMillisecond_;
    }
}

void CinematicUiRuntime::startInterfaceEffect(
    std::int32_t alpha, std::int32_t spriteParameter,
    std::int32_t frame) noexcept {
    // ALPHA_HIT_TIME is the 800.0f at 0x0056ec64. Native division is based
    // on the starting alpha itself, including its sign.
    constexpr float alphaHitTimeMilliseconds = 800.0F;
    interfaceEffectAlpha_ = static_cast<float>(alpha);
    // CLevel+0x108 is copied to CSprite+0x118 immediately before PaintFrame.
    // The recovered Spider-Sense caller supplies zero. A nonzero transform
    // is not used by this chronological combat path yet.
    (void)spriteParameter;
    interfaceEffectRatePerMillisecond_ =
        static_cast<float>(alpha) / alphaHitTimeMilliseconds;
    interfaceEffectFrame_ = frame < 0 ? 13 : frame;
    interfaceEffectJustStarted_ = true;
}

CinematicUiFrame CinematicUiRuntime::frame(
    bool quickTimeEventVisible, float quickTimeEventProgress) const {
    CinematicUiFrame result;
    result.letterboxVisible = letterboxVisible_;
    result.quickTimeEventVisible = quickTimeEventVisible;
    result.quickTimeEventProgress =
        std::clamp(quickTimeEventProgress, 0.0F, 1.0F);
    if (interfaceEffectAlpha_ >= 0.0F && interfaceEffectFrame_ >= 0) {
        result.interfaceEffectAlpha = static_cast<std::uint8_t>(
            std::clamp(interfaceEffectAlpha_, 0.0F, 255.0F));
        result.interfaceEffectFrame = interfaceEffectFrame_;
    }
    if (tutorialVisible_) {
        result.text = tutorialText_;
        result.textVisible = true;
        result.tutorialPanelVisible = true;
        result.tutorialButton = tutorialButton_;
        result.dimBackground = tutorialDimBackground_;
    } else if (messageVisible_) {
        result.text = messageText_;
        result.textVisible = true;
        result.messagePanelVisible = messagePanelVisible_;
        result.informationPanel = informationPanel_;
        result.messageFace = messageFace_;
        result.messageDurationMilliseconds = messageDurationMilliseconds_;
        result.messageElapsedMilliseconds = std::max(
            0, messageDurationMilliseconds_ - messageRemainingMilliseconds_);
    } else if (quickTimeEventVisible) {
        result.text = u"Press [A]";
        result.textVisible = true;
        result.tutorialPanelVisible = true;
    }
    return result;
}

} // namespace usm::game
