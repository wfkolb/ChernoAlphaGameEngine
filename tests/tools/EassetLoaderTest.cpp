#include <gtest/gtest.h>
#include <tools/EassetLoader.h>
#include <tools/AssetImporter.h>
#include <rendering/Camera.h>
#include <core/math/Quat.h>
#include <core/math/Transform.h>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>

using namespace engine::tools;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Fixture — writes a unit-cube .easset to a temp path before each test and
// removes it afterwards.  Mirrors the style of AssetPipelineTest.cpp.
// ---------------------------------------------------------------------------
class EassetLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        path_ = fs::temp_directory_path() / "test_easset_loader.easset";
        const auto result = importGltf("dummy.gltf", path_);
        ASSERT_TRUE(result.ok) << "SetUp: importGltf failed: " << result.errorMessage;
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove(path_, ec);
    }

    fs::path path_;
};

// ---------------------------------------------------------------------------
// Test 1: loadEasset returns a CpuMesh with exactly 8 vertices and 36 indices
//         for the unit-cube fallback.
// ---------------------------------------------------------------------------
TEST_F(EassetLoaderTest, UnitCubeHas8VerticesAnd36Indices)
{
    const auto mesh = loadEasset(path_);
    ASSERT_TRUE(mesh.has_value()) << "loadEasset returned nullopt for a valid file";
    EXPECT_EQ(mesh->vertices.size(), 8u);
    EXPECT_EQ(mesh->indices.size(),  36u);
}

// ---------------------------------------------------------------------------
// Test 2: Every vertex position lies within the unit half-extent [-0.5, 0.5].
// ---------------------------------------------------------------------------
TEST_F(EassetLoaderTest, VertexPositionsWithinUnitHalfExtent)
{
    const auto mesh = loadEasset(path_);
    ASSERT_TRUE(mesh.has_value()) << "loadEasset returned nullopt for a valid file";

    for (std::size_t i = 0; i < mesh->vertices.size(); ++i) {
        const auto& v = mesh->vertices[i];
        EXPECT_GE(v.position[0], -0.5f) << "vertex " << i << " x below -0.5";
        EXPECT_LE(v.position[0],  0.5f) << "vertex " << i << " x above  0.5";
        EXPECT_GE(v.position[1], -0.5f) << "vertex " << i << " y below -0.5";
        EXPECT_LE(v.position[1],  0.5f) << "vertex " << i << " y above  0.5";
        EXPECT_GE(v.position[2], -0.5f) << "vertex " << i << " z below -0.5";
        EXPECT_LE(v.position[2],  0.5f) << "vertex " << i << " z above  0.5";
    }
}

// ---------------------------------------------------------------------------
// Test 3: Overwriting the first 4 magic bytes with "XXXX" must cause
//         loadEasset to return nullopt.
// ---------------------------------------------------------------------------
TEST_F(EassetLoaderTest, CorruptMagicReturnsNullopt)
{
    {
        std::fstream f(path_, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open()) << "Could not open file for corruption";
        f.seekp(0, std::ios::beg);
        f.write("XXXX", 4);
        ASSERT_TRUE(f.good()) << "Write of corrupt magic failed";
    }

    const auto mesh = loadEasset(path_);
    EXPECT_FALSE(mesh.has_value()) << "loadEasset should return nullopt for bad magic";
}

// ---------------------------------------------------------------------------
// Test 4: A path that does not exist must cause loadEasset to return nullopt.
// ---------------------------------------------------------------------------
TEST_F(EassetLoaderTest, NonExistentPathReturnsNullopt)
{
    const fs::path missing = fs::temp_directory_path() / "does_not_exist_easset_loader.easset";

    // Make sure it genuinely does not exist.
    std::error_code ec;
    fs::remove(missing, ec);

    const auto mesh = loadEasset(missing);
    EXPECT_FALSE(mesh.has_value()) << "loadEasset should return nullopt for a missing file";
}

// ---------------------------------------------------------------------------
// Test 5: Truncating the file by removing the last 100 bytes must cause
//         loadEasset to return nullopt.
// ---------------------------------------------------------------------------
TEST_F(EassetLoaderTest, TruncatedFileReturnsNullopt)
{
    // Read the full file into memory.
    std::vector<char> bytes;
    {
        std::ifstream f(path_, std::ios::binary | std::ios::ate);
        ASSERT_TRUE(f.is_open()) << "Could not open file for truncation read";
        const auto size = static_cast<std::size_t>(f.tellg());
        ASSERT_GT(size, 100u) << "File too small to truncate by 100 bytes";
        bytes.resize(size - 100u);
        f.seekg(0, std::ios::beg);
        f.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(f.good()) << "Partial read for truncation failed";
    }

    // Write the truncated content back.
    {
        std::ofstream f(path_, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(f.is_open()) << "Could not open file for truncation write";
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(f.good()) << "Write of truncated content failed";
    }

    const auto mesh = loadEasset(path_);
    EXPECT_FALSE(mesh.has_value()) << "loadEasset should return nullopt for a truncated file";
}

// ---------------------------------------------------------------------------
// Test 6: Patching bytes 4-5 (the uint16_t version field) to value 99 must
//         cause loadEasset to return nullopt.
// ---------------------------------------------------------------------------
TEST_F(EassetLoaderTest, WrongVersionReturnsNullopt)
{
    {
        std::fstream f(path_, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.is_open()) << "Could not open file for version patch";
        f.seekp(4, std::ios::beg);
        const uint16_t badVersion = 99u;
        f.write(reinterpret_cast<const char*>(&badVersion), sizeof(badVersion));
        ASSERT_TRUE(f.good()) << "Write of bad version failed";
    }

    const auto mesh = loadEasset(path_);
    EXPECT_FALSE(mesh.has_value()) << "loadEasset should return nullopt for wrong version";
}

// ---------------------------------------------------------------------------
// SpinDemoMathTest — no fixture, no GPU, labelled "unit" automatically by
// gtest_discover_tests(tools_tests PROPERTIES LABELS "unit") in CMakeLists.
//
// Builds world * view * proj for one full Y-axis spin and asserts every
// element of the resulting MVP matrix is a finite float.
// ---------------------------------------------------------------------------
TEST(SpinDemoMathTest, MvpIsFiniteAfterOneSpin)
{
    using namespace engine::core::math;
    using namespace engine::rendering;

    // World matrix: rotate 360 degrees around Y axis.
    const Quat worldRot = fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f},
                                        2.0f * 3.14159265358979323846f);
    const Mat4 world = toMat4(worldRot);

    // View matrix: camera placed 5 units back on Z, looking at origin.
    Transform camTransform;
    camTransform.position = Vec3{0.0f, 0.0f, -5.0f};
    camTransform.rotation = Quat::identity();
    camTransform.scale    = Vec3{1.0f, 1.0f, 1.0f};
    const Mat4 view = cameraViewMatrix(camTransform);

    // Projection matrix: standard perspective, 16:9, reverse-Z.
    const Camera cam{};
    const float  aspect = 16.0f / 9.0f;
    const Mat4   proj   = cameraProjMatrix(cam, aspect);

    // MVP = world * view * proj
    const Mat4 mvp = world * view * proj;

    // Every one of the 16 floats must be finite (not NaN, not Inf).
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_TRUE(std::isfinite(mvp.m[row][col]))
                << "mvp.m[" << row << "][" << col << "] = " << mvp.m[row][col]
                << " is not finite";
        }
    }
}
