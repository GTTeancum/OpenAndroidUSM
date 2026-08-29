#include "platform/windows/Window.hpp"

#include <algorithm>
#include <string>

namespace usm::platform {
namespace {
constexpr wchar_t kWindowClassName[] = L"OpenAndroidUSM.Window";
}

Window::~Window() {
    if (handle_ != nullptr) {
        DestroyWindow(handle_);
    }
}

Result Window::create(HINSTANCE instance, std::wstring_view title,
                      std::uint32_t clientWidth, std::uint32_t clientHeight) {
    instance_ = instance;
    clientWidth_ = clientWidth;
    clientHeight_ = clientHeight;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &Window::windowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClassName;

    if (RegisterClassExW(&windowClass) == 0 &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return Result::failure("RegisterClassExW failed");
    }

    RECT rectangle{0, 0, static_cast<LONG>(clientWidth_),
                   static_cast<LONG>(clientHeight_)};
    constexpr DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rectangle, style, FALSE);

    const std::wstring ownedTitle(title);
    handle_ = CreateWindowExW(
        0, kWindowClassName, ownedTitle.c_str(), style, CW_USEDEFAULT,
        CW_USEDEFAULT, rectangle.right - rectangle.left,
        rectangle.bottom - rectangle.top, nullptr, nullptr, instance_, this);
    if (handle_ == nullptr) {
        return Result::failure("CreateWindowExW failed");
    }

    ShowWindow(handle_, SW_SHOWDEFAULT);
    UpdateWindow(handle_);
    return Result::success();
}

bool Window::pumpMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            return false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return true;
}

LRESULT CALLBACK Window::windowProcedure(HWND handle, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(handle, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    switch (message) {
    case WM_CLOSE:
        DestroyWindow(handle);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(handle, message, wParam, lParam);
    }
}

} // namespace usm::platform

