#pragma once
#include <vector>
#include <cstdint>

namespace engine::tools {

// One mip level of a texture (CPU-side, before GPU upload).
struct CpuMipLevel {
    std::vector<uint8_t> pixels;
    uint32_t             width  = 0;
    uint32_t             height = 0;
};

// A texture loaded from a .easset TEX section.
// dxgiFormat is a DXGI_FORMAT value (e.g. 28 = DXGI_FORMAT_R8G8B8A8_UNORM).
struct CpuTexture {
    std::vector<CpuMipLevel> mips;
    uint32_t                 dxgiFormat = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    uint32_t                 baseWidth  = 0;
    uint32_t                 baseHeight = 0;

    bool isValid() const noexcept { return !mips.empty() && baseWidth > 0; }
};

} // namespace engine::tools
