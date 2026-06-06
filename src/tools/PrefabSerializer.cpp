#include "tools/PrefabSerializer.h"
#include "tools/ByteWriter.h"

#include <core/ecs/HierarchyComponent.h>
#include <core/components/Transform.h>
#include <core/log.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace engine::tools {

using core::ecs::Entity;
using core::ecs::kInvalidEntity;
using core::ecs::ComponentTypeId;
using core::ecs::World;
using core::ecs::SpawnParams;
using core::ecs::HierarchyComponent;

namespace {

constexpr size_t      kHeaderSize = 512;
constexpr const char* kMagic      = "ENGP";
constexpr int64_t     kVersion    = 1;

// Collect all entities in the subtree rooted at `root` via DFS.
void collectSubtree(World& world, Entity root, std::vector<Entity>& out) {
    out.push_back(root);
    auto* hc = world.tryGet<HierarchyComponent>(root);
    if (!hc) return;
    Entity child = hc->firstChild;
    while (child != kInvalidEntity) {
        collectSubtree(world, child, out);
        auto* chc = world.tryGet<HierarchyComponent>(child);
        child = chc ? chc->nextSibling : kInvalidEntity;
    }
}

// Convert a live Entity reference to a local-index Entity.
// Uses generation = ~0u as the local-ref marker so it never collides with
// live entities (which start at generation 0 and increment upward).
// Returns kInvalidEntity unchanged.
Entity toLocalRef(const std::unordered_map<uint32_t, uint32_t>& entityToLocal,
                  Entity ref) {
    if (ref == kInvalidEntity) return kInvalidEntity;
    auto it = entityToLocal.find(ref.index);
    if (it != entityToLocal.end()) return Entity{ it->second, ~0u };
    return kInvalidEntity; // external reference becomes invalid
}

} // anonymous namespace

// ── capture ───────────────────────────────────────────────────────────────────

PrefabSerializer::PrefabData PrefabSerializer::capture(Entity root, World& world) {
    PrefabData data;

    std::vector<Entity> entities;
    collectSubtree(world, root, entities);

    // Build entity.index → localIdx map.
    std::unordered_map<uint32_t, uint32_t> entityToLocal;
    for (uint32_t i = 0; i < static_cast<uint32_t>(entities.size()); ++i) {
        entityToLocal[entities[i].index] = i;
    }

    data.entities.reserve(entities.size());

    for (const Entity& e : entities) {
        EntitySnapshot snap;

        world.forEachComponentOnEntity(e, [&](ComponentTypeId typeId, void* rawData) {
            const auto& meta = World::getComponentMeta(typeId);
            if (meta.size == 0) return;

            ComponentData comp;
            comp.typeId = typeId;
            comp.bytes.assign(static_cast<uint8_t*>(rawData),
                              static_cast<uint8_t*>(rawData) + meta.size);

            // Rewrite HierarchyComponent entity references to local indices.
            if (typeId == HierarchyComponent::kComponentId &&
                comp.bytes.size() >= sizeof(HierarchyComponent)) {
                auto* hc = reinterpret_cast<HierarchyComponent*>(comp.bytes.data());
                hc->parent      = toLocalRef(entityToLocal, hc->parent);
                hc->firstChild  = toLocalRef(entityToLocal, hc->firstChild);
                hc->nextSibling = toLocalRef(entityToLocal, hc->nextSibling);
                hc->prevSibling = toLocalRef(entityToLocal, hc->prevSibling);
            }

            snap.components.push_back(std::move(comp));
        });

        data.entities.push_back(std::move(snap));
    }

    return data;
}

// ── save ──────────────────────────────────────────────────────────────────────

bool PrefabSerializer::save(const PrefabData& data, const std::filesystem::path& path) {
    // ── Entity table ──────────────────────────────────────────────────────
    ByteWriter entityTable;
    entityTable.writeU32(static_cast<uint32_t>(data.entities.size()));
    for (uint32_t i = 0; i < static_cast<uint32_t>(data.entities.size()); ++i) {
        entityTable.writeU32(i); // local index
    }

    // ── Component SoA ─────────────────────────────────────────────────────
    // Collect per-type component data.
    struct ColEntry {
        uint32_t             localIdx;
        std::vector<uint8_t> bytes;
    };
    struct Column {
        ComponentTypeId      typeId{};
        size_t               compSize{};
        std::string          typeName;
        std::vector<ColEntry> entries;
    };
    std::unordered_map<uint8_t, Column> columns;

    for (uint32_t i = 0; i < static_cast<uint32_t>(data.entities.size()); ++i) {
        for (const auto& comp : data.entities[i].components) {
            const auto& meta = World::getComponentMeta(comp.typeId);
            auto& col = columns[comp.typeId];
            if (col.compSize == 0) {
                col.typeId   = comp.typeId;
                col.compSize = meta.size ? meta.size : comp.bytes.size();
                col.typeName = meta.name ? meta.name : "";
            }
            col.entries.push_back({ i, comp.bytes });
        }
    }

    ByteWriter componentSoa;
    componentSoa.writeU32(static_cast<uint32_t>(columns.size()));
    for (const auto& [typeId, col] : columns) {
        componentSoa.writeU8(typeId);
        componentSoa.writeString(col.typeName);
        componentSoa.writeU32(static_cast<uint32_t>(col.compSize));
        componentSoa.writeU32(static_cast<uint32_t>(col.entries.size()));
        for (const auto& entry : col.entries) {
            componentSoa.writeU32(entry.localIdx);
            componentSoa.writeBytes(entry.bytes.data(), entry.bytes.size());
        }
    }

    // ── Section offsets ───────────────────────────────────────────────────
    const uint64_t offEntityTable  = kHeaderSize;
    const uint64_t offComponentSoa = offEntityTable + entityTable.size();

    auto makeSection = [](uint64_t offset, uint64_t sz) {
        toml::table t;
        t.insert_or_assign("offset", static_cast<int64_t>(offset));
        t.insert_or_assign("size",   static_cast<int64_t>(sz));
        return t;
    };

    toml::table sections;
    sections.insert_or_assign("entity_table",  makeSection(offEntityTable,  entityTable.size()));
    sections.insert_or_assign("component_soa", makeSection(offComponentSoa, componentSoa.size()));

    toml::table header;
    header.insert_or_assign("version",      kVersion);
    header.insert_or_assign("name",         data.name);
    header.insert_or_assign("entity_count", static_cast<int64_t>(data.entities.size()));
    header.insert_or_assign("sections",     sections);

    std::ostringstream oss;
    oss << header;
    const std::string tomlStr = oss.str();
    // Reserve first 4 bytes for raw magic; TOML occupies [4, kHeaderSize).
    if (tomlStr.size() >= kHeaderSize - 4) return false;

    std::array<uint8_t, kHeaderSize> headerBuf{};
    std::memcpy(headerBuf.data(), kMagic, 4);
    std::memcpy(headerBuf.data() + 4, tomlStr.data(), tomlStr.size());

    // ── Write ─────────────────────────────────────────────────────────────
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out.write(reinterpret_cast<const char*>(headerBuf.data()),
              static_cast<std::streamsize>(kHeaderSize));
    out.write(reinterpret_cast<const char*>(entityTable.data().data()),
              static_cast<std::streamsize>(entityTable.size()));
    out.write(reinterpret_cast<const char*>(componentSoa.data().data()),
              static_cast<std::streamsize>(componentSoa.size()));

    return out.good();
}

// ── load ──────────────────────────────────────────────────────────────────────

std::optional<PrefabSerializer::PrefabData> PrefabSerializer::load(
    const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return std::nullopt;

    const auto fileSize = static_cast<size_t>(in.tellg());
    if (fileSize < kHeaderSize) return std::nullopt;

    std::vector<uint8_t> file(fileSize);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(fileSize));
    if (!in) return std::nullopt;
    in.close();

    // Check raw magic at bytes 0-3.
    if (std::memcmp(file.data(), kMagic, 4) != 0) return std::nullopt;

    // Parse TOML header from offset 4.
    const char* hdrBegin = reinterpret_cast<const char*>(file.data() + 4);
    size_t tomlLen = 0;
    for (size_t i = 4; i < kHeaderSize; ++i) {
        if (file[i] != 0) tomlLen = i - 3; // length from offset 4
    }

    toml::table tbl;
    try { tbl = toml::parse(std::string_view(hdrBegin, tomlLen)); }
    catch (...) { return std::nullopt; }

    const auto version = tbl["version"].value<int64_t>();
    if (!version || *version != kVersion)
        return std::nullopt;

    auto getSectionOff = [&](const char* name) -> uint64_t {
        auto v = tbl["sections"][name]["offset"].value<int64_t>();
        return v ? static_cast<uint64_t>(*v) : 0u;
    };
    auto getSectionSz = [&](const char* name) -> uint64_t {
        auto v = tbl["sections"][name]["size"].value<int64_t>();
        return v ? static_cast<uint64_t>(*v) : 0u;
    };

    const uint64_t etOff   = getSectionOff("entity_table");
    const uint64_t etSz    = getSectionSz ("entity_table");
    const uint64_t csoaOff = getSectionOff("component_soa");
    const uint64_t csoaSz  = getSectionSz ("component_soa");

    if (etOff   + etSz   > fileSize) return std::nullopt;
    if (csoaOff + csoaSz > fileSize) return std::nullopt;

    // Read entity table.
    uint32_t entityCount = 0;
    {
        ByteReader br(file.data() + etOff, etSz);
        entityCount = br.readU32();
        for (uint32_t i = 0; i < entityCount; ++i) br.readU32(); // local index (sequential)
        if (!br.ok()) return std::nullopt;
    }

    PrefabData data;
    auto name = tbl["name"].value<std::string>();
    data.name = name ? *name : "";
    data.entities.resize(entityCount);

    // Read component SoA.
    {
        ByteReader br(file.data() + csoaOff, csoaSz);
        const uint32_t typeCount = br.readU32();

        for (uint32_t ti = 0; ti < typeCount; ++ti) {
            const auto typeId   = static_cast<ComponentTypeId>(br.readU8());
            br.readString();            // type name
            const uint32_t compSz = br.readU32();
            const uint32_t entCnt = br.readU32();
            if (!br.ok()) return std::nullopt;

            for (uint32_t ei = 0; ei < entCnt; ++ei) {
                const uint32_t localIdx = br.readU32();
                const uint8_t* dataPtr  = br.ok()
                    ? (file.data() + csoaOff + br.pos())
                    : nullptr;
                br.skip(compSz);
                if (!br.ok()) return std::nullopt;

                if (localIdx < entityCount && dataPtr) {
                    ComponentData comp;
                    comp.typeId = typeId;
                    comp.bytes.assign(dataPtr, dataPtr + compSz);
                    data.entities[localIdx].components.push_back(std::move(comp));
                }
            }
        }
    }

    return data;
}

// ── instantiate ───────────────────────────────────────────────────────────────

core::ecs::Entity PrefabSerializer::instantiate(const PrefabData& data,
                                                const SpawnParams& params,
                                                World& world) {
    if (data.entities.empty()) return kInvalidEntity;

    // Create all entities.
    std::vector<Entity> newEntities;
    newEntities.reserve(data.entities.size());
    for (size_t i = 0; i < data.entities.size(); ++i) {
        newEntities.push_back(world.createEntity());
    }

    // Resolve a local-index Entity back to the newly created Entity.
    // Local refs are encoded with generation == ~0u (see toLocalRef).
    auto resolveRef = [&](Entity localRef) -> Entity {
        if (localRef == kInvalidEntity)   return kInvalidEntity;
        if (localRef.generation != ~0u)   return localRef; // not a local ref
        const uint32_t idx = localRef.index;
        if (idx < static_cast<uint32_t>(newEntities.size())) return newEntities[idx];
        return kInvalidEntity;
    };

    for (size_t i = 0; i < data.entities.size(); ++i) {
        for (const auto& comp : data.entities[i].components) {
            const auto& meta = World::getComponentMeta(comp.typeId);
            if (meta.size == 0 || comp.bytes.size() != meta.size) continue;

            std::vector<uint8_t> bytes = comp.bytes;

            // Fix up HierarchyComponent local refs to real Entity IDs.
            if (comp.typeId == HierarchyComponent::kComponentId &&
                bytes.size() >= sizeof(HierarchyComponent)) {
                auto* hc = reinterpret_cast<HierarchyComponent*>(bytes.data());
                hc->parent      = resolveRef(hc->parent);
                hc->firstChild  = resolveRef(hc->firstChild);
                hc->nextSibling = resolveRef(hc->nextSibling);
                hc->prevSibling = resolveRef(hc->prevSibling);
            }

            // Apply SpawnParams position offset to root entity's Transform.
            if (i == 0 && comp.typeId == core::Transform::kComponentId &&
                bytes.size() >= sizeof(core::Transform)) {
                auto* t = reinterpret_cast<core::Transform*>(bytes.data());
                t->position = t->position + params.position;
            }

            world.addComponentRaw(newEntities[i], comp.typeId, bytes.data(), bytes.size());
        }
    }

    return newEntities[0];
}

// ── validate ──────────────────────────────────────────────────────────────────

bool PrefabSerializer::validate(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;

    const auto fileSize = static_cast<size_t>(in.tellg());
    if (fileSize < kHeaderSize) return false;

    std::array<uint8_t, kHeaderSize> headerBuf{};
    in.seekg(0);
    in.read(reinterpret_cast<char*>(headerBuf.data()),
            static_cast<std::streamsize>(kHeaderSize));
    if (!in) return false;

    // Check raw magic at bytes 0-3.
    if (std::memcmp(headerBuf.data(), kMagic, 4) != 0) return false;

    // Parse TOML from offset 4.
    size_t tomlLen = 0;
    for (size_t i = 4; i < kHeaderSize; ++i) {
        if (headerBuf[i] != 0) tomlLen = i - 3; // length from offset 4
    }

    toml::table tbl;
    try {
        tbl = toml::parse(std::string_view(
            reinterpret_cast<const char*>(headerBuf.data() + 4), tomlLen));
    } catch (...) {
        return false;
    }

    const auto version = tbl["version"].value<int64_t>();
    if (!version || *version != kVersion)
        return false;

    const char* sections[] = { "entity_table", "component_soa" };
    for (const char* sec : sections) {
        const auto off = tbl["sections"][sec]["offset"].value<int64_t>();
        const auto sz  = tbl["sections"][sec]["size"  ].value<int64_t>();
        if (!off || !sz) return false;
        const uint64_t end = static_cast<uint64_t>(*off) + static_cast<uint64_t>(*sz);
        if (end > static_cast<uint64_t>(fileSize)) return false;
    }

    return true;
}

} // namespace engine::tools
