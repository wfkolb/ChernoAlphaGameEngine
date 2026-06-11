#include "tools/IblCooker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

// stb_image for HDR decode — implementation is in StbImageImpl.cpp
#include <stb_image.h>

// ---------------------------------------------------------------------------
// Internal binary layout (must match EassetLoader.cpp exactly)
// ---------------------------------------------------------------------------

namespace {

#pragma pack(push, 1)

struct EassHeader {
    char     magic[4];       // "EASS"
    uint16_t version;        // 4 for IBL assets
    uint16_t assetType;      // 1 = IBL
    uint32_t totalSize;
    uint32_t tocOffset;
    uint32_t tocEntryCount;
};
static_assert(sizeof(EassHeader) == 20);

struct TocEntry {
    char     id[4];
    uint32_t offset;
    uint32_t size;
    uint32_t reserved;
};
static_assert(sizeof(TocEntry) == 16);

struct CmapSectionHeader {
    uint32_t faceCount;
    uint32_t mipCount;
    uint32_t baseSize;
};
static_assert(sizeof(CmapSectionHeader) == 12);

struct CmapMipHeader {
    uint32_t face;
    uint32_t mipLevel;
    uint32_t dataSize;
};
static_assert(sizeof(CmapMipHeader) == 12);

struct BrdfSectionHeader {
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(BrdfSectionHeader) == 8);

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Math helpers (no dependency on engine::core to keep this tool self-contained)
// ---------------------------------------------------------------------------

struct Vec3 {
    float x, y, z;
};

inline Vec3 normalize(Vec3 v) noexcept {
    const float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len < 1e-8f) return {0.0f, 1.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

inline float dot(Vec3 a, Vec3 b) noexcept {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

inline Vec3 operator*(Vec3 v, float s) noexcept { return {v.x*s, v.y*s, v.z*s}; }

static const float kPi = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// float-to-half (IEEE 754 half precision) — no FP16 intrinsics needed
// ---------------------------------------------------------------------------

// Converts a single-precision float to a 16-bit half-float.
// Handles denormals, Inf, and NaN conservatively.
static uint16_t floatToHalf(float f) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);

    const uint32_t sign     = (bits >> 31) & 0x1u;
    const uint32_t exponent = (bits >> 23) & 0xFFu;
    const uint32_t mantissa =  bits        & 0x7FFFFFu;

    if (exponent == 255u) {
        // Inf or NaN
        return static_cast<uint16_t>((sign << 15) | 0x7C00u | (mantissa ? 0x0200u : 0u));
    }

    if (exponent == 0u) {
        // Zero / denormal — flush to zero
        return static_cast<uint16_t>(sign << 15);
    }

    const int32_t newExp = static_cast<int32_t>(exponent) - 127 + 15;
    if (newExp <= 0) {
        // Too small — flush to zero
        return static_cast<uint16_t>(sign << 15);
    }
    if (newExp >= 31) {
        // Overflow — clamp to max half (65504)
        return static_cast<uint16_t>((sign << 15) | 0x7BFFu);
    }

    const uint16_t halfExp = static_cast<uint16_t>(newExp) << 10;
    const uint16_t halfMan = static_cast<uint16_t>(mantissa >> 13);
    return static_cast<uint16_t>((sign << 15) | halfExp | halfMan);
}

// float to unorm16 (clamp [0,1] → [0,65535])
static uint16_t floatToUnorm16(float f) noexcept {
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    return static_cast<uint16_t>(f * 65535.0f + 0.5f);
}

// ---------------------------------------------------------------------------
// Equirectangular HDR image
// ---------------------------------------------------------------------------

struct HdrImage {
    int            width  = 0;
    int            height = 0;
    std::vector<float> pixels; // RGBA float, width*height*4
};

// Sample the equirectangular image using the given direction.
// dir must be normalised.  Returns linear RGB (fourth component ignored).
static Vec3 sampleEquirect(const HdrImage& img, Vec3 dir) noexcept {
    // Spherical coordinates: phi in [-pi, pi], theta in [0, pi]
    const float phi   = std::atan2(dir.z, dir.x);              // longitude
    const float theta = std::acos(std::max(-1.0f, std::min(1.0f, dir.y))); // latitude

    float u = (phi   + kPi) / (2.0f * kPi);   // [0,1]
    float v = theta / kPi;                      // [0,1]

    // Bilinear sample
    const float px = u * static_cast<float>(img.width  - 1);
    const float py = v * static_cast<float>(img.height - 1);

    const int x0 = static_cast<int>(px);
    const int y0 = static_cast<int>(py);
    const int x1 = std::min(x0 + 1, img.width  - 1);
    const int y1 = std::min(y0 + 1, img.height - 1);

    const float fx = px - static_cast<float>(x0);
    const float fy = py - static_cast<float>(y0);

    auto pixel = [&](int xi, int yi) -> Vec3 {
        const std::size_t idx = (static_cast<std::size_t>(yi) * img.width + xi) * 4u;
        return {img.pixels[idx], img.pixels[idx+1], img.pixels[idx+2]};
    };

    const Vec3 p00 = pixel(x0, y0);
    const Vec3 p10 = pixel(x1, y0);
    const Vec3 p01 = pixel(x0, y1);
    const Vec3 p11 = pixel(x1, y1);

    // Bilinear lerp
    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx           * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx           * fy;

    return {
        p00.x*w00 + p10.x*w10 + p01.x*w01 + p11.x*w11,
        p00.y*w00 + p10.y*w10 + p01.y*w01 + p11.y*w11,
        p00.z*w00 + p10.z*w10 + p01.z*w01 + p11.z*w11,
    };
}

// ---------------------------------------------------------------------------
// Cubemap face direction helpers
// ---------------------------------------------------------------------------

// For a given face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z)
// and normalised (s,t) in [-1,1], return the world-space direction.
// Convention: right-handed Y-up, consistent with HLSL TextureCube.
static Vec3 faceUvToDir(int face, float s, float t) noexcept {
    // DX12 / HLSL cubemap face convention:
    //  0 = +X face: right=+Z, up=-Y, forward=-X (face normal = +X)
    //  1 = -X face: right=-Z, up=-Y, forward=+X
    //  2 = +Y face: right=+X, up=+Z, forward=+Y
    //  3 = -Y face: right=+X, up=-Z, forward=-Y
    //  4 = +Z face: right=-X, up=-Y, forward=+Z
    //  5 = -Z face: right=+X, up=-Y, forward=-Z
    Vec3 dir{};
    switch (face) {
        case 0: dir = { 1.0f,  -t,  -s}; break; // +X
        case 1: dir = {-1.0f,  -t,   s}; break; // -X
        case 2: dir = {   s,  1.0f,   t}; break; // +Y
        case 3: dir = {   s, -1.0f,  -t}; break; // -Y
        case 4: dir = {   s,   -t,  1.0f}; break; // +Z
        case 5: dir = {  -s,   -t, -1.0f}; break; // -Z
        default: break;
    }
    return normalize(dir);
}

// ---------------------------------------------------------------------------
// Prefiltered environment map baking
//
// Approach: simplified box-filter approximation.
//   mip 0 (roughness=0): direct equirectangular sample (no filtering).
//   mip k (k>0):         box-filter the equirectangular image by sampling
//                        a disc of angular radius proportional to roughness²,
//                        using a fixed grid of sampleCount × sampleCount samples
//                        uniformly spread over the solid angle cone.
//
// This is an acceptable v1 approximation — it blurs correctly with roughness
// but does not weight by the GGX NDF. A full importance-sampling implementation
// can replace this in a future phase without changing the file format.
// ---------------------------------------------------------------------------

// Number of samples along each axis of the jitter grid for mip > 0.
static const int kPrefilterGridSamples = 8; // 64 total per texel

static std::vector<uint8_t> bakeFaceHalf(
    const HdrImage& hdr,
    int             face,
    uint32_t        size,     // texel size of this face
    float           roughness // 0.0 = specular, 1.0 = diffuse
) {
    // R16G16B16A16_FLOAT: 8 bytes per texel
    const std::size_t texelCount = static_cast<std::size_t>(size) * size;
    std::vector<uint8_t> out(texelCount * 8u, 0);

    const float invSize = 1.0f / static_cast<float>(size);
    // Angular radius of the filter kernel (in radians); zero for mip 0.
    const float angularRadius = roughness * roughness * (kPi * 0.5f);

    for (uint32_t ty = 0; ty < size; ++ty) {
        for (uint32_t tx = 0; tx < size; ++tx) {
            // Centre of texel in [-1,1] cube-face space
            const float s = (static_cast<float>(tx) + 0.5f) * 2.0f * invSize - 1.0f;
            const float t = (static_cast<float>(ty) + 0.5f) * 2.0f * invSize - 1.0f;
            const Vec3  centerDir = faceUvToDir(face, s, t);

            Vec3 accumulated{0.0f, 0.0f, 0.0f};
            int  sampleCount = 0;

            if (roughness <= 0.0f) {
                // Mip 0: direct sample
                accumulated = sampleEquirect(hdr, centerDir);
                sampleCount = 1;
            } else {
                // Build a tangent frame around centerDir for jitter.
                // Pick an up vector not parallel to centerDir.
                Vec3 up = (std::abs(centerDir.y) < 0.99f)
                        ? Vec3{0.0f, 1.0f, 0.0f}
                        : Vec3{1.0f, 0.0f, 0.0f};

                // tangent = normalize(cross(up, centerDir))
                Vec3 tangent = normalize({
                    up.y * centerDir.z - up.z * centerDir.y,
                    up.z * centerDir.x - up.x * centerDir.z,
                    up.x * centerDir.y - up.y * centerDir.x
                });
                // bitangent = cross(centerDir, tangent)
                Vec3 bitangent = {
                    centerDir.y * tangent.z - centerDir.z * tangent.y,
                    centerDir.z * tangent.x - centerDir.x * tangent.z,
                    centerDir.x * tangent.y - centerDir.y * tangent.x
                };

                const float maxOffset = std::tan(angularRadius);
                const float step      = 2.0f * maxOffset / static_cast<float>(kPrefilterGridSamples);

                for (int jy = 0; jy < kPrefilterGridSamples; ++jy) {
                    for (int jx = 0; jx < kPrefilterGridSamples; ++jx) {
                        const float ou = -maxOffset + (static_cast<float>(jx) + 0.5f) * step;
                        const float ov = -maxOffset + (static_cast<float>(jy) + 0.5f) * step;

                        // Perturb centerDir by (ou, ov) in the tangent plane.
                        Vec3 sampleDir = normalize({
                            centerDir.x + ou * tangent.x + ov * bitangent.x,
                            centerDir.y + ou * tangent.y + ov * bitangent.y,
                            centerDir.z + ou * tangent.z + ov * bitangent.z
                        });

                        const Vec3 col = sampleEquirect(hdr, sampleDir);
                        accumulated.x += col.x;
                        accumulated.y += col.y;
                        accumulated.z += col.z;
                        ++sampleCount;
                    }
                }
            }

            const float invCount = 1.0f / static_cast<float>(std::max(sampleCount, 1));
            const Vec3  final    = accumulated * invCount;

            const std::size_t byteOffset = (static_cast<std::size_t>(ty) * size + tx) * 8u;
            uint16_t* dst = reinterpret_cast<uint16_t*>(out.data() + byteOffset);
            dst[0] = floatToHalf(final.x); // R
            dst[1] = floatToHalf(final.y); // G
            dst[2] = floatToHalf(final.z); // B
            dst[3] = floatToHalf(1.0f);    // A
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// BRDF LUT baking
//
// Each texel (u, v) represents (NdotV, roughness).
// We compute the two split-sum integrals:
//   scale = integral of F-factor × visibility (Schlick k=0 contribution)
//   bias  = integral of (1-F) × visibility (the bias term)
//
// Using the standard GGX V-cavity geometry with importance sampling of the
// GGX NDF in the V=N plane (the V-term method from "Real Shading in UE4").
// Here we use a simpler but correct Monte Carlo integration over the hemisphere.
// ---------------------------------------------------------------------------

static const int kBrdfSamples = 64;
static const uint32_t kBrdfSize = 256u;

// Van der Corput sequence (base 2) — radical inverse.
static float radicalInverseBase2(uint32_t bits) noexcept {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// Hammersley point set: i in [0, numSamples).
static std::array<float, 2> hammersley(uint32_t i, uint32_t numSamples) noexcept {
    return {static_cast<float>(i) / static_cast<float>(numSamples),
            radicalInverseBase2(i)};
}

// GGX importance sample in tangent space.
// Returns a half vector H in tangent space (z=up).
static Vec3 importanceSampleGGX(float xi1, float xi2, float roughness) noexcept {
    const float a    = roughness * roughness;
    const float phi  = 2.0f * kPi * xi1;
    const float cosT = std::sqrt((1.0f - xi2) / (1.0f + (a*a - 1.0f) * xi2 + 1e-7f));
    const float sinT = std::sqrt(1.0f - cosT * cosT);

    return {sinT * std::cos(phi), sinT * std::sin(phi), cosT};
}

// Smith-Schlick-GGX geometry term for BRDF integration.
static float g_SchlickGGX_IBL(float NdotV, float roughness) noexcept {
    const float k = (roughness * roughness) * 0.5f;
    return NdotV / (NdotV * (1.0f - k) + k + 1e-7f);
}

static float g_Smith_IBL(float NdotV, float NdotL, float roughness) noexcept {
    return g_SchlickGGX_IBL(NdotV, roughness) * g_SchlickGGX_IBL(NdotL, roughness);
}

// Compute the (scale, bias) BRDF LUT value for a given (NdotV, roughness).
static std::array<float, 2> integrateBRDF(float NdotV, float roughness) noexcept {
    NdotV = std::max(NdotV, 1e-4f);
    roughness = std::max(roughness, 1e-4f);

    // Build tangent-space view vector: V lies in the xz-plane, NdotV = cos(angle from z).
    const float VsinT = std::sqrt(1.0f - NdotV * NdotV);
    const Vec3  V     = {VsinT, 0.0f, NdotV};
    const Vec3  N     = {0.0f,  0.0f, 1.0f};

    float scale = 0.0f;
    float bias  = 0.0f;

    for (uint32_t i = 0; i < static_cast<uint32_t>(kBrdfSamples); ++i) {
        const auto xi   = hammersley(i, static_cast<uint32_t>(kBrdfSamples));
        const Vec3 H    = importanceSampleGGX(xi[0], xi[1], roughness);
        const float HdotV = std::max(dot(H, V), 0.0f);

        // Reflect V around H to get L.
        // L = 2*(H.V)*H - V
        const Vec3 L = normalize({
            2.0f * HdotV * H.x - V.x,
            2.0f * HdotV * H.y - V.y,
            2.0f * HdotV * H.z - V.z
        });

        const float NdotL = std::max(dot(N, L), 0.0f);
        const float NdotH = std::max(dot(N, H), 0.0f);

        if (NdotL > 0.0f) {
            const float G    = g_Smith_IBL(NdotV, NdotL, roughness);
            // Geometry term weighted by PDF cancellation: G * VdotH / (NdotH * NdotV)
            const float G_Vis = (G * HdotV) / (NdotH * NdotV + 1e-7f);
            // Schlick approximation: Fc = (1 - VdotH)^5
            const float Fc = std::pow(1.0f - HdotV, 5.0f);
            scale += (1.0f - Fc) * G_Vis;
            bias  += Fc          * G_Vis;
        }
    }

    scale /= static_cast<float>(kBrdfSamples);
    bias  /= static_cast<float>(kBrdfSamples);
    return {scale, bias};
}

// Bake the full 256×256 BRDF LUT into R16G16_UNORM bytes.
// u axis = NdotV, v axis = roughness.
static std::vector<uint8_t> bakeBrdfLut() {
    const std::size_t texelCount = static_cast<std::size_t>(kBrdfSize) * kBrdfSize;
    // R16G16_UNORM: 4 bytes per texel (2 × uint16_t)
    std::vector<uint8_t> out(texelCount * 4u, 0);

    for (uint32_t ty = 0; ty < kBrdfSize; ++ty) {
        // v (row) maps to roughness: ty=0 → roughness=0, ty=kBrdfSize-1 → roughness=1
        const float roughness =
            (static_cast<float>(ty) + 0.5f) / static_cast<float>(kBrdfSize);

        for (uint32_t tx = 0; tx < kBrdfSize; ++tx) {
            // u (column) maps to NdotV: tx=0 → NdotV≈0, tx=kBrdfSize-1 → NdotV≈1
            const float NdotV =
                (static_cast<float>(tx) + 0.5f) / static_cast<float>(kBrdfSize);

            const auto [sc, bi] = integrateBRDF(NdotV, roughness);

            const std::size_t byteOffset = (static_cast<std::size_t>(ty) * kBrdfSize + tx) * 4u;
            uint16_t* dst = reinterpret_cast<uint16_t*>(out.data() + byteOffset);
            dst[0] = floatToUnorm16(sc);  // R16 = scale
            dst[1] = floatToUnorm16(bi);  // G16 = bias
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Alignment helper
// ---------------------------------------------------------------------------

static void padTo64(std::fstream& fs) {
    const auto pos = static_cast<uint32_t>(fs.tellp());
    const uint32_t rem = pos % 64u;
    if (rem == 0u) return;
    const uint32_t needed = 64u - rem;
    const uint8_t  zero   = 0u;
    for (uint32_t i = 0u; i < needed; ++i)
        fs.write(reinterpret_cast<const char*>(&zero), 1);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public implementation
// ---------------------------------------------------------------------------

namespace engine::tools {

bool cookIblAsset(const std::filesystem::path& hdrPath,
                  const std::filesystem::path& outputPath)
{
    // ------------------------------------------------------------------
    // 1. Load equirectangular HDR image
    // ------------------------------------------------------------------
    int imgW = 0, imgH = 0, imgComp = 0;
    float* rawPixels = stbi_loadf(hdrPath.string().c_str(), &imgW, &imgH, &imgComp, 4);
    if (!rawPixels || imgW <= 0 || imgH <= 0) {
        if (rawPixels) stbi_image_free(rawPixels);
        return false;
    }

    HdrImage hdr;
    hdr.width  = imgW;
    hdr.height = imgH;
    const std::size_t pixelCount = static_cast<std::size_t>(imgW) * imgH * 4u;
    hdr.pixels.assign(rawPixels, rawPixels + pixelCount);
    stbi_image_free(rawPixels);

    // ------------------------------------------------------------------
    // 2. Bake prefiltered cubemap: 6 faces × 7 mip levels
    // ------------------------------------------------------------------
    static const uint32_t kBaseSize  = 512u;
    static const uint32_t kMipCount  = 7u;
    static const uint32_t kFaceCount = 6u;

    // Per-mip roughness: mip k → roughness = k / (kMipCount - 1)
    struct FaceData {
        uint32_t             face;
        uint32_t             mipLevel;
        uint32_t             width;
        uint32_t             height;
        std::vector<uint8_t> pixels; // R16G16B16A16_FLOAT
    };

    std::vector<FaceData> cubemapMips;
    cubemapMips.reserve(kFaceCount * kMipCount);

    for (uint32_t mip = 0; mip < kMipCount; ++mip) {
        const float roughness =
            (kMipCount > 1u) ? (static_cast<float>(mip) / static_cast<float>(kMipCount - 1u))
                             : 0.0f;
        const uint32_t mipSize = std::max(1u, kBaseSize >> mip);

        for (uint32_t face = 0; face < kFaceCount; ++face) {
            FaceData fd;
            fd.face     = face;
            fd.mipLevel = mip;
            fd.width    = mipSize;
            fd.height   = mipSize;
            fd.pixels   = bakeFaceHalf(hdr, static_cast<int>(face), mipSize, roughness);
            cubemapMips.push_back(std::move(fd));
        }
    }

    // ------------------------------------------------------------------
    // 3. Bake BRDF LUT
    // ------------------------------------------------------------------
    std::vector<uint8_t> brdfPixels = bakeBrdfLut();

    // ------------------------------------------------------------------
    // 4. Compute sizes and layout
    // ------------------------------------------------------------------

    // CMAP section size: header + (faceCount × mipCount × (CmapMipHeader + pixelData))
    uint32_t cmapSectionSize = sizeof(CmapSectionHeader);
    for (const auto& fd : cubemapMips) {
        cmapSectionSize += sizeof(CmapMipHeader);
        cmapSectionSize += static_cast<uint32_t>(fd.pixels.size());
    }

    // BRDF section size: header + pixel data
    const uint32_t brdfSectionSize = sizeof(BrdfSectionHeader)
                                   + static_cast<uint32_t>(brdfPixels.size());

    // File layout:
    //   [0..19]     EassHeader (20 bytes)
    //   [20..51]    TocEntry × 2 (32 bytes)
    //   pad to 64
    //   CMAP section (64-byte aligned)
    //   BRDF section (64-byte aligned)

    constexpr uint32_t kHeaderSize   = sizeof(EassHeader);    // 20
    constexpr uint32_t kTocEntrySize = sizeof(TocEntry);      // 16
    constexpr uint32_t kAlignment    = 64u;

    const uint32_t tocOffset      = kHeaderSize;               // 20
    const uint32_t rawDataStart   = kHeaderSize + 2u * kTocEntrySize; // 20 + 32 = 52

    const uint32_t cmapSectionOffset =
        (rawDataStart + kAlignment - 1u) & ~(kAlignment - 1u); // ceil to 64 = 64

    const uint32_t afterCmap = cmapSectionOffset + cmapSectionSize;
    const uint32_t brdfSectionOffset =
        (afterCmap + kAlignment - 1u) & ~(kAlignment - 1u);

    const uint32_t totalSize = brdfSectionOffset + brdfSectionSize;

    // ------------------------------------------------------------------
    // 5. Write the .easset file
    // ------------------------------------------------------------------
    std::filesystem::create_directories(outputPath.parent_path());

    std::fstream fs(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!fs.is_open()) return false;

    // Header
    EassHeader hdrOut{};
    std::memcpy(hdrOut.magic, "EASS", 4);
    hdrOut.version       = 4u;
    hdrOut.assetType     = 1u;  // IBL
    hdrOut.totalSize     = totalSize;
    hdrOut.tocOffset     = tocOffset;
    hdrOut.tocEntryCount = 2u;
    fs.write(reinterpret_cast<const char*>(&hdrOut), sizeof(hdrOut));

    // CMAP TocEntry
    TocEntry cmapToc{};
    std::memcpy(cmapToc.id, "CMAP", 4);
    cmapToc.offset   = cmapSectionOffset;
    cmapToc.size     = cmapSectionSize;
    cmapToc.reserved = 0u;
    fs.write(reinterpret_cast<const char*>(&cmapToc), sizeof(cmapToc));

    // BRDF TocEntry
    TocEntry brdfToc{};
    std::memcpy(brdfToc.id, "BRDF", 4);
    brdfToc.offset   = brdfSectionOffset;
    brdfToc.size     = brdfSectionSize;
    brdfToc.reserved = 0u;
    fs.write(reinterpret_cast<const char*>(&brdfToc), sizeof(brdfToc));

    // Pad to CMAP section
    padTo64(fs);

    // CMAP section
    {
        CmapSectionHeader csh{};
        csh.faceCount = kFaceCount;
        csh.mipCount  = kMipCount;
        csh.baseSize  = kBaseSize;
        fs.write(reinterpret_cast<const char*>(&csh), sizeof(csh));

        for (const auto& fd : cubemapMips) {
            CmapMipHeader cmh{};
            cmh.face     = fd.face;
            cmh.mipLevel = fd.mipLevel;
            cmh.dataSize = static_cast<uint32_t>(fd.pixels.size());
            fs.write(reinterpret_cast<const char*>(&cmh), sizeof(cmh));
            fs.write(reinterpret_cast<const char*>(fd.pixels.data()),
                     static_cast<std::streamsize>(fd.pixels.size()));
        }
    }

    // Pad to BRDF section
    padTo64(fs);

    // BRDF section
    {
        BrdfSectionHeader bsh{};
        bsh.width  = kBrdfSize;
        bsh.height = kBrdfSize;
        fs.write(reinterpret_cast<const char*>(&bsh), sizeof(bsh));
        fs.write(reinterpret_cast<const char*>(brdfPixels.data()),
                 static_cast<std::streamsize>(brdfPixels.size()));
    }

    return fs.good();
}

} // namespace engine::tools
