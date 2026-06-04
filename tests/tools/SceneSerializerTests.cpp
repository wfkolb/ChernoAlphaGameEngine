#include <gtest/gtest.h>

#include <tools/ByteWriter.h>
#include <tools/SceneSerializer.h>

#include <core/components/Health.h>
#include <core/components/Lifetime.h>
#include <core/components/TeamTag.h>
#include <core/scene/Scene.h>
#include <core/scene/SceneGlobals.h>

#include <filesystem>
#include <fstream>
#include <cstring>

using namespace engine::tools;
using namespace engine::core::scene;
using engine::core::ecs::World;
using engine::core::ecs::Entity;

namespace fs = std::filesystem;

// ── Helpers ───────────────────────────────────────────────────────────────────

static fs::path tempPath(const char* name) {
    return fs::temp_directory_path() / name;
}

// Component loaders — registered once per test suite, cleared in teardown.
static void registerDefaultLoaders() {
    SceneSerializer::registerComponentLoader(
        engine::core::Lifetime::kComponentId,
        [](World& w, Entity e, const uint8_t* data, size_t sz) {
            engine::core::Lifetime lt{};
            if (sz >= sizeof(lt)) std::memcpy(&lt, data, sizeof(lt));
            w.addComponent<engine::core::Lifetime>(e, lt);
        }
    );
    SceneSerializer::registerComponentLoader(
        engine::core::Health::kComponentId,
        [](World& w, Entity e, const uint8_t* data, size_t sz) {
            engine::core::Health h{};
            if (sz >= sizeof(h)) std::memcpy(&h, data, sizeof(h));
            w.addComponent<engine::core::Health>(e, h);
        }
    );
    SceneSerializer::registerComponentLoader(
        engine::core::TeamTag::kComponentId,
        [](World& w, Entity e, const uint8_t* data, size_t sz) {
            engine::core::TeamTag tt{};
            if (sz >= sizeof(tt)) std::memcpy(&tt, data, sizeof(tt));
            w.addComponent<engine::core::TeamTag>(e, tt);
        }
    );
}

// ── ByteWriter / ByteReader tests ─────────────────────────────────────────────

TEST(ByteWriter, RoundTripU8) {
    ByteWriter bw;
    bw.writeU8(0xAB);
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_EQ(br.readU8(), 0xAB);
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, RoundTripU16) {
    ByteWriter bw;
    bw.writeU16(0x1234);
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_EQ(br.readU16(), 0x1234u);
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, RoundTripU32) {
    ByteWriter bw;
    bw.writeU32(0xDEADBEEFu);
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_EQ(br.readU32(), 0xDEADBEEFu);
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, RoundTripU64) {
    ByteWriter bw;
    bw.writeU64(0x0123456789ABCDEFull);
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_EQ(br.readU64(), 0x0123456789ABCDEFull);
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, RoundTripF32) {
    ByteWriter bw;
    bw.writeF32(3.14f);
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_NEAR(br.readF32(), 3.14f, 1e-6f);
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, RoundTripString) {
    ByteWriter bw;
    bw.writeString("hello_world");
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_EQ(br.readString(), "hello_world");
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, EmptyStringRoundTrip) {
    ByteWriter bw;
    bw.writeString("");
    ByteReader br(bw.data().data(), bw.size());
    EXPECT_EQ(br.readString(), "");
    EXPECT_TRUE(br.ok());
}

TEST(ByteWriter, ReadBeyondEndSetsOkFalse) {
    ByteWriter bw;
    bw.writeU32(42u);
    ByteReader br(bw.data().data(), bw.size());
    br.readU32();        // valid read
    br.readU8();         // overread
    EXPECT_FALSE(br.ok());
}

TEST(ByteWriter, SkipAdvancesPosition) {
    ByteWriter bw;
    bw.writeU32(0u);
    bw.writeU32(0xCAFEu);
    ByteReader br(bw.data().data(), bw.size());
    br.skip(4);
    EXPECT_EQ(br.readU32(), 0xCAFEu);
    EXPECT_TRUE(br.ok());
}

// ── SceneSerializer tests ─────────────────────────────────────────────────────

class SceneSerializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        SceneSerializer::clearComponentLoaders();
        registerDefaultLoaders();
    }
    void TearDown() override {
        SceneSerializer::clearComponentLoaders();
    }
};

TEST_F(SceneSerializerTest, RoundTripEmptyScene) {
    const fs::path p = tempPath("ss_empty.scene");

    Scene src;
    src.load("empty_map", 99u);
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));
    EXPECT_EQ(dst.globals().sceneName, "empty_map");
    EXPECT_EQ(dst.id(), 99u);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripEntityCount) {
    const fs::path p = tempPath("ss_entities.scene");

    Scene src;
    src.load("ent_map");
    src.world().createEntity();
    src.world().createEntity();
    src.world().createEntity();
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int count = 0;
    dst.world().forEachEntity([&](Entity) { ++count; });
    EXPECT_EQ(count, 3);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripLifetimeComponent) {
    const fs::path p = tempPath("ss_lifetime.scene");

    Scene src;
    src.load("lt_map");
    Entity e = src.world().createEntity();
    engine::core::Lifetime lt; lt.remaining = 7.5f;
    src.world().addComponent<engine::core::Lifetime>(e, lt);
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int count = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* ltp = dst.world().tryGet<engine::core::Lifetime>(de);
        if (ltp) {
            EXPECT_NEAR(ltp->remaining, 7.5f, 1e-5f);
            ++count;
        }
    });
    EXPECT_EQ(count, 1);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripHealthComponent) {
    const fs::path p = tempPath("ss_health.scene");

    Scene src;
    src.load("hp_map");
    Entity e = src.world().createEntity();
    engine::core::Health hp;
    hp.currentHp     = 80.0f;
    hp.maxHp         = 100.0f;
    hp.shieldPercent = 0.25f;
    src.world().addComponent<engine::core::Health>(e, hp);
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        auto* hptr = dst.world().tryGet<engine::core::Health>(de);
        if (hptr) {
            EXPECT_NEAR(hptr->currentHp,     80.0f,  1e-5f);
            EXPECT_NEAR(hptr->maxHp,         100.0f, 1e-5f);
            EXPECT_NEAR(hptr->shieldPercent, 0.25f,  1e-5f);
            ++found;
        }
    });
    EXPECT_EQ(found, 1);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripMultipleComponents) {
    const fs::path p = tempPath("ss_multi.scene");

    Scene src;
    src.load("multi_map");
    Entity e = src.world().createEntity();
    engine::core::Lifetime lt; lt.remaining = 3.0f;
    engine::core::Health   hp; hp.currentHp = 50.0f; hp.maxHp = 100.0f;
    src.world().addComponent<engine::core::Lifetime>(e, lt);
    src.world().addComponent<engine::core::Health>(e, hp);
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int entities = 0;
    dst.world().forEachEntity([&](Entity de) {
        ++entities;
        EXPECT_TRUE(dst.world().tryGet<engine::core::Lifetime>(de) != nullptr);
        EXPECT_TRUE(dst.world().tryGet<engine::core::Health>(de)   != nullptr);
    });
    EXPECT_EQ(entities, 1);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripSceneGlobals) {
    const fs::path p = tempPath("ss_globals.scene");

    Scene src;
    src.load("globals_map", 7u);
    src.globals().matchTimeLimit = 120.0f;
    src.globals().maxPlayers     = 12;
    src.globals().gameMode       = "TeamDeathmatch";
    src.globals().fogDensity     = 0.05f;
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));
    EXPECT_NEAR(dst.globals().matchTimeLimit, 120.0f, 1e-5f);
    EXPECT_EQ(dst.globals().maxPlayers,  12);
    EXPECT_EQ(dst.globals().gameMode,    "TeamDeathmatch");
    EXPECT_NEAR(dst.globals().fogDensity, 0.05f, 1e-6f);
    EXPECT_EQ(dst.id(), 7u);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripSpawnPoints) {
    const fs::path p = tempPath("ss_spawns.scene");

    Scene src;
    src.load("spawn_map");
    src.globals().spawnPoints.push_back({1.0f, 0.0f, 2.0f});
    src.globals().spawnPoints.push_back({5.0f, 0.0f, 5.0f});
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));
    ASSERT_EQ(dst.globals().spawnPoints.size(), 2u);
    EXPECT_NEAR(dst.globals().spawnPoints[0].x, 1.0f, 1e-5f);
    EXPECT_NEAR(dst.globals().spawnPoints[1].x, 5.0f, 1e-5f);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, RoundTripDefaultGravity) {
    const fs::path p = tempPath("ss_gravity.scene");

    Scene src;
    src.load("gravity_map");
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));
    EXPECT_NEAR(dst.globals().gravity.y, -9.81f, 1e-4f);
    fs::remove(p);
}

TEST_F(SceneSerializerTest, ValidateReturnsTrueForValidFile) {
    const fs::path p = tempPath("ss_valid.scene");
    Scene src;
    src.load("v_map");
    ASSERT_TRUE(SceneSerializer::save(src, p));
    EXPECT_TRUE(SceneSerializer::validate(p));
    fs::remove(p);
}

TEST_F(SceneSerializerTest, ValidateReturnsFalseForMissingFile) {
    EXPECT_FALSE(SceneSerializer::validate(tempPath("does_not_exist.scene")));
}

TEST_F(SceneSerializerTest, ValidateReturnsFalseForTruncatedFile) {
    const fs::path p = tempPath("ss_trunc.scene");
    // Write only 100 bytes (less than 512-byte header).
    {
        std::ofstream out(p, std::ios::binary);
        std::vector<uint8_t> junk(100, 0xAB);
        out.write(reinterpret_cast<const char*>(junk.data()),
                  static_cast<std::streamsize>(junk.size()));
    }
    EXPECT_FALSE(SceneSerializer::validate(p));
    fs::remove(p);
}

TEST_F(SceneSerializerTest, ValidateReturnsFalseForWrongMagic) {
    const fs::path p = tempPath("ss_magic.scene");

    // Write a file with wrong magic value but otherwise plausible TOML.
    {
        std::ofstream out(p, std::ios::binary);
        // 600-byte file: first 512 bytes are the header with wrong magic.
        std::vector<uint8_t> buf(600, 0u);
        const char* fakeHeader = "magic = \"XXXX\"\nversion = 1\n";
        std::memcpy(buf.data(), fakeHeader, std::strlen(fakeHeader));
        out.write(reinterpret_cast<const char*>(buf.data()),
                  static_cast<std::streamsize>(buf.size()));
    }
    EXPECT_FALSE(SceneSerializer::validate(p));
    fs::remove(p);
}

TEST_F(SceneSerializerTest, UnregisteredComponentSkipped) {
    // Save a scene with Lifetime + Health.
    // Load with only the Lifetime loader registered → Health silently skipped.
    const fs::path p = tempPath("ss_skip.scene");

    Scene src;
    src.load("skip_map");
    Entity e = src.world().createEntity();
    engine::core::Lifetime lt; lt.remaining = 4.0f;
    engine::core::Health   hp; hp.currentHp = 60.0f;
    src.world().addComponent<engine::core::Lifetime>(e, lt);
    src.world().addComponent<engine::core::Health>(e, hp);
    ASSERT_TRUE(SceneSerializer::save(src, p));

    // Re-register only Lifetime loader.
    SceneSerializer::clearComponentLoaders();
    SceneSerializer::registerComponentLoader(
        engine::core::Lifetime::kComponentId,
        [](World& w, Entity ent, const uint8_t* data, size_t sz) {
            engine::core::Lifetime ltt{};
            if (sz >= sizeof(ltt)) std::memcpy(&ltt, data, sizeof(ltt));
            w.addComponent<engine::core::Lifetime>(ent, ltt);
        }
    );

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    dst.world().forEachEntity([&](Entity de) {
        EXPECT_NE(dst.world().tryGet<engine::core::Lifetime>(de), nullptr);
        EXPECT_EQ(dst.world().tryGet<engine::core::Health>(de),   nullptr);
    });
    fs::remove(p);
}

TEST_F(SceneSerializerTest, LoadAsyncSucceeds) {
    const fs::path p = tempPath("ss_async.scene");
    Scene src;
    src.load("async_map");
    ASSERT_TRUE(SceneSerializer::save(src, p));

    Scene dst;
    auto fut = SceneSerializer::loadAsync(dst, p);
    const bool ok = fut.get();
    EXPECT_TRUE(ok);
    EXPECT_EQ(dst.globals().sceneName, "async_map");
    fs::remove(p);
}

TEST_F(SceneSerializerTest, SaveReturnsFalseForUnloadedScene) {
    Scene s; // never called load()
    EXPECT_FALSE(SceneSerializer::save(s, tempPath("ss_noop.scene")));
}

TEST_F(SceneSerializerTest, LoadReturnsFalseForMissingFile) {
    Scene dst;
    EXPECT_FALSE(SceneSerializer::load(dst, tempPath("nonexistent_12345.scene")));
}
