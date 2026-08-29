#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::game {

struct CinematicAttribute {
    std::string type;
    std::string name;
    std::string value;
};

struct CinematicCommand {
    std::uint32_t timestampMilliseconds{};
    std::int32_t id{-1};
    std::string name;
    std::vector<CinematicAttribute> attributes;

    [[nodiscard]] const CinematicAttribute* findAttribute(
        std::string_view attributeName) const noexcept;
};

struct CinematicThread {
    std::int32_t type{-1};
    std::int32_t objectId{-1};
    std::string name;
    std::vector<CinematicCommand> commands;
};

// Parser for the original CFF cinematic command stream. CFF resources are
// UTF-16 XML fragments containing one or more cinematicThread roots.
class CinematicScript final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const std::vector<CinematicThread>& threads() const noexcept {
        return threads_;
    }
    [[nodiscard]] std::size_t commandCount() const noexcept;

private:
    std::vector<CinematicThread> threads_;
};

} // namespace usm::game
