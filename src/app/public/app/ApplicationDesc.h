#pragma once
#include <cstdint>
#include <string>

namespace engine::app {

class IGame;

struct ApplicationDesc {
    std::wstring windowTitle   = L"Engine";
    uint32_t     windowWidth   = 1280;
    uint32_t     windowHeight  = 720;
    bool         vsync         = true;
    IGame*       game          = nullptr;
    std::string  startScenePath;
};

} // namespace engine::app
