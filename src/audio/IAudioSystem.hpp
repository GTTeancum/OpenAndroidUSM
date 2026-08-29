#pragma once

#include "core/Result.hpp"

namespace usm::audio {

class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;
    [[nodiscard]] virtual Result initialize() = 0;
};

} // namespace usm::audio

