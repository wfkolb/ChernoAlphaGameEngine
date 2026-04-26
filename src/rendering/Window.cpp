#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <rendering/Window.h>
#include <string>
#include <memory>

#include "WindowImpl.h"

namespace engine::rendering {

static constexpr wchar_t kWindowClassName[] = L"EngineWindow";

static Window::Impl* implFromHwnd(HWND hwnd) {
    return reinterpret_cast<Window::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    Window::Impl* impl = implFromHwnd(hwnd);

    switch (msg) {
    case WM_SIZE:
        if (impl) {
            impl->width  = static_cast<uint32_t>(LOWORD(lParam));
            impl->height = static_cast<uint32_t>(HIWORD(lParam));
        }
        return 0;

    case WM_DESTROY:
        if (impl) {
            impl->closed = true;
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

Window::Window(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;

Window::~Window() {
    if (!impl_) return;
    if (impl_->hwnd) {
        DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
    UnregisterClassW(kWindowClassName, impl_->hinstance);
}

Window Window::create(const Desc& desc) {
    auto impl = std::make_unique<Impl>();
    impl->hinstance = GetModuleHandleW(nullptr);
    impl->width     = desc.width;
    impl->height    = desc.height;
    impl->title     = std::wstring(desc.title);

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = impl->hinstance;
    wc.hCursor       = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    impl->hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        impl->title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left,
        rc.bottom - rc.top,
        nullptr,
        nullptr,
        impl->hinstance,
        impl.get()
    );

    ShowWindow(impl->hwnd, SW_SHOWDEFAULT);

    return Window(std::move(impl));
}

void* Window::nativeHandle() const noexcept {
    return static_cast<void*>(impl_->hwnd);
}

uint32_t Window::clientWidth() const noexcept {
    return impl_->width;
}

uint32_t Window::clientHeight() const noexcept {
    return impl_->height;
}

bool Window::wantsClose() const noexcept {
    if (!impl_) return true;

    MSG msg = {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            impl_->closed = true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return impl_->closed;
}

} // namespace engine::rendering
