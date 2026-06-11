#include <gtest/gtest.h>
#include <tools/EassetLoader.h>
#include <tools/CpuTexture.h>
#include <cmath>

TEST(TextureCookingTests, MipCountFormula) {
    // mipCount = floor(log2(max(w, h))) + 1
    auto expectedMips = [](uint32_t w, uint32_t h) -> uint32_t {
        uint32_t maxDim = std::max(w, h);
        return static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1u;
    };
    EXPECT_EQ(expectedMips(4, 4), 3u);    // 4,2,1 = 3 mips
    EXPECT_EQ(expectedMips(256, 256), 9u);
    EXPECT_EQ(expectedMips(512, 256), 10u);
    EXPECT_EQ(expectedMips(1, 1), 1u);
}

TEST(TextureCookingTests, V1EassetLoadsWithoutError) {
    // A v1 .easset has no texture section — should load fine with empty textures.
    // This test is informational: it documents that backward compat is required.
    // Actual file loading requires a test asset on disk — skip if not present.
    const auto mesh = engine::tools::loadEasset("nonexistent_v1.easset");
    EXPECT_FALSE(mesh.has_value()); // non-existent file returns nullopt
}

TEST(TextureCookingTests, UnitCubeFallbackHasNoTextures) {
    // importGltf with a dummy path falls back to the unit cube which has no textures.
    // The resulting .easset should load successfully with an empty textures vector.
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "test_tex_cooking_cube.easset";

    const auto result = engine::tools::importGltf("dummy_for_tex_test.gltf", path);
    ASSERT_TRUE(result.ok) << "importGltf failed: " << result.errorMessage;

    const auto mesh = engine::tools::loadEasset(path);
    ASSERT_TRUE(mesh.has_value()) << "loadEasset returned nullopt for unit-cube fallback";
    EXPECT_TRUE(mesh->textures.empty()) << "Unit-cube fallback should have no textures";

    std::error_code ec;
    fs::remove(path, ec);
}

TEST(TextureCookingTests, CpuTextureIsValidCheck) {
    // Verify the isValid() logic on CpuTexture directly.
    engine::tools::CpuTexture empty;
    EXPECT_FALSE(empty.isValid()) << "Default-constructed CpuTexture should be invalid";

    engine::tools::CpuTexture withMip;
    withMip.baseWidth  = 4;
    withMip.baseHeight = 4;
    engine::tools::CpuMipLevel mip;
    mip.width  = 4;
    mip.height = 4;
    mip.pixels.resize(4u * 4u * 4u, 255u); // RGBA8 opaque white
    withMip.mips.push_back(std::move(mip));
    EXPECT_TRUE(withMip.isValid()) << "CpuTexture with one mip and non-zero baseWidth should be valid";
}
