#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LocalizedStringTable.hpp"

#include <cstdint>
#include <string>

namespace usm::game {

enum class InformationPanel : std::uint8_t {
    None,
    Expanded,
    Compact,
};

struct CinematicUiFrame {
    std::u16string text;
    bool textVisible{};
    bool tutorialPanelVisible{};
    std::int32_t tutorialButton{-1};
    InformationPanel informationPanel{InformationPanel::None};
    bool messagePanelVisible{};
    std::int32_t messageFace{-1};
    std::int32_t messageElapsedMilliseconds{};
    std::int32_t messageDurationMilliseconds{};
    bool letterboxVisible{};
    bool dimBackground{};
    bool quickTimeEventVisible{};
    float quickTimeEventProgress{};
    float blackOverlayAlpha{};
    std::uint8_t interfaceEffectAlpha{};
    std::int32_t interfaceEffectFrame{-1};
    std::uint32_t interfaceEffectColorRgb{0x00ffffffU};
};

// Portable state behind CCinematicThread::OnTutorial (0x003710d4),
// ShowMessage (0x003725f8), and InterfaceControlCmd (0x003711e0).
class CinematicUiRuntime final {
public:
    void bind(const LevelTextCatalog& strings) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicCommand& command);
    [[nodiscard]] Result showComicCover(std::int32_t comicIndex);
    void setColladaMovieUi(bool active) noexcept {
        letterboxVisible_ = active;
    }
    void update(std::uint32_t elapsedMilliseconds,
                bool dismissPressed) noexcept;
    // CLevel::StartInterfaceEffect (0x0037d7d0). Spider-Sense calls this
    // with (160, 0, -1); knockback hurt states call it with
    // (255, 0xff0000, -1). A zero sprite color means native white.
    void startInterfaceEffect(std::int32_t alpha = 160,
                              std::int32_t spriteParameter = 0,
                              std::int32_t frame = -1) noexcept;
    [[nodiscard]] CinematicUiFrame frame(bool quickTimeEventVisible = false,
                                         float quickTimeEventProgress = 0.0F)
        const;

    [[nodiscard]] bool tutorialVisible() const noexcept {
        return tutorialVisible_;
    }
    [[nodiscard]] bool modalTutorialVisible() const noexcept {
        // CTutorial::AddInfo (0x0038dea0) stores Timer < 1 as the modal flag.
        // CLevel::Update (0x003820bc) returns before world/player/physics
        // simulation while that tutorial remains visible.
        return tutorialVisible_ && tutorialRemainingMilliseconds_ < 1;
    }
    [[nodiscard]] bool messageVisible() const noexcept {
        return messageVisible_;
    }
    [[nodiscard]] bool letterboxVisible() const noexcept {
        return letterboxVisible_;
    }

private:
    const LevelTextCatalog* strings_{};
    std::u16string tutorialText_;
    std::u16string messageText_;
    std::int32_t tutorialRemainingMilliseconds_{};
    std::int32_t tutorialButton_{-1};
    std::int32_t messageRemainingMilliseconds_{};
    std::int32_t messageDurationMilliseconds_{};
    std::int32_t messageFace_{-1};
    bool tutorialVisible_{};
    bool messageVisible_{};
    bool messagePanelVisible_{};
    InformationPanel informationPanel_{InformationPanel::None};
    bool tutorialDimBackground_{};
    bool letterboxVisible_{};
    bool comicCoverTipShown_{};
    float interfaceEffectAlpha_{};
    float interfaceEffectRatePerMillisecond_{};
    std::int32_t interfaceEffectFrame_{-1};
    std::uint32_t interfaceEffectColorRgb_{0x00ffffffU};
    bool interfaceEffectJustStarted_{};
};

} // namespace usm::game
