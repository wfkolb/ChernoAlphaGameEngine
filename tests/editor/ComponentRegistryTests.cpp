#ifdef ENGINE_DEVREL

#include <gtest/gtest.h>
#include "editor/panels/InspectorPanel.h"
#include <core/ecs/World.h>
#include <core/ecs/Entity.h>

using namespace engine;
using namespace engine::editor;
using namespace engine::core::ecs;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// A scratch component type with a high ID unlikely to clash with real engine
// components in any test run. Uses kComponentId >= 200 to stay clear.
struct ScratchComp200 {
    static constexpr ComponentTypeId kComponentId = 200;
    int value = 0;
};

struct ScratchComp201 {
    static constexpr ComponentTypeId kComponentId = 201;
    float value = 0.f;
};

bool g_scratchRegistered = false;

void ensureScratchRegistered() {
    if (g_scratchRegistered) return;
    g_scratchRegistered = true;
    World::registerComponent<ScratchComp200>({
        "ScratchComp200", sizeof(ScratchComp200), alignof(ScratchComp200),
        [](void* p) { new(p) ScratchComp200{}; }, nullptr, nullptr
    });
    World::registerComponent<ScratchComp201>({
        "ScratchComp201", sizeof(ScratchComp201), alignof(ScratchComp201),
        [](void* p) { new(p) ScratchComp201{}; }, nullptr, nullptr
    });
}

} // namespace

// ---------------------------------------------------------------------------
// ComponentEditorRegistry — basic registration
// ---------------------------------------------------------------------------

TEST(ComponentEditorRegistry, RegisterAndFindWidget) {
    ComponentEditorRegistry reg;

    bool called = false;
    reg.registerWidget(42, [&called](void*) -> bool { called = true; return true; });

    const auto* w = reg.find(42);
    ASSERT_NE(w, nullptr);
    (*w)(nullptr);
    EXPECT_TRUE(called);
}

TEST(ComponentEditorRegistry, FindReturnsNullForUnregistered) {
    ComponentEditorRegistry reg;
    EXPECT_EQ(reg.find(99), nullptr);
}

TEST(ComponentEditorRegistry, HasWidget) {
    ComponentEditorRegistry reg;
    EXPECT_FALSE(reg.hasWidget(10));
    reg.registerWidget(10, [](void*) -> bool { return false; });
    EXPECT_TRUE(reg.hasWidget(10));
}

TEST(ComponentEditorRegistry, RegisterTraitsNoOpsOnEmptyWidget) {
    // ComponentTraits<ScratchComp200> is unspecialized — makeWidget() returns {}.
    // registerTraits<T>() must not crash and must leave hasWidget() false.
    ComponentEditorRegistry reg;
    reg.registerTraits<ScratchComp200>();
    EXPECT_FALSE(reg.hasWidget(ScratchComp200::kComponentId));
}

TEST(ComponentEditorRegistry, OverwriteWidgetWithRegisterWidget) {
    ComponentEditorRegistry reg;
    int callCount = 0;
    reg.registerWidget(5, [&callCount](void*) -> bool { ++callCount; return false; });
    reg.registerWidget(5, [&callCount](void*) -> bool { callCount += 10; return false; });

    const auto* w = reg.find(5);
    ASSERT_NE(w, nullptr);
    (*w)(nullptr);
    EXPECT_EQ(callCount, 10); // second registration wins
}

// ---------------------------------------------------------------------------
// validateCoverage — success path
// ---------------------------------------------------------------------------

TEST(ComponentEditorRegistry, ValidateCoveragePassesWhenAllCovered) {
    ensureScratchRegistered();

    ComponentEditorRegistry reg;
    // Register widgets for both scratch components.
    reg.registerWidget(ScratchComp200::kComponentId, [](void*) -> bool { return false; });
    reg.registerWidget(ScratchComp201::kComponentId, [](void*) -> bool { return false; });

    // validateCoverage checks ALL slots with meta.name != nullptr in the global
    // table — we must cover every component registered so far in this process.
    // Enumerate and fill any gaps with a no-op widget so the call doesn't assert.
    for (uint16_t id = 0; id < 256; ++id) {
        const auto& meta = World::getComponentMeta(static_cast<ComponentTypeId>(id));
        if (meta.name && !reg.hasWidget(static_cast<ComponentTypeId>(id))) {
            reg.registerWidget(static_cast<ComponentTypeId>(id),
                               [](void*) -> bool { return false; });
        }
    }

    // Should not abort.
    EXPECT_NO_FATAL_FAILURE(reg.validateCoverage());
}

// ---------------------------------------------------------------------------
// validateCoverage — death path (requires debug build where ENGINE_ASSERT fires)
// ---------------------------------------------------------------------------

#if !defined(NDEBUG)
TEST(ComponentEditorRegistry, ValidateCoverageAbortsOnMissingWidget) {
    ensureScratchRegistered();

    // Build a registry that covers every slot EXCEPT ScratchComp200.
    ComponentEditorRegistry reg;
    for (uint16_t id = 0; id < 256; ++id) {
        if (id == ScratchComp200::kComponentId) continue; // intentionally skip
        const auto& meta = World::getComponentMeta(static_cast<ComponentTypeId>(id));
        if (meta.name) {
            reg.registerWidget(static_cast<ComponentTypeId>(id),
                               [](void*) -> bool { return false; });
        }
    }

    // ENGINE_ASSERT calls std::abort() in debug mode.
    EXPECT_DEATH(reg.validateCoverage(), "");
}
#endif // !NDEBUG

#endif // ENGINE_DEVREL
