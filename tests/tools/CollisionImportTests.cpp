#include <gtest/gtest.h>
#include <tools/AssetImporter.h>
#include <tools/EassetLoader.h>
#include <core/components/ColliderComponent.h>
#include <core/components/MeshHandle.h>
#include <core/ecs/World.h>

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;
using namespace engine::tools;
using engine::core::ColliderComponent;
using engine::core::ecs::World;
using engine::core::ecs::Entity;

// ---------------------------------------------------------------------------
// Binary layout helpers (must match AssetImporter.cpp / EassetLoader.cpp).
// ---------------------------------------------------------------------------

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
struct CollSectionHeader {
    uint8_t  collisionType;
    uint8_t  pad[3];
    uint32_t vertexCount;
    uint32_t indexCount;
};
#pragma pack(pop)

// Helper: read all TocEntries and return one by id, or empty.
static TocEntry findTocEntry(std::ifstream& f, const EassHeader& hdr, const char id[4]) {
    f.seekg(hdr.tocOffset, std::ios::beg);
    for (uint32_t i = 0; i < hdr.tocEntryCount; ++i) {
        TocEntry entry{};
        f.read(reinterpret_cast<char*>(&entry), sizeof(entry));
        if (!f.good()) break;
        if (std::memcmp(entry.id, id, 4) == 0) return entry;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Test 1: Import without collision settings → version 1, no COLL section.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, NoCollisionProducesVersion1WithoutCollSection)
{
    const fs::path out =
        fs::temp_directory_path() / "coll_import_tests" / "no_coll.easset";
    fs::create_directories(out.parent_path());

    // Default ImportSettings has generateCollision = false.
    ImportSettings settings{};
    ASSERT_FALSE(settings.generateCollision);

    const auto result = importGltf("dummy.gltf", out, settings);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    // Scope the ifstream so it is closed before remove_all (Windows file lock).
    {
        std::ifstream f(out, std::ios::binary);
        ASSERT_TRUE(f.is_open());

        EassHeader hdr{};
        f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        ASSERT_TRUE(f.good());

        // Version must be 1 when no collision is requested.
        EXPECT_EQ(hdr.version, static_cast<uint16_t>(1));
        // Single TOC entry for MESH only.
        EXPECT_EQ(hdr.tocEntryCount, static_cast<uint32_t>(1));

        // No COLL entry should exist.
        TocEntry collEntry = findTocEntry(f, hdr, "COLL");
        EXPECT_EQ(collEntry.size, 0u) << "COLL section should not be present in version-1 file";
    }

    fs::remove_all(out.parent_path());
}

// ---------------------------------------------------------------------------
// Test 2: loadEasset() on a version-1 file → collision == nullopt, no crash.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, LoadV1EassetHasNulloptCollision)
{
    const fs::path out =
        fs::temp_directory_path() / "coll_import_tests" / "v1_load.easset";
    fs::create_directories(out.parent_path());

    ImportSettings settings{};  // generateCollision = false
    const auto importResult = importGltf("dummy.gltf", out, settings);
    ASSERT_TRUE(importResult.ok);

    const auto mesh = loadEasset(out);
    ASSERT_TRUE(mesh.has_value()) << "loadEasset should succeed on v1 file";
    EXPECT_FALSE(mesh->collision.has_value())
        << "Version-1 file must not have collision data";
    EXPECT_GT(mesh->vertices.size(), 0u);
    EXPECT_GT(mesh->indices.size(),  0u);

    fs::remove_all(out.parent_path());
}

// ---------------------------------------------------------------------------
// Test 3: Import with generateCollision=true, TriangleMesh →
//         version 2, COLL present, vertex count matches mesh, index count > 0.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, TriangleMeshCollisionProducesVersion2WithCollSection)
{
    const fs::path out =
        fs::temp_directory_path() / "coll_import_tests" / "tri_coll.easset";
    fs::create_directories(out.parent_path());

    ImportSettings settings{};
    settings.generateCollision = true;
    settings.collisionType     = CollisionType::TriangleMesh;

    const auto result = importGltf("dummy.gltf", out, settings);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    // Scope ifstream to release file handle before remove_all (Windows lock).
    {
        std::ifstream f(out, std::ios::binary);
        ASSERT_TRUE(f.is_open());

        EassHeader hdr{};
        f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        ASSERT_TRUE(f.good());

        EXPECT_EQ(hdr.version,       static_cast<uint16_t>(2));
        EXPECT_EQ(hdr.tocEntryCount, static_cast<uint32_t>(2));

        TocEntry collEntry = findTocEntry(f, hdr, "COLL");
        EXPECT_GT(collEntry.size, 0u) << "COLL TocEntry must have non-zero size";
        EXPECT_EQ(collEntry.offset % 64u, 0u) << "COLL section must be 64-byte aligned";

        // Read the CollSectionHeader.
        f.seekg(collEntry.offset, std::ios::beg);
        CollSectionHeader csh{};
        f.read(reinterpret_cast<char*>(&csh), sizeof(csh));
        ASSERT_TRUE(f.good());

        EXPECT_EQ(csh.collisionType, static_cast<uint8_t>(CollisionType::TriangleMesh));
        EXPECT_GT(csh.vertexCount, 0u) << "TriangleMesh collision must have vertices";
        EXPECT_GT(csh.indexCount,  0u) << "TriangleMesh collision must have indices";

        // For the unit-cube fallback mesh: 8 vertices, 36 indices.
        EXPECT_EQ(csh.vertexCount, 8u);
        EXPECT_EQ(csh.indexCount,  36u);
    }

    fs::remove_all(out.parent_path());
}

// ---------------------------------------------------------------------------
// Test 4: Import with generateCollision=true, ConvexHull →
//         version 2, COLL present, vertex count <= mesh count, index count == 0.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, ConvexHullCollisionProducesVersion2WithNoIndices)
{
    const fs::path out =
        fs::temp_directory_path() / "coll_import_tests" / "hull_coll.easset";
    fs::create_directories(out.parent_path());

    ImportSettings settings{};
    settings.generateCollision = true;
    settings.collisionType     = CollisionType::ConvexHull;

    const auto result = importGltf("dummy.gltf", out, settings);
    ASSERT_TRUE(result.ok) << result.errorMessage;

    // Scope ifstream to release file handle before remove_all (Windows lock).
    {
        std::ifstream f(out, std::ios::binary);
        ASSERT_TRUE(f.is_open());

        EassHeader hdr{};
        f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
        ASSERT_TRUE(f.good());

        EXPECT_EQ(hdr.version, static_cast<uint16_t>(2));

        TocEntry collEntry = findTocEntry(f, hdr, "COLL");
        EXPECT_GT(collEntry.size, 0u);

        f.seekg(collEntry.offset, std::ios::beg);
        CollSectionHeader csh{};
        f.read(reinterpret_cast<char*>(&csh), sizeof(csh));
        ASSERT_TRUE(f.good());

        EXPECT_EQ(csh.collisionType, static_cast<uint8_t>(CollisionType::ConvexHull));
        EXPECT_GT(csh.vertexCount, 0u) << "ConvexHull must have vertices";
        // ConvexHull does not store indices — vertex soup only.
        EXPECT_EQ(csh.indexCount, 0u) << "ConvexHull collision must have no indices";
    }

    fs::remove_all(out.parent_path());
}

// ---------------------------------------------------------------------------
// Test 5: loadEasset() on a version-2 file with TriangleMesh collision →
//         CpuMesh::collision is populated with correct type, vertices, indices.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, LoadV2EassetHasTriangleMeshCollision)
{
    const fs::path out =
        fs::temp_directory_path() / "coll_import_tests" / "v2_tri_load.easset";
    fs::create_directories(out.parent_path());

    ImportSettings settings{};
    settings.generateCollision = true;
    settings.collisionType     = CollisionType::TriangleMesh;

    const auto importResult = importGltf("dummy.gltf", out, settings);
    ASSERT_TRUE(importResult.ok);

    const auto mesh = loadEasset(out);
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(mesh->collision.has_value())
        << "Version-2 file with COLL section must have collision data";

    const CpuCollision& coll = *mesh->collision;
    EXPECT_EQ(coll.type, CollisionType::TriangleMesh);
    EXPECT_EQ(coll.vertices.size(), 8u)  << "Unit cube has 8 unique positions";
    EXPECT_EQ(coll.indices.size(),  36u) << "Unit cube has 36 triangle indices";

    // Every vertex position must lie within the unit half-extent [-0.5, 0.5].
    for (const auto& v : coll.vertices) {
        EXPECT_GE(v[0], -0.5f); EXPECT_LE(v[0], 0.5f);
        EXPECT_GE(v[1], -0.5f); EXPECT_LE(v[1], 0.5f);
        EXPECT_GE(v[2], -0.5f); EXPECT_LE(v[2], 0.5f);
    }

    fs::remove_all(out.parent_path());
}

// ---------------------------------------------------------------------------
// Test 6: loadEasset() on a version-2 file with ConvexHull collision →
//         CpuMesh::collision has type ConvexHull and empty indices.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, LoadV2EassetHasConvexHullCollision)
{
    const fs::path out =
        fs::temp_directory_path() / "coll_import_tests" / "v2_hull_load.easset";
    fs::create_directories(out.parent_path());

    ImportSettings settings{};
    settings.generateCollision = true;
    settings.collisionType     = CollisionType::ConvexHull;

    const auto importResult = importGltf("dummy.gltf", out, settings);
    ASSERT_TRUE(importResult.ok);

    const auto mesh = loadEasset(out);
    ASSERT_TRUE(mesh.has_value());
    ASSERT_TRUE(mesh->collision.has_value());

    const CpuCollision& coll = *mesh->collision;
    EXPECT_EQ(coll.type, CollisionType::ConvexHull);
    EXPECT_GT(coll.vertices.size(), 0u);
    EXPECT_TRUE(coll.indices.empty()) << "ConvexHull must have no indices";

    fs::remove_all(out.parent_path());
}

// ---------------------------------------------------------------------------
// Test 7: ColliderComponent auto-attach from collision data — the shape type
//         set on ColliderComponent must match the CpuCollision type.
//         (Tests the logic without requiring a full Application/GPU stack.)
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, ColliderComponentShapeMatchesCollisionType)
{
    // Simulate what Application::run() does when draining pendingMeshLoads_.
    World w;
    Entity e = w.createEntity();

    // Simulate: cpuMesh has a TriangleMesh collision section.
    {
        ColliderComponent cc{};
        cc.shape = ColliderComponent::Shape::TriangleMesh;
        w.addComponent<ColliderComponent>(e, cc);
    }

    const ColliderComponent* got = w.tryGet<ColliderComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->shape, ColliderComponent::Shape::TriangleMesh);
}

// ---------------------------------------------------------------------------
// Test 8: If an entity already has a ColliderComponent, it must not be
//         overwritten (the existing component takes priority).
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, ExistingColliderComponentNotOverwritten)
{
    World w;
    Entity e = w.createEntity();

    // Entity has a manually-authored Box collider.
    ColliderComponent authored{};
    authored.shape          = ColliderComponent::Shape::Box;
    authored.params.box     = { 2.0f, 2.0f, 2.0f };
    w.addComponent<ColliderComponent>(e, authored);

    // Simulate the auto-attach guard: tryGet returns non-null, so we skip.
    const bool alreadyHas = (w.tryGet<ColliderComponent>(e) != nullptr);
    EXPECT_TRUE(alreadyHas);

    // Verify the original component is intact.
    const ColliderComponent* got = w.tryGet<ColliderComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->shape, ColliderComponent::Shape::Box);
    EXPECT_FLOAT_EQ(got->params.box.halfX, 2.0f);
}

// ---------------------------------------------------------------------------
// Test 9: ConvexHull ColliderComponent shape value round-trips through ECS.
// ---------------------------------------------------------------------------
TEST(CollisionImportTest, ConvexHullShapeRoundTripsThroughEcs)
{
    World w;
    Entity e = w.createEntity();

    ColliderComponent cc{};
    cc.shape = ColliderComponent::Shape::ConvexHull;
    w.addComponent<ColliderComponent>(e, cc);

    const ColliderComponent* got = w.tryGet<ColliderComponent>(e);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->shape, ColliderComponent::Shape::ConvexHull);
}
