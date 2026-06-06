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
    uint16_t     hostPort      = 0;        // non-zero = start as server on this port
    std::string  connectAddr;              // non-empty = connect as client to "ip:port"
};

} // namespace engine::app
