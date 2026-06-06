// Tests for Task #71 — Entity Authoring Tools:
//   - DuplicateEntityCommand (component copy + transform offset, undo)
//   - Multi-select (SelectionSystem centroid, toggle, range)
//   - Snap-to-surface is a runtime/ImGui feature; tested via unit geometry only

#include <gtest/gtest.h>

#ifdef ENGINE_DEVREL

#include <editor/commands/EntityCommand.h>
#include <editor/SelectionSystem.h>

#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/components/Transform.h>
#include <core/components/Health.h>

using engine::core::Transform;
using engine::core::Health;
using engine::core::ecs::Entity;
using engine::core::ecs::World;
using engine::core::ecs::kInvalidEntity;
using engine::core::math::Vec3;
using engine::editor::DuplicateEntityCommand;
using engine::editor::SelectionSystem;

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────

static Entity makeEntity(World& w, Vec3 pos, float hp = 0.0f) {
    Entity e = w.createEntity();
    Transform t{};
    t.position = pos;
    w.addComponent<Transform>(e, t);
    if (hp > 0.0f) {
        Health h{};
        h.currentHp = hp;
        h.maxHp     = hp;
        w.addComponent<Health>(e, h);
    }
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DuplicateEntityCommand — component copy
// ─────────────────────────────────────────────────────────────────────────────

TEST(DuplicateEntityCommandTest, DuplicateCreatesNewEntity) {
    World w;
    Entity src = makeEntity(w, Vec3{1.f, 2.f, 3.f});

    DuplicateEntityCommand cmd(w, src);
    cmd.execute();

    const Entity dup = cmd.duplicate();
    ASSERT_NE(dup, kInvalidEntity);
    EXPECT_TRUE(w.isAlive(dup));
    EXPECT_NE(dup, src);
}

TEST(DuplicateEntityCommandTest, DuplicateHasTransformOffset) {
    World w;
    Entity src = makeEntity(w, Vec3{1.f, 0.f, 1.f});

    DuplicateEntityCommand cmd(w, src);
    cmd.execute();

    const Entity dup = cmd.duplicate();
    ASSERT_NE(dup, kInvalidEntity);

    const Transform* tr = w.tryGet<Transform>(dup);
    ASSERT_NE(tr, nullptr);
    EXPECT_FLOAT_EQ(tr->position.x, 1.5f);   // +0.5
    EXPECT_FLOAT_EQ(tr->position.y, 0.f);    // unchanged
    EXPECT_FLOAT_EQ(tr->position.z, 1.5f);   // +0.5
}

TEST(DuplicateEntityCommandTest, DuplicateCopiesHealthComponent) {
    World w;
    Entity src = makeEntity(w, Vec3{0.f, 0.f, 0.f}, 80.f);

    DuplicateEntityCommand cmd(w, src);
    cmd.execute();

    const Entity dup = cmd.duplicate();
    ASSERT_NE(dup, kInvalidEntity);

    const Health* h = w.tryGet<Health>(dup);
    ASSERT_NE(h, nullptr);
    EXPECT_FLOAT_EQ(h->currentHp, 80.f);
    EXPECT_FLOAT_EQ(h->maxHp,     80.f);
}

TEST(DuplicateEntityCommandTest, DuplicateDoesNotModifySource) {
    World w;
    Entity src = makeEntity(w, Vec3{3.f, 0.f, 3.f}, 100.f);

    DuplicateEntityCommand cmd(w, src);
    cmd.execute();

    // Source Transform unchanged.
    const Transform* tr = w.tryGet<Transform>(src);
    ASSERT_NE(tr, nullptr);
    EXPECT_FLOAT_EQ(tr->position.x, 3.f);
    EXPECT_FLOAT_EQ(tr->position.z, 3.f);

    // Source Health unchanged.
    const Health* h = w.tryGet<Health>(src);
    ASSERT_NE(h, nullptr);
    EXPECT_FLOAT_EQ(h->currentHp, 100.f);
}

// ─────────────────────────────────────────────────────────────────────────────
//  DuplicateEntityCommand — undo
// ─────────────────────────────────────────────────────────────────────────────

TEST(DuplicateEntityCommandTest, UndoDestroysDuplicate) {
    World w;
    Entity src = makeEntity(w, Vec3{0.f, 0.f, 0.f});

    DuplicateEntityCommand cmd(w, src);
    cmd.execute();

    const Entity dup = cmd.duplicate();
    ASSERT_TRUE(w.isAlive(dup));

    cmd.undo();
    EXPECT_FALSE(w.isAlive(dup));
    // Source survives undo.
    EXPECT_TRUE(w.isAlive(src));
}

TEST(DuplicateEntityCommandTest, RedoAfterUndoCreatesEntity) {
    World w;
    Entity src = makeEntity(w, Vec3{0.f, 0.f, 0.f});

    DuplicateEntityCommand cmd(w, src);
    cmd.execute();
    cmd.undo();

    // Re-execute (redo path).
    cmd.execute();
    const Entity dup2 = cmd.duplicate();
    EXPECT_NE(dup2, kInvalidEntity);
    EXPECT_TRUE(w.isAlive(dup2));
}

TEST(DuplicateEntityCommandTest, UndoOnDeadSourceIsNoOp) {
    World w;
    Entity src = makeEntity(w, Vec3{0.f, 0.f, 0.f});

    DuplicateEntityCommand cmd(w, src);
    // Never executed — duplicate_ is kInvalidEntity; undo must not crash.
    cmd.undo();
    EXPECT_TRUE(w.isAlive(src));
}

// ─────────────────────────────────────────────────────────────────────────────
//  SelectionSystem — single-select
// ─────────────────────────────────────────────────────────────────────────────

TEST(SelectionSystemTest, InitiallyEmpty) {
    SelectionSystem sel;
    EXPECT_TRUE(sel.selection().empty());
    EXPECT_FALSE(sel.isSelected({0, 0}));
}

TEST(SelectionSystemTest, SelectOnlySetsSingleEntity) {
    SelectionSystem sel;
    Entity a{1, 0};
    Entity b{2, 0};
    sel.selectOnly(a);
    sel.selectOnly(b);

    EXPECT_EQ(sel.selection().size(), 1u);
    EXPECT_TRUE(sel.isSelected(b));
    EXPECT_FALSE(sel.isSelected(a));
}

TEST(SelectionSystemTest, ClearSelectionEmptiesAll) {
    SelectionSystem sel;
    sel.selectOnly({1, 0});
    sel.selectOnly({2, 0});
    sel.clearSelection();
    EXPECT_TRUE(sel.selection().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
//  SelectionSystem — toggle
// ─────────────────────────────────────────────────────────────────────────────

TEST(SelectionSystemTest, ToggleAddsEntityWhenNotSelected) {
    SelectionSystem sel;
    Entity a{5, 0};
    sel.toggleSelect(a);
    EXPECT_TRUE(sel.isSelected(a));
    EXPECT_EQ(sel.selection().size(), 1u);
}

TEST(SelectionSystemTest, ToggleRemovesEntityWhenAlreadySelected) {
    SelectionSystem sel;
    Entity a{5, 0};
    sel.toggleSelect(a);
    sel.toggleSelect(a);
    EXPECT_FALSE(sel.isSelected(a));
    EXPECT_TRUE(sel.selection().empty());
}

TEST(SelectionSystemTest, ToggleMultipleEntitiesAccumulate) {
    SelectionSystem sel;
    Entity a{1, 0}, b{2, 0}, c{3, 0};
    sel.toggleSelect(a);
    sel.toggleSelect(b);
    sel.toggleSelect(c);
    EXPECT_EQ(sel.selection().size(), 3u);
    EXPECT_TRUE(sel.isSelected(a));
    EXPECT_TRUE(sel.isSelected(b));
    EXPECT_TRUE(sel.isSelected(c));
}

TEST(SelectionSystemTest, ToggleIgnoresInvalidEntity) {
    SelectionSystem sel;
    sel.toggleSelect(kInvalidEntity);
    EXPECT_TRUE(sel.selection().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
//  SelectionSystem — range-select
// ─────────────────────────────────────────────────────────────────────────────

TEST(SelectionSystemTest, RangeSelectAddsEntitiesBetweenPivotAndTarget) {
    // Order: e0, e1, e2, e3, e4
    SelectionSystem sel;
    Entity e0{0, 0}, e1{1, 0}, e2{2, 0}, e3{3, 0}, e4{4, 0};
    const std::vector<Entity> order = {e0, e1, e2, e3, e4};

    sel.selectOnly(e1);            // set pivot to e1
    sel.rangeSelect(e3, order);    // extend to e3

    EXPECT_TRUE(sel.isSelected(e1));
    EXPECT_TRUE(sel.isSelected(e2));
    EXPECT_TRUE(sel.isSelected(e3));
    // e0 and e4 should not be in selection.
    EXPECT_FALSE(sel.isSelected(e0));
    EXPECT_FALSE(sel.isSelected(e4));
}

TEST(SelectionSystemTest, RangeSelectWorksReverseOrder) {
    SelectionSystem sel;
    Entity e0{0, 0}, e1{1, 0}, e2{2, 0}, e3{3, 0};
    const std::vector<Entity> order = {e0, e1, e2, e3};

    sel.selectOnly(e3);            // pivot at far end
    sel.rangeSelect(e1, order);    // extend backward

    EXPECT_TRUE(sel.isSelected(e1));
    EXPECT_TRUE(sel.isSelected(e2));
    EXPECT_TRUE(sel.isSelected(e3));
    EXPECT_FALSE(sel.isSelected(e0));
}

TEST(SelectionSystemTest, RangeSelectWithNoPivotFallsBackToSingle) {
    SelectionSystem sel;
    Entity a{1, 0}, b{2, 0};
    const std::vector<Entity> order = {a, b};

    // No prior selection (pivot = kInvalidEntity).
    sel.rangeSelect(b, order);

    // Should still add b.
    EXPECT_TRUE(sel.isSelected(b));
}

// ─────────────────────────────────────────────────────────────────────────────
//  SelectionSystem — centroid
// ─────────────────────────────────────────────────────────────────────────────

TEST(SelectionSystemTest, CentroidOfSingleEntityIsItsPosition) {
    World w;
    Entity e = makeEntity(w, Vec3{4.f, 0.f, 0.f});

    SelectionSystem sel;
    sel.selectOnly(e);

    const Vec3 c = sel.selectionCentroid(w);
    EXPECT_FLOAT_EQ(c.x, 4.f);
    EXPECT_FLOAT_EQ(c.y, 0.f);
    EXPECT_FLOAT_EQ(c.z, 0.f);
}

TEST(SelectionSystemTest, CentroidOfTwoEntitiesIsMidpoint) {
    World w;
    Entity a = makeEntity(w, Vec3{0.f, 0.f, 0.f});
    Entity b = makeEntity(w, Vec3{4.f, 2.f, 6.f});

    SelectionSystem sel;
    sel.toggleSelect(a);
    sel.toggleSelect(b);

    const Vec3 c = sel.selectionCentroid(w);
    EXPECT_FLOAT_EQ(c.x, 2.f);
    EXPECT_FLOAT_EQ(c.y, 1.f);
    EXPECT_FLOAT_EQ(c.z, 3.f);
}

TEST(SelectionSystemTest, CentroidOfThreeEntities) {
    World w;
    Entity a = makeEntity(w, Vec3{0.f, 0.f, 0.f});
    Entity b = makeEntity(w, Vec3{3.f, 0.f, 0.f});
    Entity c = makeEntity(w, Vec3{6.f, 0.f, 0.f});

    SelectionSystem sel;
    sel.toggleSelect(a);
    sel.toggleSelect(b);
    sel.toggleSelect(c);

    const Vec3 cent = sel.selectionCentroid(w);
    EXPECT_FLOAT_EQ(cent.x, 3.f);
}

TEST(SelectionSystemTest, CentroidOfEmptySelectionIsZero) {
    World w;
    SelectionSystem sel;
    const Vec3 c = sel.selectionCentroid(w);
    EXPECT_FLOAT_EQ(c.x, 0.f);
    EXPECT_FLOAT_EQ(c.y, 0.f);
    EXPECT_FLOAT_EQ(c.z, 0.f);
}

TEST(SelectionSystemTest, CentroidIgnoresEntitiesWithoutTransform) {
    World w;
    Entity a = makeEntity(w, Vec3{2.f, 0.f, 0.f});
    // b has no Transform
    Entity b = w.createEntity();

    SelectionSystem sel;
    sel.toggleSelect(a);
    sel.toggleSelect(b);

    const Vec3 c = sel.selectionCentroid(w);
    // Only 'a' contributes — centroid == a's position.
    EXPECT_FLOAT_EQ(c.x, 2.f);
}

#else  // !ENGINE_DEVREL

// Tests compile in release too, they just all pass as no-ops.
TEST(DuplicateEntityCommandTest, CompilesInRelease) { SUCCEED(); }
TEST(SelectionSystemTest, CompilesInRelease) { SUCCEED(); }

#endif // ENGINE_DEVREL
