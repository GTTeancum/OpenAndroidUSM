#pragma once

#include "audio/xaudio2/XAudio2System.hpp"
#include "platform/windows/Window.hpp"
#include "renderer/d3d11/D3D11Renderer.hpp"

#include <Windows.h>

namespace usm {

class Application final {
public:
    [[nodiscard]] int run(HINSTANCE instance);

private:
    platform::Window window_;
    renderer::D3D11Renderer renderer_;
    audio::XAudio2System audio_;
};

} // namespace usm

