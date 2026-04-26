#include "core/ecs/Archetype.h"
#include "core/ecs/World.h"
#include "core/diag/Assert.h"

#include <cstring>

namespace engine::core::ecs {

    void archetypeAppendRow(Archetype& arch, Entity e) {
        for (auto& [id, col] : arch.columns) {
            const ComponentMeta& meta = World::getComponentMeta(id);
            const size_t oldSize      = col.size();
            col.resize(oldSize + meta.size);
            if (meta.construct) {
                meta.construct(col.data() + oldSize);
            } else {
                std::memset(col.data() + oldSize, 0, meta.size);
            }
        }
        arch.entities.push_back(e);
        ++arch.rowCount;
    }

    // Raw swap-remove: does NOT call destructors. Caller must have already destroyed
    // any components that need it (or is moving the entity, so destruction is wrong).
    Entity archetypeSwapRemoveRow(Archetype& arch, uint32_t row) {
        ENGINE_ASSERT(row < arch.rowCount, "archetypeSwapRemoveRow: row out of range");

        const uint32_t lastRow = arch.rowCount - 1;
        Entity movedEntity     = kInvalidEntity;

        for (auto& [id, col] : arch.columns) {
            const size_t sz  = World::getComponentMeta(id).size;
            uint8_t* rowPtr  = col.data() + row      * sz;
            uint8_t* lastPtr = col.data() + lastRow  * sz;

            if (row != lastRow) {
                std::memcpy(rowPtr, lastPtr, sz);
            }
            col.resize(col.size() - sz);
        }

        if (row != lastRow) {
            movedEntity        = arch.entities[lastRow];
            arch.entities[row] = movedEntity;
        }
        arch.entities.pop_back();
        --arch.rowCount;

        return movedEntity;
    }

    void archetypeCopySharedComponents(const Archetype& src, uint32_t srcRow,
                                       Archetype& dst, uint32_t dstRow) {
        for (const auto& [id, srcCol] : src.columns) {
            auto it = dst.columns.find(id);
            if (it == dst.columns.end()) continue;

            const size_t sz        = World::getComponentMeta(id).size;
            const uint8_t* srcPtr  = srcCol.data() + srcRow * sz;
            uint8_t*       dstPtr  = it->second.data() + dstRow * sz;
            std::memcpy(dstPtr, srcPtr, sz);
        }
    }

} // namespace engine::core::ecs
