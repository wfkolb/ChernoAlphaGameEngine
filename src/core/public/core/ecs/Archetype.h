#pragma once

#include "core/ecs/Entity.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::core::ecs {

    struct Archetype {
        ComponentMask mask;

        // One byte-array column per component type present in mask.
        std::unordered_map<ComponentTypeId, std::vector<uint8_t>> columns;

        // Parallel to column rows — maps row index back to the owning entity.
        std::vector<Entity> entities;

        // Graph edges: addEdge[i] points to the archetype reached by adding component i.
        std::array<Archetype*, 256> addEdge    = {};
        std::array<Archetype*, 256> removeEdge = {};

        uint32_t rowCount = 0;
    };

    // ---------- column helpers (implemented in Archetype.cpp) ----------

    // Append one default-constructed row to every column in dst.
    void archetypeAppendRow(Archetype& arch, Entity e);

    // Swap-remove row at 'row' from arch without calling destructors; updates the
    // entity list and returns the entity that was moved into the vacated slot
    // (kInvalidEntity if none).  Caller is responsible for destructor calls.
    Entity archetypeSwapRemoveRow(Archetype& arch, uint32_t row);

    // Copy all component bytes that exist in both src and dst from src row srcRow
    // into dst row dstRow (used when moving an entity between archetypes).
    void archetypeCopySharedComponents(const Archetype& src, uint32_t srcRow,
                                       Archetype& dst, uint32_t dstRow);

} // namespace engine::core::ecs
