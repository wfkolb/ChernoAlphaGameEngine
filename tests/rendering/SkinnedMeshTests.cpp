#include <gtest/gtest.h>
#include <rendering/Mesh.h>

TEST(SkinnedMeshTests, VertexSkinnedLayout) {
    using namespace engine::rendering;
    EXPECT_EQ(sizeof(VertexSkinned), 36u);
    EXPECT_EQ(sizeof(VertexStatic), 28u);
    // VertexSkinned = VertexStatic + 4 bone indices + 4 bone weights = 28+4+4=36
    EXPECT_EQ(sizeof(VertexSkinned) - sizeof(VertexStatic), 8u);
}

TEST(SkinnedMeshTests, VertexSkinnedAlignedToStatic) {
    using namespace engine::rendering;
    // VertexSkinned's base is a VertexStatic — check offset is 0.
    EXPECT_EQ(offsetof(VertexSkinned, base), 0u);
    EXPECT_EQ(offsetof(VertexSkinned, boneIndices), 28u);
    EXPECT_EQ(offsetof(VertexSkinned, boneWeights), 32u);
}
