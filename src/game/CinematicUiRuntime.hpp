#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LocalizedStringTable.hpp"

#include <cstdint>
#include <string>

namespace usm::game {

struct CinematicUiFrame {
    std::u16string text;
    bool textVisible{};
    bool letterboxVisible{};
    bool dimBackground{};
    bool quickTimeEventVisible{};
    float quickTimeEventProgress{};
    float blackOverlayAlpha{};
};

// Portable state behind CCinematicThread::OnTutorial (0x003710d4),
// ShowMessage (0x003725f8), and InterfaceControlCmd (0x003711e0).
class CinematicUiRuntime final {
public:
    void bind(const LevelTextCatalog& strings) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicCommand& command);
    void update(std::uint32_t elapsedMilliseconds,
                bool dismissPressed) noexcept;
    [[nodiscard]] CinematicUiFrame frame(bool quickTimeEventVisible = false,
                                         float quickTimeEventProgress = 0.0F)
        const;

    [[nodiscard]] bool tutorialVisible() const noexcept {
        return tutorialVisible_;
    }
    [[nodiscard]] bool messageVisible() const noexcept {
        return messageVisible_;
    }

private:
    const LevelTextCatalog* strings_{};
    std::u16string tutorialText_;
    std::u16string messageText_;
    std::int32_t tutorialRemainingMilliseconds_{};
    std::int32_t messageRemainingMilliseconds_{};
    bool tutorialVisible_{};
    bool messageVisible_{};
    bool tutorialDimBackground_{};
    bool letterboxVisible_{};
};

} // namespace usm::game
