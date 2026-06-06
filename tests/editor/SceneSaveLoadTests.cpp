#ifdef ENGINE_DEVREL

#include <gtest/gtest.h>

#include "editor/EditorPrefs.h"
#include "editor/MetaFileWriter.h"

#include <tools/SceneSerializer.h>
#include <core/components/ColliderComponent.h>
#include <core/scene/Scene.h>

#include <filesystem>
#include <fstream>
#include <cstring>

namespace fs = std::filesystem;

using engine::editor::EditorPrefs;
using engine::editor::AssetMeta;
using engine::editor::AssetImportSettings;
namespace MFW = engine::editor::MetaFileWriter;
using engine::tools::SceneSerializer;
using engine::core::ColliderComponent;
using engine::core::scene::Scene;
using engine::core::ecs::Entity;

static fs::path tmpFile(const char* name) {
    return fs::temp_directory_path() / name;
}

// ── EditorPrefs ───────────────────────────────────────────────────────────────

TEST(EditorPrefsTest, DefaultsBeforeLoad) {
    EditorPrefs p;
    EXPECT_EQ(p.piePort(), EditorPrefs::kDefaultPIEPort);
    EXPECT_TRUE(p.getRecentScenes().empty());
}

TEST(EditorPrefsTest, RoundTripPiePort) {
    const fs::path prefsPath = tmpFile("test_prefs_pieport.toml");
    fs::remove(prefsPath);

    EditorPrefs w;
    w.setPiePort(12345u);
    w.saveToDisk(prefsPath);

    EditorPrefs r;
    r.loadFromDisk(prefsPath);
    EXPECT_EQ(r.piePort(), 12345u);

    fs::remove(prefsPath);
}

TEST(EditorPrefsTest, RoundTripRecentScenes) {
    const fs::path prefsPath = tmpFile("test_prefs_recent.toml");
    fs::remove(prefsPath);

    // Create dummy scene files so getRecentScenes() doesn't filter them out.
    const fs::path s1 = tmpFile("scene_a.scene");
    const fs::path s2 = tmpFile("scene_b.scene");
    { std::ofstream f(s1); f << "x"; }
    { std::ofstream f(s2); f << "x"; }

    EditorPrefs w;
    w.addRecentScene(s1);
    w.addRecentScene(s2);
    w.saveToDisk(prefsPath);

    EditorPrefs r;
    r.loadFromDisk(prefsPath);
    const auto recents = r.getRecentScenes();
    ASSERT_EQ(recents.size(), 2u);
    // Most-recently added is at front.
    EXPECT_EQ(recents[0], s2);
    EXPECT_EQ(recents[1], s1);

    fs::remove(prefsPath);
    fs::remove(s1);
    fs::remove(s2);
}

TEST(EditorPrefsTest, StaleRecentScenesFilteredOut) {
    const fs::path prefsPath = tmpFile("test_prefs_stale.toml");
    fs::remove(prefsPath);

    EditorPrefs w;
    w.addRecentScene(tmpFile("does_not_exist.scene"));
    w.saveToDisk(prefsPath);

    EditorPrefs r;
    r.loadFromDisk(prefsPath);
    EXPECT_TRUE(r.getRecentScenes().empty());

    fs::remove(prefsPath);
}

TEST(EditorPrefsTest, AddRecentDeduplicate) {
    EditorPrefs p;
    const fs::path s = tmpFile("dup.scene");
    p.addRecentScene(s);
    p.addRecentScene(s);
    p.addRecentScene(s);
    // Internal list has one entry (stale entries handled in getRecentScenes).
    // Check via serialise/deserialise round-trip.
    const fs::path prefsPath = tmpFile("test_prefs_dedup.toml");
    p.saveToDisk(prefsPath);

    EditorPrefs r;
    r.loadFromDisk(prefsPath);
    // getRecentScenes() filters stale paths, but the raw list should have 1.
    // We verify via getRecentScenes(): even with the file missing, at most 1 stale entry.
    // Create the file to make it non-stale.
    { std::ofstream f(s); f << "x"; }
    EditorPrefs r2;
    r2.loadFromDisk(prefsPath);
    EXPECT_EQ(r2.getRecentScenes().size(), 1u);

    fs::remove(prefsPath);
    fs::remove(s);
}

// ── MetaFileWriter ────────────────────────────────────────────────────────────

TEST(MetaFileWriterTest, RoundTripSettings) {
    const fs::path easset = tmpFile("mfw_test.easset");
    fs::remove(MFW::metaPath(easset));

    AssetMeta meta;
    meta.sourcePath    = "C:/assets/model.glb";
    meta.sourceModTime = 0x0102030405060708ULL;
    meta.sourceSha256  = "deadbeef";
    meta.settings.uniformScale      = 2.5f;
    meta.settings.upAxisZ           = true;
    meta.settings.generateCollision = true;
    meta.settings.mergeMeshes       = false;

    MFW::write(easset, meta);
    ASSERT_TRUE(MFW::exists(easset));

    const AssetMeta got = MFW::read(easset);
    EXPECT_EQ(got.sourcePath,    meta.sourcePath);
    EXPECT_EQ(got.sourceModTime, meta.sourceModTime);
    EXPECT_EQ(got.sourceSha256,  meta.sourceSha256);
    EXPECT_NEAR(got.settings.uniformScale, 2.5f, 1e-5f);
    EXPECT_TRUE(got.settings.upAxisZ);
    EXPECT_TRUE(got.settings.generateCollision);
    EXPECT_FALSE(got.settings.mergeMeshes);

    fs::remove(MFW::metaPath(easset));
}

TEST(MetaFileWriterTest, MetaPathAppendsMetaExtension) {
    const fs::path easset = fs::path("C:/foo/bar.easset");
    EXPECT_EQ(MFW::metaPath(easset).string(), "C:/foo/bar.easset.meta");
}

TEST(MetaFileWriterTest, ExistsReturnsFalseWhenNoMeta) {
    const fs::path easset = tmpFile("no_meta.easset");
    fs::remove(MFW::metaPath(easset));
    EXPECT_FALSE(MFW::exists(easset));
}

TEST(MetaFileWriterTest, MissingSourcePathIsStale) {
    const fs::path easset = tmpFile("stale_test.easset");
    fs::remove(MFW::metaPath(easset));

    // Write a meta with an empty sourcePath.
    AssetMeta meta;
    meta.sourcePath = "";
    MFW::write(easset, meta);

    EXPECT_TRUE(MFW::isStale(easset));

    fs::remove(MFW::metaPath(easset));
}

// ── SceneSerializer + ColliderComponent ──────────────────────────────────────

class SceneSerializerColliderTest : public ::testing::Test {
protected:
    void SetUp() override {
        SceneSerializer::clearComponentLoaders();
        SceneSerializer::registerComponentLoader(
            ColliderComponent::kComponentId,
            [](engine::core::ecs::World& w, Entity e, const uint8_t* data, size_t sz) {
                ColliderComponent c{};
                if (sz >= sizeof(c)) std::memcpy(&c, data, sizeof(c));
                w.addComponent<ColliderComponent>(e, c);
            }
        );
    }
    void TearDown() override {
        SceneSerializer::clearComponentLoaders();
    }
};

TEST_F(SceneSerializerColliderTest, RoundTripBoxCollider) {
    const fs::path p = tmpFile("collider_box.scene");

    ColliderComponent box{};
    box.shape          = ColliderComponent::Shape::Box;
    box.params.box     = { 2.0f, 1.0f, 3.0f };
    box.layerIndex     = 3u;
    box.isTrigger      = false;

    {
        Scene src;
        src.load("collider_scene", 42u);
        Entity e = src.world().createEntity();
        src.world().addComponent<ColliderComponent>(e, box);
        ASSERT_TRUE(SceneSerializer::save(src, p));
    }

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        const ColliderComponent* got = dst.world().tryGet<ColliderComponent>(de);
        if (!got) return;
        EXPECT_EQ(got->shape, ColliderComponent::Shape::Box);
        EXPECT_FLOAT_EQ(got->params.box.halfX, 2.0f);
        EXPECT_FLOAT_EQ(got->params.box.halfY, 1.0f);
        EXPECT_FLOAT_EQ(got->params.box.halfZ, 3.0f);
        EXPECT_EQ(got->layerIndex, 3u);
        EXPECT_FALSE(got->isTrigger);
        ++found;
    });
    EXPECT_EQ(found, 1);
    fs::remove(p);
}

TEST_F(SceneSerializerColliderTest, RoundTripSphereCollider) {
    const fs::path p = tmpFile("collider_sphere.scene");

    ColliderComponent s{};
    s.shape                = ColliderComponent::Shape::Sphere;
    s.params.sphere.radius = 1.5f;
    s.isTrigger            = true;
    s.materialIndex        = 2u;

    {
        Scene src;
        src.load("sphere_scene");
        Entity e = src.world().createEntity();
        src.world().addComponent<ColliderComponent>(e, s);
        ASSERT_TRUE(SceneSerializer::save(src, p));
    }

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        const ColliderComponent* got = dst.world().tryGet<ColliderComponent>(de);
        if (!got) return;
        EXPECT_EQ(got->shape, ColliderComponent::Shape::Sphere);
        EXPECT_FLOAT_EQ(got->params.sphere.radius, 1.5f);
        EXPECT_TRUE(got->isTrigger);
        EXPECT_EQ(got->materialIndex, 2u);
        ++found;
    });
    EXPECT_EQ(found, 1);
    fs::remove(p);
}

TEST_F(SceneSerializerColliderTest, RoundTripCapsuleCollider) {
    const fs::path p = tmpFile("collider_capsule.scene");

    ColliderComponent cap{};
    cap.shape                    = ColliderComponent::Shape::Capsule;
    cap.params.capsule.radius     = 0.4f;
    cap.params.capsule.halfHeight = 1.2f;
    cap.offsetY                   = -0.5f;

    {
        Scene src;
        src.load("capsule_scene");
        Entity e = src.world().createEntity();
        src.world().addComponent<ColliderComponent>(e, cap);
        ASSERT_TRUE(SceneSerializer::save(src, p));
    }

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    int found = 0;
    dst.world().forEachEntity([&](Entity de) {
        const ColliderComponent* got = dst.world().tryGet<ColliderComponent>(de);
        if (!got) return;
        EXPECT_EQ(got->shape, ColliderComponent::Shape::Capsule);
        EXPECT_FLOAT_EQ(got->params.capsule.radius,     0.4f);
        EXPECT_FLOAT_EQ(got->params.capsule.halfHeight, 1.2f);
        EXPECT_FLOAT_EQ(got->offsetY, -0.5f);
        ++found;
    });
    EXPECT_EQ(found, 1);
    fs::remove(p);
}

TEST_F(SceneSerializerColliderTest, UnknownColliderTypeIdSkippedOnLoad) {
    // Save a scene with a ColliderComponent, then load without a registered loader
    // — the entity should still exist, just without the component.
    const fs::path p = tmpFile("collider_skip.scene");
    {
        Scene src;
        src.load("skip_scene");
        Entity e = src.world().createEntity();
        src.world().addComponent<ColliderComponent>(e, ColliderComponent{});
        ASSERT_TRUE(SceneSerializer::save(src, p));
    }

    SceneSerializer::clearComponentLoaders(); // remove the loader

    Scene dst;
    ASSERT_TRUE(SceneSerializer::load(dst, p));

    // Entity must exist, but it won't have the ColliderComponent.
    int entities = 0;
    dst.world().forEachEntity([&](Entity de) {
        ++entities;
        EXPECT_EQ(dst.world().tryGet<ColliderComponent>(de), nullptr);
    });
    EXPECT_EQ(entities, 1);
    fs::remove(p);
}

#endif // ENGINE_DEVREL
