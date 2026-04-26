#include <gtest/gtest.h>

#include <tools/AssetImporter.h>
#include <tools/EassetLoader.h>
#include <rendering/GpuDevice.h>
#include <rendering/Window.h>

#include <filesystem>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Test 1 — AssetLoadAndUploadHeadless
//
// Imports a fallback unit-cube .easset to a temp path, round-trips it through
// loadEasset, and asserts the expected vertex and index counts.
// Pure CPU — no GPU required.
// ---------------------------------------------------------------------------
TEST(SpinDemoTest, AssetLoadAndUploadHeadless)
{
    const fs::path eassetPath =
        fs::temp_directory_path() / "spin_demo_test_asset.easset";

    // Cleanup guard — removes the file whether the test passes or fails.
    struct Guard {
        const fs::path& p;
        ~Guard() { std::error_code ec; fs::remove(p, ec); }
    } guard{eassetPath};

    const engine::tools::ImportResult result =
        engine::tools::importGltf("", eassetPath);
    ASSERT_TRUE(result.ok) << "importGltf failed: " << result.errorMessage;

    const auto mesh = engine::tools::loadEasset(eassetPath);
    ASSERT_TRUE(mesh.has_value()) << "loadEasset returned nullopt for a valid .easset";
    EXPECT_EQ(mesh->vertices.size(), 8u);
    EXPECT_EQ(mesh->indices.size(),  36u);
}

// ---------------------------------------------------------------------------
// Test 2 — GpuDeviceSkipsGracefullyHeadless
//
// Creates a Window and GpuDevice.  On a headless / CI machine the swapchain
// will fail; the test skips gracefully via GTEST_SKIP() rather than crashing.
// On a machine with a display it exercises beginFrame / endFrame / flush.
// ---------------------------------------------------------------------------
TEST(SpinDemoTest, GpuDeviceSkipsGracefullyHeadless)
{
    using namespace engine::rendering;

    auto window = Window::create({.width = 64, .height = 64, .title = L"SpinDemoTest"});

    auto device = GpuDevice::create({.window = &window, .vsync = false});
    if (!device.isValid()) {
        GTEST_SKIP() << "GpuDevice swapchain failed (headless/no display)";
    }

    device.beginFrame();
    device.endFrame();
    device.flush();
}
