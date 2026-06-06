#include <gtest/gtest.h>

#include <tools/SceneSerializer.h>

#include <core/ecs/EntityFactory.h>
#include <core/ecs/Name.h>
#include <core/ecs/World.h>
#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/scene/Scene.h>
#include <core/scene/SceneGlobals.h>

#include <cstring>
#include <filesystem>

using namespace engine::tools;
using namespace engine::core::scene;
using engine::core::ecs::EntityFactory;
using engine::core::ecs::SpawnParams;
using engine::core::ecs::World;
using engine::core::ecs::Entity;
using engine::core::ecs::Name;

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static fs::path tempPath(const char* name) {
    return fs::temp_directory_path() / name;
}

// Register the component loaders SceneSerializer uses during load().
static void registerLoaders() {
    SceneSerializer::registerComponentLoader(
        engine::core::Health::kComponentId,
        [](World& w, Entity e, const uint8_t* data, size_t sz) {
            engine::core::Health h{};
            if (sz >= sizeof(h)) std::memcpy(&h, data, sizeof(h));
            w.addComponent<engine::core::Health>(e, h);
        }
    );
    SceneSerializer::registerComponentLoader(
        engine::core::Lifetime::kComponentId,
        [](World& w, Entity e, const uint8_t* data, size_t sz) {
            engine::core::Lifetime lt{};
            if (sz >= sizeof(lt)) std::memcpy(&lt, data, sizeof(lt));
            w.addComponent<engine::core::Lifetime>(e, lt);
        }
    );
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class ArchetypeRoundTripTest : public ::testing::Test {
protected:
    void SetUp() override {
        SceneSerializer::clearComponentLoaders();
        SceneSerializer::clearEntityFactory();
        registerLoaders();

        // Register a "Soldier" archetype that always starts with full Health
        // (100/100) and a default Lifetime (remaining = 30.0f).
        factory_.registerArchetype("Soldier",
            [](Entity e, const SpawnParams&, World& w) {
                engine::core::Health hp;
                hp.currentHp     = 100.0f;
                hp.maxHp         = 100.0f;
                hp.shieldPercent = 0.0f;
                w.addComponent<engine::core::Health>(e, hp);

                engine::core::Lifetime lt;
                lt.remaining = 30.0f;
                w.addComponent<engine::core::Lifetime>(e, lt);
            });
    }

    void TearDown() override {
        SceneSerializer::clearComponentLoaders();
        SceneSerializer::clearEntityFactory();
    }

    EntityFactory factory_;
};

// ── Tests ─────────────────────────────────────────────────────────────────────

// Save a scene containing a named entity, load it back without a factory:
// entity is created bare and component data is applied from the SoA section.
TEST_F(ArchetypeRoundTripTest, LoadWithoutFactory_ComponentsApplied) {
    const fs::path p = tempPath("art_no_factory.scene");

    // Build and save the source scene.
    Scene src;
    src.load("test_map");
    {
        World& w = src.world();
        Entity e = w.createEntity();
        Name nm("Soldier");
        w.addComponent<Name>(e, nm);
        engine::core::Health hp;
        hp.currentHp = 75.0f; hp.maxHp = 100.0f;
        w.addComponent<engine::core::Health>(e, hp);
    }
    ASSERT_TRUE(SceneSerializer::save(src, p));

    // Load without a factory — entity created bare, SoA data applied.
    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* hptr = dst.world().tryGet<engine::core::Health>(de);
        if (hptr) {
            EXPECT_NEAR(hptr->currentHp, 75.0f, 1e-5f);
            ++found;
        }
    });
    EXPECT_EQ(found, 1);

    fs::remove(p);
}

// Register a factory, save a scene with an entity whose Name matches an
// archetype.  On load the archetype fn sets defaults, then the stored SoA
// delta overwrites only the fields that were explicitly saved.
TEST_F(ArchetypeRoundTripTest, LoadWithFactory_ArchetypeDefaultsThenDeltaApplied) {
    const fs::path p = tempPath("art_with_factory.scene");

    // Save: one Soldier entity with currentHp = 42 (overriding default 100).
    Scene src;
    src.load("factory_map");
    {
        World& w = src.world();
        Entity e = w.createEntity();
        Name nm("Soldier");
        w.addComponent<Name>(e, nm);
        engine::core::Health hp;
        hp.currentHp = 42.0f; hp.maxHp = 100.0f; hp.shieldPercent = 0.0f;
        w.addComponent<engine::core::Health>(e, hp);
        engine::core::Lifetime lt;
        lt.remaining = 30.0f; // same as archetype default
        w.addComponent<engine::core::Lifetime>(e, lt);
    }
    ASSERT_TRUE(SceneSerializer::save(src, p));

    // Register the factory and load.
    SceneSerializer::setEntityFactory(&factory_);

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int count = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* hptr = dst.world().tryGet<engine::core::Health>(de);
        auto* lptr = dst.world().tryGet<engine::core::Lifetime>(de);
        if (hptr && lptr) {
            // Saved delta must have overwritten currentHp to 42.
            EXPECT_NEAR(hptr->currentHp, 42.0f, 1e-5f);
            // maxHp: archetype default == saved value, both are 100.
            EXPECT_NEAR(hptr->maxHp, 100.0f, 1e-5f);
            // Lifetime default matches saved value.
            EXPECT_NEAR(lptr->remaining, 30.0f, 1e-5f);
            ++count;
        }
    });
    EXPECT_EQ(count, 1);

    fs::remove(p);
}

// Verify that the entity gets the Name component on load (with factory path).
TEST_F(ArchetypeRoundTripTest, LoadWithFactory_EntityHasNameComponent) {
    const fs::path p = tempPath("art_name.scene");

    Scene src;
    src.load("name_map");
    {
        World& w = src.world();
        Entity e = w.createEntity();
        Name nm("Soldier");
        w.addComponent<Name>(e, nm);
        engine::core::Health hp;
        hp.currentHp = 80.0f; hp.maxHp = 100.0f;
        w.addComponent<engine::core::Health>(e, hp);
    }
    ASSERT_TRUE(SceneSerializer::save(src, p));

    SceneSerializer::setEntityFactory(&factory_);

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int nameCount = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* nptr = dst.world().tryGet<Name>(de);
        if (nptr) {
            EXPECT_STREQ(nptr->c_str(), "Soldier");
            ++nameCount;
        }
    });
    EXPECT_EQ(nameCount, 1);

    fs::remove(p);
}

// Unknown archetype in factory falls back to bare entity; components from SoA
// are still applied.
TEST_F(ArchetypeRoundTripTest, LoadWithFactory_UnknownArchetypeFallsBackToBare) {
    const fs::path p = tempPath("art_unknown.scene");

    Scene src;
    src.load("unk_map");
    {
        World& w = src.world();
        Entity e = w.createEntity();
        Name nm("Goblin"); // not registered in factory_
        w.addComponent<Name>(e, nm);
        engine::core::Health hp;
        hp.currentHp = 55.0f; hp.maxHp = 60.0f;
        w.addComponent<engine::core::Health>(e, hp);
    }
    ASSERT_TRUE(SceneSerializer::save(src, p));

    SceneSerializer::setEntityFactory(&factory_);

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* hptr = dst.world().tryGet<engine::core::Health>(de);
        if (hptr) {
            EXPECT_NEAR(hptr->currentHp, 55.0f, 1e-5f);
            EXPECT_NEAR(hptr->maxHp,     60.0f, 1e-5f);
            ++found;
        }
    });
    EXPECT_EQ(found, 1);

    fs::remove(p);
}

// A scene with entities that have no Name component is unaffected by the
// factory — they are still created bare.
TEST_F(ArchetypeRoundTripTest, LoadWithFactory_NoNameEntityUnaffected) {
    const fs::path p = tempPath("art_noname.scene");

    Scene src;
    src.load("noname_map");
    {
        World& w = src.world();
        Entity e = w.createEntity();
        // No Name component — factory should not be consulted.
        engine::core::Lifetime lt;
        lt.remaining = 5.0f;
        w.addComponent<engine::core::Lifetime>(e, lt);
    }
    ASSERT_TRUE(SceneSerializer::save(src, p));

    SceneSerializer::setEntityFactory(&factory_);

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* lptr = dst.world().tryGet<engine::core::Lifetime>(de);
        if (lptr) {
            EXPECT_NEAR(lptr->remaining, 5.0f, 1e-5f);
            ++found;
        }
    });
    EXPECT_EQ(found, 1);

    fs::remove(p);
}
