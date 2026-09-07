#pragma once

#include "core/Result.hpp"
#include "game/LocalizedStringTable.hpp"

#include <cstdint>
#include <string>

namespace usm::game {

enum class DeathConfirmationOutcome {
    None,
    Retry,
    Exit,
};

struct DeathConfirmationFrame {
    bool visible{};
    std::int32_t selection{};
    std::u16string title;
    std::u16string message;
    std::u16string yes;
    std::u16string no;
    bool loadingVisible{};
    std::u16string loadingLabel;
    std::u16string loadingSuffix;
};

// Source-level reconstruction of the death-owned GS_Confirmation instance.
// CLevel::UpdateBlackScreen (0x003805f0) creates it with Main string 0x30,
// title 0x22 and initial selection zero. GS_Confirmation::Update
// (0x002dc0d0) consumes release edges and wraps between Yes and No.
class DeathConfirmationRuntime final {
public:
    [[nodiscard]] Result bind(const LevelTextCatalog& strings);
    void reset() noexcept;
    void update(bool confirmationReady, bool upReleased,
                bool downReleased, bool menuSelectedReleased) noexcept;

    [[nodiscard]] DeathConfirmationOutcome consumeOutcome() noexcept;
    [[nodiscard]] DeathConfirmationFrame frame() const;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::int32_t selection() const noexcept {
        return selection_;
    }

private:
    std::u16string title_;
    std::u16string message_;
    std::u16string yes_;
    std::u16string no_;
    bool active_{};
    std::int32_t selection_{};
    DeathConfirmationOutcome outcome_{DeathConfirmationOutcome::None};
};

} // namespace usm::game
