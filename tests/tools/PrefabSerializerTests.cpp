#include <gtest/gtest.h>

#include <tools/PrefabSerializer.h>

#include <core/ecs/World.h>
#include <core/ecs/HierarchyComponent.h>
#include <core/ecs/PrefabInstance.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

using namespace engine::tools;
using namespace engine::core::ecs;
using engine::core::Transform;
using engine::core::Health;

static fs::path tempPath(const char* name) {
    return fs::temp_directory_path() / name;
}

// Register minimal component metadata so addComponentRaw works.
static void registerTestComponents() {
    World::registerComponent<Transform>({
        "Transform", sizeof(Transform), alignof(Transform),
        [](void* p) { new(p) Transform{}; }, nullptr, nullptr
    });
    World::registerComponent<Health>({
        "Health", sizeof(Health), alignof(Health),
        [](void* p) { new(p) Health{}; }, nullptr, nullptr
    });
    World::registerComponent<HierarchyComponent>({
        "HierarchyComponent",
        sizeof(HierarchyComponent), alignof(HierarchyComponent),
        [](void* p) { new(p) HierarchyComponent{}; }, nullptr, nullptr
    });
    World::registerComponent<PrefabInstance>({
        "PrefabInstance",
        sizeof(PrefabInstance), alignof(PrefabInstance),
        [](void* p) { new(p) PrefabInstance{}; }, nullptr, nullptr
    });
}

class PrefabSerializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        registerTestComponents();
    }
};

// ── capture ───────────────────────────────────────────────────────────────────

TEST_F(PrefabSerializerTest, CaptureRootOnlyHasOneEntity) {
    World world;
    Entity root = world.createEntity();
    Transform t{};
    t.position = {1.f, 2.f, 3.f};
    world.addComponent<Transform>(root, t);

    const auto data = PrefabSerializer::capture(root, world);
    EXPECT_EQ(data.entities.size(), 1u);
}

TEST_F(PrefabSerializerTest, CaptureHierarchyHasCorrectEntityCount) {
    World world;
    Entity root  = world.createEntity();
    Entity child = world.createEntity();

    world.addComponent<Transform>(root,  Transform{});
    world.addComponent<Transform>(child, Transform{});
    world.addComponent<HierarchyComponent>(child, HierarchyComponent{});
    linkChild(world, root, child);

    const auto data = PrefabSerializer::capture(root, world);
    ASSERT_EQ(data.entities.size(), 2u);
}

TEST_F(PrefabSerializerTest, CaptureRootIsEntityZero) {
    World world;
    Entity root  = world.createEntity();
    Entity child = world.createEntity();
    world.addComponent<Transform>(root,  Transform{});
    world.addComponent<Transform>(child, Transform{});
    world.addComponent<HierarchyComponent>(child, HierarchyComponent{});
    linkChild(world, root, child);

    const auto data = PrefabSerializer::capture(root, world);
    // root = entity 0: HierarchyComponent parent should be kInvalidEntity
    bool foundHC = false;
    for (const auto& comp : data.entities[0].components) {
        if (comp.typeId == HierarchyComponent::kComponentId) {
            foundHC = true;
            HierarchyComponent hc{};
            std::memcpy(&hc, comp.bytes.data(), sizeof(hc));
            EXPECT_EQ(hc.parent, kInvalidEntity);
        }
    }
    EXPECT_TRUE(foundHC);
}

// ── save / validate ───────────────────────────────────────────────────────────

TEST_F(PrefabSerializerTest, SaveWritesEngpMagic) {
    World world;
    Entity root = world.createEntity();
    world.addComponent<Transform>(root, Transform{});

    auto data = PrefabSerializer::capture(root, world);
    data.name = "TestPrefab";

    const fs::path path = tempPath("test_save.prefab");
    fs::remove(path);
    ASSERT_TRUE(PrefabSerializer::save(data, path));

    // Read first 4 bytes to check magic. Scope closes `in` before remove
    // (Windows requires the file handle to be released before deletion).
    {
        std::ifstream in(path, std::ios::binary);
        char magic[5] = {};
        in.read(magic, 4);
        EXPECT_STREQ(magic, "ENGP");
    }

    fs::remove(path);
}

TEST_F(PrefabSerializerTest, ValidateReturnsTrueForValidFile) {
    World world;
    Entity root = world.createEntity();
    world.addComponent<Transform>(root, Transform{});

    auto data = PrefabSerializer::capture(root, world);
    const fs::path path = tempPath("test_validate.prefab");
    fs::remove(path);
    ASSERT_TRUE(PrefabSerializer::save(data, path));
    EXPECT_TRUE(PrefabSerializer::validate(path));

    fs::remove(path);
}

TEST_F(PrefabSerializerTest, ValidateReturnsFalseForSceneFile) {
    // A .scene file with ENGS magic should fail ENGP validation.
    const fs::path path = tempPath("fake_engp.prefab");
    fs::remove(path);
    {
        std::array<char, 512> buf{};
        const char* fakeMagic = "ENGS";
        std::memcpy(buf.data(), fakeMagic, 4);
        std::ofstream out(path, std::ios::binary);
        out.write(buf.data(), buf.size());
    }
    EXPECT_FALSE(PrefabSerializer::validate(path));
    fs::remove(path);
}

TEST_F(PrefabSerializerTest, ValidateReturnsFalseForEmptyFile) {
    const fs::path path = tempPath("empty.prefab");
    fs::remove(path);
    { std::ofstream f(path); }
    EXPECT_FALSE(PrefabSerializer::validate(path));
    fs::remove(path);
}

// ── load ──────────────────────────────────────────────────────────────────────

TEST_F(PrefabSerializerTest, LoadRoundTripsName) {
    World world;
    Entity root = world.createEntity();
    world.addComponent<Transform>(root, Transform{});

    auto data = PrefabSerializer::capture(root, world);
    data.name = "MyPrefab";

    const fs::path path = tempPath("test_name.prefab");
    fs::remove(path);
    ASSERT_TRUE(PrefabSerializer::save(data, path));

    const auto loaded = PrefabSerializer::load(path);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->name, "MyPrefab");

    fs::remove(path);
}

TEST_F(PrefabSerializerTest, LoadRoundTripsComponentBytes) {
    World world;
    Entity root = world.createEntity();
    Health h{};
    h.currentHp = 75.f;
    h.maxHp     = 100.f;
    world.addComponent<Transform>(root, Transform{});
    world.addComponent<Health>(root, h);

    auto data = PrefabSerializer::capture(root, world);
    const fs::path path = tempPath("test_bytes.prefab");
    fs::remove(path);
    ASSERT_TRUE(PrefabSerializer::save(data, path));

    const auto loaded = PrefabSerializer::load(path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->entities.size(), 1u);

    bool foundHealth = false;
    for (const auto& comp : loaded->entities[0].components) {
        if (comp.typeId == Health::kComponentId) {
            foundHealth = true;
            Health got{};
            std::memcpy(&got, comp.bytes.data(), sizeof(got));
            EXPECT_FLOAT_EQ(got.currentHp, 75.f);
            EXPECT_FLOAT_EQ(got.maxHp, 100.f);
        }
    }
    EXPECT_TRUE(foundHealth);

    fs::remove(path);
}

// ── instantiate ───────────────────────────────────────────────────────────────

TEST_F(PrefabSerializerTest, InstantiateCreatesLiveEntity) {
    World srcWorld;
    Entity src = srcWorld.createEntity();
    srcWorld.addComponent<Transform>(src, Transform{});

    const auto data = PrefabSerializer::capture(src, srcWorld);

    World dstWorld;
    const Entity root = PrefabSerializer::instantiate(data, {}, dstWorld);
    EXPECT_NE(root, kInvalidEntity);
    EXPECT_TRUE(dstWorld.isAlive(root));
}

TEST_F(PrefabSerializerTest, InstantiateWithPositionOffsetApplied) {
    World srcWorld;
    Entity src = srcWorld.createEntity();
    Transform t{};
    t.position = {0.f, 0.f, 0.f};
    srcWorld.addComponent<Transform>(src, t);

    const auto data = PrefabSerializer::capture(src, srcWorld);

    World dstWorld;
    SpawnParams params{};
    params.position = {1.f, 2.f, 3.f};
    const Entity root = PrefabSerializer::instantiate(data, params, dstWorld);

    const auto* tr = dstWorld.tryGet<Transform>(root);
    ASSERT_NE(tr, nullptr);
    EXPECT_NEAR(tr->position.x, 1.f, 1e-5f);
    EXPECT_NEAR(tr->position.y, 2.f, 1e-5f);
    EXPECT_NEAR(tr->position.z, 3.f, 1e-5f);
}

TEST_F(PrefabSerializerTest, InstantiateFixesUpHierarchyParentRefs) {
    World srcWorld;
    Entity srcRoot  = srcWorld.createEntity();
    Entity srcChild = srcWorld.createEntity();
    srcWorld.addComponent<Transform>(srcRoot,  Transform{});
    srcWorld.addComponent<Transform>(srcChild, Transform{});
    srcWorld.addComponent<HierarchyComponent>(srcChild, HierarchyComponent{});
    linkChild(srcWorld, srcRoot, srcChild);

    const auto data = PrefabSerializer::capture(srcRoot, srcWorld);

    World dstWorld;
    const Entity dstRoot = PrefabSerializer::instantiate(data, {}, dstWorld);

    // Find the child entity — it has a HierarchyComponent with parent = dstRoot.
    Entity dstChild = kInvalidEntity;
    dstWorld.forEachEntity([&](Entity e) {
        if (e == dstRoot) return;
        const auto* hc = dstWorld.tryGet<HierarchyComponent>(e);
        if (hc && hc->parent == dstRoot) dstChild = e;
    });

    EXPECT_NE(dstChild, kInvalidEntity);
    EXPECT_TRUE(dstWorld.isAlive(dstChild));
}

TEST_F(PrefabSerializerTest, InstantiateHierarchyNotSrcIds) {
    World srcWorld;
    Entity srcRoot  = srcWorld.createEntity();
    Entity srcChild = srcWorld.createEntity();
    srcWorld.addComponent<Transform>(srcRoot,  Transform{});
    srcWorld.addComponent<Transform>(srcChild, Transform{});
    srcWorld.addComponent<HierarchyComponent>(srcChild, HierarchyComponent{});
    linkChild(srcWorld, srcRoot, srcChild);

    const auto data = PrefabSerializer::capture(srcRoot, srcWorld);
    World dstWorld;
    // Pre-populate dstWorld so instantiated entities get different indices than
    // srcWorld entities (both worlds are fresh and would otherwise assign the
    // same index=0 to their first entity, making the NE check a false pass).
    dstWorld.createEntity();
    const Entity dstRoot = PrefabSerializer::instantiate(data, {}, dstWorld);

    // The child's parent must NOT be the old srcRoot entity ID.
    Entity dstChild = kInvalidEntity;
    dstWorld.forEachEntity([&](Entity e) {
        if (e == dstRoot) return;
        const auto* hc = dstWorld.tryGet<HierarchyComponent>(e);
        if (hc) {
            EXPECT_NE(hc->parent, srcRoot);
            dstChild = e;
        }
    });
    EXPECT_NE(dstChild, kInvalidEntity);
}
