#pragma once
#include <vector>
#include <cstdint>

namespace engine::rendering {
    class GpuDevice;
    struct RGBA8 { uint8_t r, g, b, a; };
    std::vector<RGBA8> readbackBackBuffer(GpuDevice& device, int x, int y, int w, int h);
}
