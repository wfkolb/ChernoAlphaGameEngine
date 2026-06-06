#include "tools/SceneSerializer.h"
#include "tools/ByteWriter.h"
#include "tools/PrefabSerializer.h"

#include <core/log.h>
#include <core/ecs/Archetype.h>
#include <core/ecs/World.h>
#include <core/ecs/Name.h>
#include <core/ecs/PrefabInstance.h>
#include <core/scene/Scene.h>
#include <core/scene/SceneGlobals.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <future>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace engine::tools {

namespace {

// ── Constants ─────────────────────────────────────────────────────────────────

constexpr size_t      kHeaderSize = 512;
constexpr const char* kMagic      = "ENGS";
constexpr int64_t     kVersion    = 2;

// ── Component-mask helpers ────────────────────────────────────────────────────

using MaskBytes = std::array<uint8_t, 32>;

static MaskBytes maskToBytes(const core::ecs::ComponentMask& mask) {
    MaskBytes out{};
    for (size_t i = 0; i < 256; ++i) {
        if (mask.test(i))
            out[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
    }
    return out;
}

// ── SceneGlobals binary I/O ───────────────────────────────────────────────────

static void writeGlobals(ByteWriter& bw, const core::scene::SceneGlobals& g) {
    bw.writeF32(g.gravity.x);
    bw.writeF32(g.gravity.y);
    bw.writeF32(g.gravity.z);
    bw.writeF32(g.ambientLight.x);
    bw.writeF32(g.ambientLight.y);
    bw.writeF32(g.ambientLight.z);
    bw.writeF32(g.fogColor.x);
    bw.writeF32(g.fogColor.y);
    bw.writeF32(g.fogColor.z);
    bw.writeF32(g.fogDensity);
    bw.writeU32(g.sceneId);
    bw.writeF32(g.matchTimeLimit);
    bw.writeU32(static_cast<uint32_t>(g.maxPlayers));
    bw.writeString(g.sceneName);
    bw.writeString(g.gameMode);
}

static bool readGlobals(ByteReader& br, core::scene::SceneGlobals& g) {
    g.gravity.x = br.readF32();
    g.gravity.y = br.readF32();
    g.gravity.z = br.readF32();
    g.ambientLight.x = br.readF32();
    g.ambientLight.y = br.readF32();
    g.ambientLight.z = br.readF32();
    g.fogColor.x = br.readF32();
    g.fogColor.y = br.readF32();
    g.fogColor.z = br.readF32();
    g.fogDensity     = br.readF32();
    g.sceneId        = br.readU32();
    g.matchTimeLimit = br.readF32();
    g.maxPlayers     = static_cast<int>(br.readU32());
    g.sceneName      = br.readString();
    g.gameMode       = br.readString();
    return br.ok();
}

} // anonymous namespace

// ── SceneSerializer public API ────────────────────────────────────────────────

void SceneSerializer::registerComponentLoader(core::ecs::ComponentTypeId id,
                                              ComponentLoader loader) {
    loaders_[id] = std::move(loader);
}

void SceneSerializer::clearComponentLoaders() {
    loaders_ = {};
}

bool SceneSerializer::save(const core::scene::Scene& scene,
                           const std::filesystem::path& path) {
    if (!scene.isLoaded()) return false;

    const core::ecs::World&         world   = scene.world();
    const core::scene::SceneGlobals& globals = scene.globals();

    // ── 1. Collect entities ───────────────────────────────────────────────────

    // Assign a sequential saved-index to each live entity.
    std::vector<core::ecs::Entity> entities;
    std::unordered_map<uint32_t, uint32_t> entityToSaved; // entity.index → saved index

    for (const auto& archPtr : world.archetypes()) {
        for (const core::ecs::Entity& e : archPtr->entities) {
            const auto savedIdx = static_cast<uint32_t>(entities.size());
            entityToSaved[e.index] = savedIdx;
            entities.push_back(e);
        }
    }

    // ── 2. Build Entity Table binary ──────────────────────────────────────────

    ByteWriter entityTable;
    entityTable.writeU32(static_cast<uint32_t>(entities.size()));
    for (const auto& archPtr : world.archetypes()) {
        const core::ecs::Archetype& arch = *archPtr;

        // Determine which v2 flags apply to every entity in this archetype.
        // (All entities in an archetype share the same component mask.)
        const bool archHasPrefabRef =
            arch.mask.test(core::ecs::PrefabInstance::kComponentId);
        const bool archHasArchetypeName =
            arch.mask.test(core::ecs::Name::kComponentId);

        // Locate column data pointers once per archetype (nullptr when absent).
        const core::ecs::ComponentMeta& prefabMeta =
            core::ecs::World::getComponentMeta(core::ecs::PrefabInstance::kComponentId);
        const core::ecs::ComponentMeta& nameMeta =
            core::ecs::World::getComponentMeta(core::ecs::Name::kComponentId);

        auto findColData = [&](const core::ecs::Archetype& a,
                               core::ecs::ComponentTypeId id) -> const uint8_t* {
            auto it = a.columns.find(id);
            return (it != a.columns.end()) ? it->second.data() : nullptr;
        };

        const uint8_t* prefabColData =
            archHasPrefabRef ? findColData(arch, core::ecs::PrefabInstance::kComponentId)
                             : nullptr;
        const uint8_t* nameColData =
            archHasArchetypeName ? findColData(arch, core::ecs::Name::kComponentId)
                                 : nullptr;

        const MaskBytes mb = maskToBytes(arch.mask);
        for (uint32_t row = 0; row < arch.rowCount; ++row) {
            const core::ecs::Entity e = arch.entities[row];
            entityTable.writeU32(e.index);
            entityTable.writeU32(e.generation);
            // Component mask (32 bytes)
            entityTable.writeBytes(mb.data(), mb.size());

            // v2 extension: prefab-ref flag + optional path string
            const bool writePrefab = archHasPrefabRef && prefabColData
                                     && prefabMeta.size > 0;
            entityTable.writeU8(writePrefab ? 1u : 0u);
            if (writePrefab) {
                const auto* pi = reinterpret_cast<const core::ecs::PrefabInstance*>(
                    prefabColData + row * prefabMeta.size);
                entityTable.writeString(std::string_view(pi->sourcePrefabPath));
            }

            // v2 extension: archetype name flag + optional name string
            const bool writeName = archHasArchetypeName && nameColData
                                   && nameMeta.size > 0;
            entityTable.writeU8(writeName ? 1u : 0u);
            if (writeName) {
                const auto* nm = reinterpret_cast<const core::ecs::Name*>(
                    nameColData + row * nameMeta.size);
                entityTable.writeString(std::string_view(nm->c_str()));
            }
        }
    }

    // ── 3. Build Component SoA binary ─────────────────────────────────────────

    // Collect per-component-type data: typeId → list of (savedEntityIdx, bytes).
    struct ComponentEntry {
        uint32_t             savedEntityIdx;
        std::vector<uint8_t> bytes;
    };
    struct ComponentColumn {
        core::ecs::ComponentTypeId typeId{};
        size_t                     componentSize{};
        std::string                typeName;
        std::vector<ComponentEntry> entries;
    };
    std::unordered_map<uint8_t, ComponentColumn> columns;

    for (const auto& archPtr : world.archetypes()) {
        const core::ecs::Archetype& arch = *archPtr;
        if (arch.rowCount == 0) continue;

        for (const auto& [typeId, col] : arch.columns) {
            const core::ecs::ComponentMeta& meta =
                core::ecs::World::getComponentMeta(typeId);
            if (meta.size == 0) continue; // not registered, skip

            auto& column = columns[typeId];
            if (column.componentSize == 0) {
                column.typeId         = typeId;
                column.componentSize  = meta.size;
                column.typeName       = meta.name ? meta.name : "";
            }

            for (uint32_t row = 0; row < arch.rowCount; ++row) {
                const core::ecs::Entity e = arch.entities[row];
                auto it = entityToSaved.find(e.index);
                if (it == entityToSaved.end()) continue;

                ComponentEntry entry;
                entry.savedEntityIdx = it->second;
                const uint8_t* src = col.data() + row * meta.size;
                entry.bytes.assign(src, src + meta.size);
                column.entries.push_back(std::move(entry));
            }
        }
    }

    ByteWriter componentSoa;
    componentSoa.writeU32(static_cast<uint32_t>(columns.size()));
    for (const auto& [typeId, column] : columns) {
        componentSoa.writeU8(typeId);
        componentSoa.writeString(column.typeName);
        componentSoa.writeU32(static_cast<uint32_t>(column.componentSize));
        componentSoa.writeU32(static_cast<uint32_t>(column.entries.size()));
        for (const auto& entry : column.entries) {
            componentSoa.writeU32(entry.savedEntityIdx);
            componentSoa.writeBytes(entry.bytes.data(), entry.bytes.size());
        }
    }

    // ── 4. Build Asset Ref Table binary ───────────────────────────────────────

    // Asset ref table — placeholder for future external asset dependencies.
    ByteWriter assetRefTable;
    assetRefTable.writeU32(0u);

    // ── 5. Build SceneGlobals binary ──────────────────────────────────────────

    ByteWriter globalsSection;
    writeGlobals(globalsSection, globals);

    // ── 6. Build Hierarchy binary (placeholder — no parent/child API yet) ─────

    ByteWriter hierarchy;
    hierarchy.writeU32(0u); // count = 0

    // ── 7. Compute section offsets (all offsets are from file start) ──────────

    const uint64_t offEntityTable  = kHeaderSize;
    const uint64_t offComponentSoa = offEntityTable  + entityTable.size();
    const uint64_t offAssetRefTable= offComponentSoa + componentSoa.size();
    const uint64_t offGlobals      = offAssetRefTable+ assetRefTable.size();
    const uint64_t offHierarchy    = offGlobals      + globalsSection.size();

    // ── 8. Build 512-byte TOML header ─────────────────────────────────────────

    auto makeSection = [](uint64_t offset, uint64_t size) {
        toml::table t;
        t.insert_or_assign("offset", static_cast<int64_t>(offset));
        t.insert_or_assign("size",   static_cast<int64_t>(size));
        return t;
    };

    toml::table sections;
    sections.insert_or_assign("entity_table",   makeSection(offEntityTable,  entityTable.size()));
    sections.insert_or_assign("component_soa",  makeSection(offComponentSoa, componentSoa.size()));
    sections.insert_or_assign("asset_ref_table",makeSection(offAssetRefTable,assetRefTable.size()));
    sections.insert_or_assign("scene_globals",  makeSection(offGlobals,      globalsSection.size()));
    sections.insert_or_assign("hierarchy",      makeSection(offHierarchy,    hierarchy.size()));

    toml::table header;
    header.insert_or_assign("magic",        kMagic);
    header.insert_or_assign("version",      kVersion);
    header.insert_or_assign("entity_count", static_cast<int64_t>(entities.size()));
    header.insert_or_assign("sections",     sections);

    std::ostringstream oss;
    oss << header;
    const std::string tomlStr = oss.str();
    if (tomlStr.size() >= kHeaderSize) return false; // header too large

    std::array<uint8_t, kHeaderSize> headerBuf{};
    std::memcpy(headerBuf.data(), tomlStr.data(), tomlStr.size());

    // ── 9. Write to disk ──────────────────────────────────────────────────────

    std::ofstream out(path, std::ios::binary);
    if (!out) return false;

    out.write(reinterpret_cast<const char*>(headerBuf.data()),
              static_cast<std::streamsize>(kHeaderSize));
    out.write(reinterpret_cast<const char*>(entityTable.data().data()),
              static_cast<std::streamsize>(entityTable.size()));
    out.write(reinterpret_cast<const char*>(componentSoa.data().data()),
              static_cast<std::streamsize>(componentSoa.size()));
    out.write(reinterpret_cast<const char*>(assetRefTable.data().data()),
              static_cast<std::streamsize>(assetRefTable.size()));
    out.write(reinterpret_cast<const char*>(globalsSection.data().data()),
              static_cast<std::streamsize>(globalsSection.size()));
    out.write(reinterpret_cast<const char*>(hierarchy.data().data()),
              static_cast<std::streamsize>(hierarchy.size()));

    return out.good();
}

bool SceneSerializer::load(core::scene::Scene& scene,
                           const std::filesystem::path& path) {
    // Read entire file.
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;

    const auto fileSize = static_cast<size_t>(in.tellg());
    if (fileSize < kHeaderSize) return false;

    std::vector<uint8_t> file(fileSize);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(file.data()),
            static_cast<std::streamsize>(fileSize));
    if (!in) return false;
    in.close();

    // ── Parse TOML header ─────────────────────────────────────────────────────

    // Strip null padding so toml++ parser doesn't choke.
    const char* headerBegin = reinterpret_cast<const char*>(file.data());
    size_t tomlLen = 0;
    for (size_t i = 0; i < kHeaderSize; ++i) {
        if (file[i] != 0) tomlLen = i + 1;
    }
    const std::string_view tomlSv(headerBegin, tomlLen);

    toml::table tbl;
    try {
        tbl = toml::parse(tomlSv);
    } catch (...) {
        return false;
    }

    const auto magic   = tbl["magic"  ].value<std::string>();
    const auto version = tbl["version"].value<int64_t>();
    if (!magic || *magic != kMagic || !version)
        return false;
    const int64_t fileVersion = *version;
    if (fileVersion != 1 && fileVersion != 2)
        return false;

    auto getSectionOffset = [&](const char* name) -> uint64_t {
        auto v = tbl["sections"][name]["offset"].value<int64_t>();
        return v ? static_cast<uint64_t>(*v) : 0u;
    };
    auto getSectionSize = [&](const char* name) -> uint64_t {
        auto v = tbl["sections"][name]["size"].value<int64_t>();
        return v ? static_cast<uint64_t>(*v) : 0u;
    };

    const uint64_t etOff    = getSectionOffset("entity_table");
    const uint64_t etSz     = getSectionSize  ("entity_table");
    const uint64_t csoaOff  = getSectionOffset("component_soa");
    const uint64_t csoaSz   = getSectionSize  ("component_soa");
    const uint64_t glOff    = getSectionOffset("scene_globals");
    const uint64_t glSz     = getSectionSize  ("scene_globals");

    // Basic bounds check.
    if (etOff + etSz   > fileSize) return false;
    if (csoaOff + csoaSz > fileSize) return false;
    if (glOff + glSz     > fileSize) return false;

    // ── Read SceneGlobals ─────────────────────────────────────────────────────

    core::scene::SceneGlobals globals;
    {
        ByteReader br(file.data() + glOff, glSz);
        if (!readGlobals(br, globals)) return false;
    }

    // Initialise the scene.
    scene.load(globals.sceneName, globals.sceneId);

    // ── Read Entity Table → create entities ───────────────────────────────────

    std::vector<core::ecs::Entity> savedEntities; // savedIndex → live Entity
    {
        ByteReader br(file.data() + etOff, etSz);
        const uint32_t entityCount = br.readU32();
        savedEntities.reserve(entityCount);
        for (uint32_t i = 0; i < entityCount; ++i) {
            // Original index/generation are recorded but not enforced; we just
            // create entities sequentially in the fresh World.
            br.readU32(); // original index  (ignored)
            br.readU32(); // original generation (ignored)
            br.skip(32);  // component mask (ignored — rebuilt by addComponent)

            if (!br.ok()) return false;

            if (fileVersion == 2) {
                // Read v2 flags: hasPrefabRef then hasArchetypeName.
                const uint8_t hasPrefabRef      = br.readU8();
                std::string   prefabPath;
                if (hasPrefabRef) {
                    prefabPath = br.readString();
                }
                const uint8_t hasArchetypeName  = br.readU8();
                std::string   archetypeName;
                if (hasArchetypeName) {
                    archetypeName = br.readString();
                }
                if (!br.ok()) return false;

                core::ecs::Entity e;
                if (hasPrefabRef && !prefabPath.empty()) {
                    // Reconstruct entity structure from prefab, then let the
                    // component SoA pass overwrite individual components with
                    // the saved (potentially overridden) values.
                    auto prefabData = PrefabSerializer::load(prefabPath);
                    if (prefabData) {
                        core::ecs::SpawnParams params{};
                        e = PrefabSerializer::instantiate(*prefabData, params,
                                                         scene.world());
                    } else {
                        LOG_WARN("SceneSerializer: prefab '{}' not found, "
                                 "creating bare entity", prefabPath);
                        e = scene.world().createEntity();
                    }
                } else {
                    e = scene.world().createEntity();
                }

                if (!archetypeName.empty()) {
                    core::ecs::Name nm(archetypeName.c_str());
                    scene.world().addComponent<core::ecs::Name>(e, nm);
                }

                savedEntities.push_back(e);
            } else {
                // v1: no flags — create entity directly.
                savedEntities.push_back(scene.world().createEntity());
            }
        }
    }

    // ── Read Component SoA → add components ───────────────────────────────────

    {
        ByteReader br(file.data() + csoaOff, csoaSz);
        const uint32_t typeCount = br.readU32();

        for (uint32_t ti = 0; ti < typeCount; ++ti) {
            const auto typeId       = static_cast<core::ecs::ComponentTypeId>(br.readU8());
            br.readString();              // consume stored type name (future: name-keyed lookup)
            const uint32_t compSize = br.readU32();
            const uint32_t entCount = br.readU32();

            if (!br.ok()) return false;

            const ComponentLoader& loader = loaders_[typeId];

            for (uint32_t ei = 0; ei < entCount; ++ei) {
                const uint32_t savedIdx = br.readU32();
                const uint8_t* compData = br.ok()
                    ? (file.data() + csoaOff + br.pos())
                    : nullptr;
                br.skip(compSize);
                if (!br.ok()) return false;

                if (savedIdx >= static_cast<uint32_t>(savedEntities.size()))
                    continue;

                core::ecs::Entity ent = savedEntities[savedIdx];

                // If the entity already has this component (e.g. from prefab
                // instantiation), overwrite its bytes in-place rather than
                // adding a duplicate (which would assert).
                if (scene.world().hasComponent(ent, typeId)) {
                    const core::ecs::ComponentMeta& meta =
                        core::ecs::World::getComponentMeta(typeId);
                    if (meta.size == compSize && compData) {
                        scene.world().forEachComponentOnEntity(ent,
                            [&](core::ecs::ComponentTypeId id, void* ptr) {
                                if (id == typeId)
                                    std::memcpy(ptr, compData, compSize);
                            });
                    }
                } else if (loader) {
                    loader(scene.world(), ent,
                           compData, static_cast<size_t>(compSize));
                } else {
                    LOG_WARN("SceneSerializer: skipping unknown component type ID {} "
                             "on saved entity index {}. "
                             "Was the component registered before loading this scene?",
                             typeId, savedIdx);
                }
            }
        }
    }

    // Restore globals (overwrite what scene.load set).
    scene.globals() = globals;

    return true;
}

std::future<bool> SceneSerializer::loadAsync(core::scene::Scene& scene,
                                             const std::filesystem::path& path) {
    return std::async(std::launch::async, [&scene, path]() {
        return load(scene, path);
    });
}

bool SceneSerializer::validate(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;

    const auto fileSize = static_cast<size_t>(in.tellg());
    if (fileSize < kHeaderSize) return false;

    std::array<uint8_t, kHeaderSize> headerBuf{};
    in.seekg(0);
    in.read(reinterpret_cast<char*>(headerBuf.data()),
            static_cast<std::streamsize>(kHeaderSize));
    if (!in) return false;

    // Strip null padding.
    size_t tomlLen = 0;
    for (size_t i = 0; i < kHeaderSize; ++i) {
        if (headerBuf[i] != 0) tomlLen = i + 1;
    }

    toml::table tbl;
    try {
        tbl = toml::parse(std::string_view(
            reinterpret_cast<const char*>(headerBuf.data()), tomlLen));
    } catch (...) {
        return false;
    }

    const auto magic   = tbl["magic"  ].value<std::string>();
    const auto version = tbl["version"].value<int64_t>();
    if (!magic || *magic != kMagic || !version)
        return false;
    {
        const int64_t v = *version;
        if (v != 1 && v != 2) return false;
    }

    // Verify all five section offsets + sizes are within file bounds.
    const char* sections[] = {
        "entity_table", "component_soa", "asset_ref_table",
        "scene_globals", "hierarchy"
    };
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
