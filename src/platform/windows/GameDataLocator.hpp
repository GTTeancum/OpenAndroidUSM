#pragma once

#include "core/Result.hpp"

#include <filesystem>

namespace usm::platform {

[[nodiscard]] Result locateGameData(std::filesystem::path& output);

} // namespace usm::platform
