#pragma once
#include <rendering/Mesh.h>
#include <tools/AssetImporter.h>
#include <tools/CpuTexture.h>
#include <filesystem>
#include <optional>
#include <vector>
#include <array>
#include <cstdint>

namespace engine::tools {

// Collision geometry extracted from a .easset version-2 file.
struct CpuCollision {
    CollisionType                          type;
    std::vector<std::array<float, 3>>      vertices; // position-only, no normal/UV
    std::vector<uint32_t>                  indices;  // empty for ConvexHull
};

struct CpuMesh {
    std::vector<rendering::VertexStatic> vertices;
    std::vector<uint32_t>               indices;
    std::optional<CpuCollision>         collision; // nullopt if no collision section
    std::vector<CpuTexture>             textures;  // empty if no TEX sections (backward compat)
};

// ---------------------------------------------------------------------------
// IBL (Image-Based Lighting) CPU data structures
// ---------------------------------------------------------------------------

// One face+mip level of a prefiltered cubemap.
// pixels contains R16G16B16A16_FLOAT raw bytes (8 bytes per texel).
struct CpuCubemapMip {
    uint32_t             width;
    uint32_t             height;
    uint32_t             face;      // 0-5 (cubemap face index: +X,-X,+Y,-Y,+Z,-Z)
    uint32_t             mipLevel;
    std::vector<uint8_t> pixels;   // R16G16B16A16_FLOAT raw bytes
};

// Prefiltered environment cubemap with mip chain for PBR specular IBL.
// mipCount == 7 for the standard roughness range (roughness 0..1 mapped to mip 0..6).
struct CpuCubemap {
    uint32_t                   baseSize;    // width == height of face at mip 0
    uint32_t                   mipCount;    // typically 7
    uint32_t                   faceCount;   // always 6
    std::vector<CpuCubemapMip> mips;        // ordered: face0/mip0, face0/mip1, ..., face5/mip6

    bool isValid() const noexcept { return !mips.empty() && mipCount > 0; }
};

// Precomputed BRDF integration LUT for split-sum IBL approximation.
// pixels stores R16G16_UNORM (4 bytes per texel — R16 scale, G16 bias).
// Lookup: uv = (NdotV, roughness); result = (DFG scale, DFG bias).
struct CpuBrdfLut {
    uint32_t             width;    // 256
    uint32_t             height;   // 256
    std::vector<uint8_t> pixels;   // R16G16_UNORM raw bytes (4 bytes per texel)

    bool isValid() const noexcept { return !pixels.empty(); }
};

// Combined IBL data returned by loadIblEasset().
// Present when the .easset file contains CMAP and/or BRDF sections.
struct IblData {
    CpuCubemap prefilteredEnvMap;
    CpuBrdfLut brdfLut;

    bool hasEnvMap()  const noexcept { return prefilteredEnvMap.isValid(); }
    bool hasBrdfLut() const noexcept { return brdfLut.isValid(); }
};

// ---------------------------------------------------------------------------
// Load functions
// ---------------------------------------------------------------------------

// Load a .easset file written by importGltf().
// Returns nullopt on any error: missing file, bad magic, version mismatch,
// unsupported asset type, truncated data.
// Version-1 files are accepted; collision will be std::nullopt.
std::optional<CpuMesh> loadEasset(const std::filesystem::path& path);

// Load a .easset v4 IBL asset written by cookIblAsset().
// Returns nullopt if the file is missing, corrupt, or not a v4 IBL asset.
// On success, the returned IblData may have a valid prefilteredEnvMap,
// a valid brdfLut, or both, depending on which sections the file contains.
std::optional<IblData> loadIblEasset(const std::filesystem::path& path);

} // namespace engine::tools
