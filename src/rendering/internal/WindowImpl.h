#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <string>
#include <rendering/Window.h>

namespace engine::rendering {

struct Window::Impl {
    HWND          hwnd      = nullptr;
    HINSTANCE     hinstance = nullptr;
    uint32_t      width     = 0;
    uint32_t      height    = 0;
    bool          closed    = false;
    std::wstring  title;
};

} // namespace engine::rendering
