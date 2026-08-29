#pragma once

#include "core/Result.hpp"

#include <Windows.h>

#include <cstdint>
#include <string_view>

namespace usm::platform {

class Window final {
public:
    Window() = default;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    [[nodiscard]] Result create(HINSTANCE instance, std::wstring_view title,
                                std::uint32_t clientWidth,
                                std::uint32_t clientHeight);
    [[nodiscard]] bool pumpMessages();
    [[nodiscard]] HWND nativeHandle() const noexcept { return handle_; }
    [[nodiscard]] std::uint32_t clientWidth() const noexcept { return clientWidth_; }
    [[nodiscard]] std::uint32_t clientHeight() const noexcept { return clientHeight_; }

private:
    static LRESULT CALLBACK windowProcedure(HWND handle, UINT message,
                                            WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_{};
    HWND handle_{};
    std::uint32_t clientWidth_{};
    std::uint32_t clientHeight_{};
};

} // namespace usm::platform

