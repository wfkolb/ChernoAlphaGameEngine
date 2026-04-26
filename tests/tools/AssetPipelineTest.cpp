#include <gtest/gtest.h>
#include "tools/AssetImporter.h"
#include <filesystem>
#include <fstream>
#include <cstring>

#pragma pack(push, 1)
struct EassHeader {
    char     magic[4];
    uint16_t version;
    uint16_t assetType;
    uint32_t totalSize;
    uint32_t tocOffset;
    uint32_t tocEntryCount;
};
struct TocEntry {
    char     id[4];
    uint32_t offset;
    uint32_t size;
    uint32_t reserved;
};
struct MeshSectionHeader {
    uint32_t vertexCount;
    uint32_t indexCount;
    uint8_t  vertexLayout;
    uint8_t  pad[3];
    float    aabbMin[3];
    float    aabbMax[3];
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// Test 1: Valid .easset header fields
// ---------------------------------------------------------------------------
TEST(AssetPipelineTest, importGltfWritesValidEassHeader)
{
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "engine_test_AssetPipeline" / "cube.easset";

    std::filesystem::create_directories(outputPath.parent_path());

    const auto result = engine::tools::importGltf("dummy.gltf", outputPath);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    std::ifstream f(outputPath, std::ios::binary);
    ASSERT_TRUE(f.is_open()) << "Could not open output file: " << outputPath;

    EassHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    ASSERT_TRUE(f.good()) << "Failed to read header bytes";

    EXPECT_TRUE(std::strncmp(hdr.magic, "EASS", 4) == 0)
        << "magic mismatch: got '"
        << std::string(hdr.magic, 4) << "'";
    EXPECT_EQ(hdr.version,       static_cast<uint16_t>(1));
    EXPECT_EQ(hdr.assetType,     static_cast<uint16_t>(0));
    EXPECT_EQ(hdr.tocEntryCount, static_cast<uint32_t>(1));

    f.close();
    std::filesystem::remove_all(outputPath.parent_path());
}

// ---------------------------------------------------------------------------
// Test 2: TOC entry points to a valid, 64-byte-aligned mesh section
// ---------------------------------------------------------------------------
TEST(AssetPipelineTest, importGltfTocPointsToMeshSection)
{
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "engine_test_AssetPipeline" / "cube2.easset";

    std::filesystem::create_directories(outputPath.parent_path());

    const auto result = engine::tools::importGltf("dummy.gltf", outputPath);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    std::ifstream f(outputPath, std::ios::binary);
    ASSERT_TRUE(f.is_open()) << "Could not open output file: " << outputPath;

    EassHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    ASSERT_TRUE(f.good()) << "Failed to read header bytes";

    // Seek to the TOC (immediately after header per spec)
    f.seekg(hdr.tocOffset, std::ios::beg);
    ASSERT_TRUE(f.good()) << "Seek to tocOffset failed";

    TocEntry toc{};
    f.read(reinterpret_cast<char*>(&toc), sizeof(toc));
    ASSERT_TRUE(f.good()) << "Failed to read TOC entry";

    EXPECT_TRUE(std::strncmp(toc.id, "MESH", 4) == 0)
        << "TOC id mismatch: got '" << std::string(toc.id, 4) << "'";
    EXPECT_EQ(toc.offset % 64u, 0u)
        << "Mesh section offset " << toc.offset << " is not 64-byte aligned";
    EXPECT_GT(toc.size, 0u) << "TOC size must be > 0";

    f.close();
    std::filesystem::remove_all(outputPath.parent_path());
}

// ---------------------------------------------------------------------------
// Test 3: Mesh geometry matches the unit-cube specification
// ---------------------------------------------------------------------------
TEST(AssetPipelineTest, importGltfMeshGeometryIsUnitCube)
{
    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "engine_test_AssetPipeline" / "cube3.easset";

    std::filesystem::create_directories(outputPath.parent_path());

    const auto result = engine::tools::importGltf("dummy.gltf", outputPath);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    std::ifstream f(outputPath, std::ios::binary);
    ASSERT_TRUE(f.is_open()) << "Could not open output file: " << outputPath;

    EassHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    ASSERT_TRUE(f.good()) << "Failed to read header bytes";

    f.seekg(hdr.tocOffset, std::ios::beg);
    ASSERT_TRUE(f.good()) << "Seek to tocOffset failed";

    TocEntry toc{};
    f.read(reinterpret_cast<char*>(&toc), sizeof(toc));
    ASSERT_TRUE(f.good()) << "Failed to read TOC entry";

    f.seekg(toc.offset, std::ios::beg);
    ASSERT_TRUE(f.good()) << "Seek to mesh section failed";

    MeshSectionHeader mesh{};
    f.read(reinterpret_cast<char*>(&mesh), sizeof(mesh));
    ASSERT_TRUE(f.good()) << "Failed to read MeshSectionHeader";

    EXPECT_EQ(mesh.vertexCount,  8u);
    EXPECT_EQ(mesh.indexCount,  36u);
    EXPECT_EQ(static_cast<unsigned>(mesh.vertexLayout), 1u);

    EXPECT_NEAR(mesh.aabbMin[0], -0.5f, 1e-4f);
    EXPECT_NEAR(mesh.aabbMin[1], -0.5f, 1e-4f);
    EXPECT_NEAR(mesh.aabbMin[2], -0.5f, 1e-4f);
    EXPECT_NEAR(mesh.aabbMax[0],  0.5f, 1e-4f);
    EXPECT_NEAR(mesh.aabbMax[1],  0.5f, 1e-4f);
    EXPECT_NEAR(mesh.aabbMax[2],  0.5f, 1e-4f);

    f.close();
    std::filesystem::remove_all(outputPath.parent_path());
}
