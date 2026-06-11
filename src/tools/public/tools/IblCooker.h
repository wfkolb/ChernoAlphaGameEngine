#pragma once
#include <filesystem>

namespace engine::tools {

// Cook an equirectangular HDR image into a v4 .easset IBL file.
//
// The output file contains:
//   CMAP section — prefiltered environment cubemap (7 mip levels, 6 faces, R16G16B16A16_FLOAT)
//   BRDF section — split-sum BRDF LUT (256×256, R16G16_UNORM)
//
// hdrPath    : path to an equirectangular .hdr or .exr image (loaded via stb_image).
// outputPath : destination .easset path; parent directories are created if needed.
//
// Returns true on success; false on any I/O or decode error.
//
// Notes:
//   - This is an offline tool. Performance is secondary to correctness.
//   - The prefilter uses a simplified box-filter approximation per mip
//     (not full GGX importance sampling) for acceptable offline cook times.
//   - The BRDF LUT uses Monte Carlo integration with 64 samples per texel.
bool cookIblAsset(const std::filesystem::path& hdrPath,
                  const std::filesystem::path& outputPath);

} // namespace engine::tools
