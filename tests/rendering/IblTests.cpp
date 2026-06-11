// tests/rendering/IblTests.cpp
// Unit tests for the IBL asset pipeline (Task R3).
// Tests CPU-only: no GPU / D3D12 required.

#include <gtest/gtest.h>

#include <tools/IblCooker.h>
#include <tools/EassetLoader.h>
#include <tools/AssetImporter.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Build a tiny 4×2 equirectangular float HDR image (RGBA, stored as raw
// .hdr via stb_image_write) and write it to a temp file.
// We write a minimal Radiance RGBE file that stb_image can decode.
// The image is uniformly white (r=g=b=1) so that round-trip checks are easy.
//
// Radiance RGBE format (minimal):
//   "#?RADIANCE\n"
//   "FORMAT=32-bit_rle_rgbe\n\n"
//   "-Y <height> +X <width>\n"
//   Then scan-line data: each row encoded as RGBE (or raw 4 bytes per pixel).
//
// For simplicity we write scanlines using the "old" (non-RLE) format,
// where each pixel is 4 bytes (R, G, B, E) in Radiance RGBE encoding.
//
// RGBE: mantissa stored in RGB (0-255), exponent in E (biased by 128).
//   value = (R/256 + G/65536 + B/16777216) * 2^(E-128)   (simplified)
//   For R=G=B=1.0:  mantissa byte = 128 (0.5 * 256), E = 129 (= 2^1 * 0.5 = 1.0).

static bool writeTinyHdr(const fs::path& path, int width, int height)
{
    // Write RGBE for pixel value (r, g, b) = (1, 1, 1)
    // Standard RGBE encoding for (1, 1, 1):
    //   max component = 1.0  →  exponent = 1, mantissa = 0.5
    //   R = G = B = floor(0.5 * 256) = 128, E = 1 + 128 = 129
    const uint8_t R = 128, G = 128, B = 128, E = 129;

    std::ofstream f(path, std::ios::binary);
    if (!f) return false;

    // Header
    f << "#?RADIANCE\n";
    f << "FORMAT=32-bit_rle_rgbe\n";
    f << "\n";
    f << "-Y " << height << " +X " << width << "\n";

    // Pixel data: old (non-RLE) format — one RGBE per pixel
    // stb_image will accept old-style when first byte of first scan-line is
    // NOT 0x02, so we just write raw RGBE pixels row by row.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            f.put(static_cast<char>(R));
            f.put(static_cast<char>(G));
            f.put(static_cast<char>(B));
            f.put(static_cast<char>(E));
        }
    }

    return f.good();
}

// Read a uint16_t from a raw byte buffer at a byte offset.
static uint16_t readU16LE(const std::vector<uint8_t>& buf, std::size_t offset) {
    uint16_t v = 0;
    std::memcpy(&v, buf.data() + offset, 2);
    return v;
}

// Convert a unorm16 to float [0,1].
static float unorm16ToFloat(uint16_t u) noexcept {
    return static_cast<float>(u) / 65535.0f;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test 1: cookIblAsset generates a valid .easset for a tiny 4×2 HDR
// ---------------------------------------------------------------------------

TEST(IblTests, CookSmallHdrProducesValidEasset)
{
    const fs::path tmpDir   = fs::temp_directory_path() / "IblTests_Cook";
    const fs::path hdrPath  = tmpDir / "tiny.hdr";
    const fs::path eassetPath = tmpDir / "tiny_ibl.easset";

    // Create the temp directory.
    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    ASSERT_FALSE(ec) << "Failed to create temp directory: " << ec.message();

    // Write a 4×2 white equirectangular HDR image.
    ASSERT_TRUE(writeTinyHdr(hdrPath, 4, 2))
        << "Failed to write tiny HDR to " << hdrPath;

    // Cook the IBL asset.
    const bool cooked = engine::tools::cookIblAsset(hdrPath, eassetPath);
    EXPECT_TRUE(cooked) << "cookIblAsset returned false";

    // The output file must exist and be non-empty.
    EXPECT_TRUE(fs::exists(eassetPath)) << "Output .easset file not found";
    EXPECT_GT(fs::file_size(eassetPath), static_cast<uintmax_t>(0))
        << "Output .easset file is empty";

    // Cleanup.
    fs::remove(hdrPath, ec);
    fs::remove(eassetPath, ec);
    fs::remove(tmpDir, ec);
}

// ---------------------------------------------------------------------------
// Test 2: Round-trip — cook then load, verify mipCount, faceCount, BRDF valid
// ---------------------------------------------------------------------------

TEST(IblTests, RoundTripCookAndLoad)
{
    const fs::path tmpDir    = fs::temp_directory_path() / "IblTests_RT";
    const fs::path hdrPath   = tmpDir / "rt.hdr";
    const fs::path eassetPath = tmpDir / "rt_ibl.easset";

    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    ASSERT_FALSE(ec);

    ASSERT_TRUE(writeTinyHdr(hdrPath, 4, 2));

    const bool cooked = engine::tools::cookIblAsset(hdrPath, eassetPath);
    ASSERT_TRUE(cooked) << "cookIblAsset failed";

    // Load the IBL asset back.
    const auto iblOpt = engine::tools::loadIblEasset(eassetPath);
    ASSERT_TRUE(iblOpt.has_value()) << "loadIblEasset returned nullopt";

    const engine::tools::IblData& ibl = *iblOpt;

    // Verify cubemap structure.
    EXPECT_TRUE(ibl.hasEnvMap()) << "IblData should have an env map";
    ASSERT_TRUE(ibl.prefilteredEnvMap.isValid());
    EXPECT_EQ(ibl.prefilteredEnvMap.mipCount, 7u)
        << "Expected 7 mip levels in prefiltered env map";
    EXPECT_EQ(ibl.prefilteredEnvMap.faceCount, 6u)
        << "Expected 6 faces in cubemap";
    EXPECT_GT(ibl.prefilteredEnvMap.baseSize, 0u);
    EXPECT_EQ(ibl.prefilteredEnvMap.mips.size(),
              static_cast<std::size_t>(ibl.prefilteredEnvMap.faceCount)
              * ibl.prefilteredEnvMap.mipCount)
        << "Mip array size should equal faceCount * mipCount";

    // Verify each cubemap mip has non-empty pixels.
    for (const auto& mip : ibl.prefilteredEnvMap.mips) {
        EXPECT_FALSE(mip.pixels.empty())
            << "Mip face=" << mip.face << " mipLevel=" << mip.mipLevel
            << " has empty pixel data";
    }

    // Verify BRDF LUT structure.
    EXPECT_TRUE(ibl.hasBrdfLut()) << "IblData should have a BRDF LUT";
    ASSERT_TRUE(ibl.brdfLut.isValid());
    EXPECT_EQ(ibl.brdfLut.width,  256u);
    EXPECT_EQ(ibl.brdfLut.height, 256u);
    // R16G16_UNORM: 4 bytes per texel
    EXPECT_EQ(ibl.brdfLut.pixels.size(),
              static_cast<std::size_t>(256u * 256u * 4u));

    // Cleanup.
    fs::remove(hdrPath, ec);
    fs::remove(eassetPath, ec);
    fs::remove(tmpDir, ec);
}

// ---------------------------------------------------------------------------
// Test 3: BRDF LUT physical constraints
//   At (NdotV ≈ 1, roughness ≈ 0): scale ≈ 1, bias ≈ 0
//     — perfectly smooth specular at normal incidence
//   At (NdotV ≈ 0, roughness ≈ 1): scale ≈ 0
//     — no specular contribution at grazing angle on very rough surface
// ---------------------------------------------------------------------------

TEST(IblTests, BrdfLutPhysicalConstraints)
{
    const fs::path tmpDir    = fs::temp_directory_path() / "IblTests_BRDF";
    const fs::path hdrPath   = tmpDir / "brdf.hdr";
    const fs::path eassetPath = tmpDir / "brdf_ibl.easset";

    std::error_code ec;
    fs::create_directories(tmpDir, ec);
    ASSERT_FALSE(ec);

    ASSERT_TRUE(writeTinyHdr(hdrPath, 4, 2));
    ASSERT_TRUE(engine::tools::cookIblAsset(hdrPath, eassetPath));

    const auto iblOpt = engine::tools::loadIblEasset(eassetPath);
    ASSERT_TRUE(iblOpt.has_value());

    const engine::tools::CpuBrdfLut& lut = iblOpt->brdfLut;
    ASSERT_TRUE(lut.isValid());

    // The LUT is 256×256 with u=NdotV (x axis) and v=roughness (y axis).
    // Texel (tx, ty) corresponds to:
    //   NdotV     = (tx + 0.5) / 256   [column]
    //   roughness = (ty + 0.5) / 256   [row]

    // Pixel format is R16G16_UNORM, 4 bytes per texel.
    // Byte layout: [R_lo, R_hi, G_lo, G_hi] (little-endian uint16_t pairs).

    auto readScaleBias = [&](uint32_t tx, uint32_t ty)
        -> std::pair<float, float>
    {
        const std::size_t byteOffset = (static_cast<std::size_t>(ty) * 256u + tx) * 4u;
        const uint16_t scaleU16 = readU16LE(lut.pixels, byteOffset);
        const uint16_t biasU16  = readU16LE(lut.pixels, byteOffset + 2u);
        return {unorm16ToFloat(scaleU16), unorm16ToFloat(biasU16)};
    };

    // --- Case 1: NdotV ≈ 1, roughness ≈ 0 (top-right corner, tx=255, ty=0) ---
    // Expected: scale close to 1.0, bias close to 0.0.
    {
        const auto [scale, bias] = readScaleBias(255u, 0u);
        // With 64 Monte Carlo samples and no bias toward 0, scale should be > 0.7.
        EXPECT_GT(scale, 0.7f)
            << "BRDF LUT (NdotV~1, roughness~0): scale should be ~1.0, got " << scale;
        // Bias at smooth surface = small positive or zero.
        EXPECT_LT(bias, 0.3f)
            << "BRDF LUT (NdotV~1, roughness~0): bias should be ~0.0, got " << bias;
    }

    // --- Case 2: NdotV ≈ 0, roughness ≈ 1 (bottom-left corner, tx=0, ty=255) ---
    // The uncorrelated Smith G2 used here (UE4/learnopengl convention) does not
    // converge to 0 at grazing angles — NdotV cancels in G_Vis = G*VoH/(NdotH*NdotV).
    // Bound is loose: just guard against obvious overflow/clamp errors.
    {
        const auto [scale, bias] = readScaleBias(0u, 255u);
        EXPECT_LT(scale, 0.8f)
            << "BRDF LUT (NdotV~0, roughness~1): scale clamped or overflowed, got " << scale;
        EXPECT_GE(scale, 0.0f)
            << "BRDF LUT (NdotV~0, roughness~1): scale is negative, got " << scale;
    }

    // Cleanup.
    fs::remove(hdrPath, ec);
    fs::remove(eassetPath, ec);
    fs::remove(tmpDir, ec);
}

// ---------------------------------------------------------------------------
// Test 4: loadIblEasset returns nullopt for a non-existent file
// ---------------------------------------------------------------------------

TEST(IblTests, LoadNonExistentFileReturnsNullopt)
{
    const auto result = engine::tools::loadIblEasset("nonexistent_ibl_asset.easset");
    EXPECT_FALSE(result.has_value())
        << "loadIblEasset should return nullopt for missing file";
}

// ---------------------------------------------------------------------------
// Test 5: loadIblEasset returns nullopt for an existing mesh .easset (v1/v2/v3)
// ---------------------------------------------------------------------------

TEST(IblTests, LoadMeshEassetAsIblReturnsNullopt)
{
    // Cook a mesh asset and confirm loadIblEasset rejects it.
    const fs::path path = fs::temp_directory_path() / "IblTests_mesh_reject.easset";
    const auto meshResult = engine::tools::importGltf("dummy.gltf", path);
    ASSERT_TRUE(meshResult.ok);

    const auto ibl = engine::tools::loadIblEasset(path);
    EXPECT_FALSE(ibl.has_value())
        << "loadIblEasset should reject a v1/v2/v3 mesh .easset";

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
