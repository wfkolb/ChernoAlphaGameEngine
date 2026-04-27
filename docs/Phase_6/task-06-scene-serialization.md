# Scene Serialization: Saving and Loading Scenes

**Phase 6 — Physics & Scene Lead Reference**
**Engine:** Windows DX12 / C++20 | `engine::core` module

---

## Table of Contents

1. [Purpose and Scope](#1-purpose-and-scope)
2. [File Format Overview](#2-file-format-overview)
3. [Scene File Structure](#3-scene-file-structure)
4. [Component Serialization](#4-component-serialization)
5. [Asset References](#5-asset-references)
6. [Entity Hierarchy Serialization](#6-entity-hierarchy-serialization)
7. [Editor Workflow](#7-editor-workflow)
8. [Runtime Loading Sequence](#8-runtime-loading-sequence)
9. [Partial Loads and Streaming](#9-partial-loads-and-streaming)
10. [Versioning and Migration](#10-versioning-and-migration)
11. [C++ API](#11-c-api)

---

## 1. Purpose and Scope

The scene serialization system persists a `Scene` — its entities, component data, spatial configuration, and scene-global settings — to and from a `.scene` file on disk.

Two consumers share the same file format:

- **The editor** writes `.scene` files when the developer saves a level.
- **The runtime** reads `.scene` files to load a level during gameplay.

This single-format approach means scenes saved in the editor are loaded byte-for-byte identically at runtime, eliminating editor-to-runtime conversion steps and the bugs that come with them.

### What Is NOT Serialized

| Data | Where It Lives |
|---|---|
| Mesh geometry | `.mesh` asset files, referenced by content hash |
| Texture data | `.tex` asset files |
| Physics mesh (trimesh) | `.phys` asset files |
| Material parameters | `.mat` asset files |
| Physics materials | `config/physics_materials.toml` |
| Navigation mesh | `.navmesh` asset file, referenced by scene globals |
| Runtime-only entity state | In-memory only (e.g. current velocity mid-flight) |

All of the above are **referenced** from the `.scene` file by content-hash strings; they are not embedded.

---

## 2. File Format Overview

A `.scene` file is a **hybrid binary format**: a fixed-size TOML text header, followed by tightly packed binary sections. This makes the file human-inspectable at the header level (version, name, entity count) without sacrificing performance for the binary bulk data.

```
┌──────────────────────────────────────────────┐
│  Text Header  (TOML, fixed 512-byte block)   │
├──────────────────────────────────────────────┤
│  Magic bytes + binary header                 │
├──────────────────────────────────────────────┤
│  Section 0: Entity Table                     │
├──────────────────────────────────────────────┤
│  Section 1: Component Data (SoA per type)    │
├──────────────────────────────────────────────┤
│  Section 2: Asset Reference Table            │
├──────────────────────────────────────────────┤
│  Section 3: Scene Globals                    │
├──────────────────────────────────────────────┤
│  Section 4: Entity Hierarchy                 │
├──────────────────────────────────────────────┤
│  Section 5: Component Metadata Table         │
└──────────────────────────────────────────────┘
```

### Text Header (first 512 bytes, TOML)

The text header is always exactly 512 bytes, null-padded. It is the first thing parsed; its fields tell the loader where each binary section begins.

```toml
[scene]
magic         = "ENGS"
format_version = 3
scene_name    = "arena_01"
scene_id      = 2891473622
created_date  = "2026-04-26T09:00:00Z"
engine_version = "0.6.0"
entity_count  = 847

[sections]
binary_header_offset  = 512
entity_table_offset   = 1024
component_data_offset = 8192
asset_ref_offset      = 524288
globals_offset        = 524800
hierarchy_offset      = 525312
metadata_offset       = 525824
file_size             = 528384
```

---

## 3. Scene File Structure

### Binary Header (immediately after text header)

```cpp
struct SceneFileHeader {
    uint8_t  magic[4];          // 'E','N','G','S'
    uint32_t formatVersion;     // incremented on breaking changes
    uint32_t sceneId;           // stable hash of scene name
    uint32_t entityCount;
    uint32_t archetypeCount;
    uint32_t componentTypeCount;
    uint32_t assetRefCount;
    uint32_t hierarchyPairCount;
    uint64_t sectionOffsets[8]; // indexed by SectionIndex enum
    uint8_t  reserved[32];
    uint32_t headerCRC32;       // CRC of this struct up to (not including) headerCRC32
};
```

### Section 0: Entity Table

One `EntityRecord` per entity. The table is indexed 0..N-1; these indices are used internally for cross-references within the file (not to be confused with the runtime `EntityId`).

```cpp
struct EntityRecord {
    uint32_t fileIndex;         // stable index within this file (0..entityCount-1)
    uint32_t archetypeIndex;    // index into the archetype table at the end of entity table
    uint32_t componentOffsets[MAX_COMPONENTS_PER_ARCHETYPE];
    // componentOffsets[i] = byte offset from start of component data section
    //                       to this entity's data for component type i
    uint32_t hierarchyParentFileIndex;  // 0xFFFFFFFF = no parent
    uint32_t nameOffset;        // offset into a string pool at the end of section 0
};
```

The entity table also includes an **archetype table** — a list of `ArchetypeDescriptor` objects that map archetype indices to component type lists.

```cpp
struct ArchetypeDescriptor {
    uint32_t componentTypeMask[2];   // 128-bit bitmask of component type indices
    uint32_t componentTypeCount;
    uint32_t componentTypeIndices[MAX_COMPONENTS_PER_ARCHETYPE];
};
```

### Section 1: Component Data (SoA per type)

Component data is stored as **Structure-of-Arrays per component type**. All `Transform` components for all entities come first as a contiguous array, then all `RigidBody` components, etc. Within each type's array, entities are ordered by their `fileIndex`.

```
[Component Data Section]
  [Transform array]     offset 0x0000  -- 847 * sizeof(Transform)  bytes
  [Mesh array]          offset 0x1234  -- only entities with Mesh
  [Material array]      offset 0x2468
  [Collider array]      offset 0x3456
  [RigidBody array]     offset 0x5678
  [Health array]        offset 0x6789
  ... (one sub-array per registered component type present in this scene)
```

The offsets within this section are redundantly stored in both the `EntityRecord.componentOffsets` (for random access) and in a **component type index** at the section header (for bulk iteration).

```cpp
struct ComponentSectionHeader {
    uint32_t componentTypeCount;
    struct TypeEntry {
        uint32_t typeId;           // registered component type index
        uint32_t entityCount;      // how many entities have this type
        uint64_t dataOffset;       // offset from section start
        uint32_t componentVersion; // version of the serialized format for this type
        uint32_t strideBytes;      // sizeof serialized record for this type
    } types[MAX_COMPONENT_TYPES];
};
```

### Section 2: Asset Reference Table

Asset references are stored as **content-hash strings** (64-character hex SHA-256). The table is a flat array of `AssetRef` records indexed by a 0-based integer. Component data stores indices into this table rather than inline strings.

```cpp
struct AssetRef {
    char     contentHash[65];      // null-terminated hex SHA-256
    char     logicalPath[256];     // e.g. "meshes/arena_world.mesh" (for debugging)
    uint32_t assetTypeId;          // registered AssetType index (Mesh, Material, etc.)
};
```

Example: the `Mesh` component in the binary data stores a `uint32_t assetRefIndex` (not a full path). At load time, `AssetManager` resolves each index to a live asset handle.

### Section 3: Scene Globals

Serialised `SceneGlobals` struct. Trivially copyable; written as a raw binary block.

```cpp
struct SerializedSceneGlobals {
    float    gravityX, gravityY, gravityZ;
    float    ambientIntensity;
    float    ambientColorR, ambientColorG, ambientColorB;
    uint8_t  fogEnabled;
    float    fogDensity;
    uint32_t navmeshAssetRefIndex;    // 0xFFFFFFFF = none
    float    matchTimeLimit;
    uint8_t  maxPlayers;
    char     gameMode[32];
    uint32_t spawnPointCount;
    // Followed immediately by spawnPointCount * SerializedSpawnPoint structs
};

struct SerializedSpawnPoint {
    uint32_t entityFileIndex;
    float    posX, posY, posZ;
    float    rotX, rotY, rotZ, rotW;
    uint8_t  teamId;
};
```

### Section 4: Entity Hierarchy

Stores parent→child relationships as pairs of file indices.

```cpp
struct HierarchyPair {
    uint32_t parentFileIndex;
    uint32_t childFileIndex;
};
// Array of HierarchyPair, count = SceneFileHeader.hierarchyPairCount
// Sorted by parentFileIndex for efficient child lookup at load time.
```

### Section 5: Component Metadata Table

Maps component type IDs (used in sections 0–1) to stable string names. Used during migration: if a component type name from the file matches a registered type, they are paired; unmatched names trigger the migration table lookup.

```cpp
struct ComponentMetaEntry {
    uint32_t typeId;
    uint32_t version;
    char     name[64];
};
```

---

## 4. Component Serialization

### Default: memcpy for Trivially Copyable Types

If a component type is `std::is_trivially_copyable_v<T>` and has no asset references, the engine serializes it with a raw `memcpy`. No registration needed.

### Custom Serializers for Non-Trivial or Asset-Referencing Components

Register a serializer pair when:
- The component contains an `AssetHandle` (must be stored as an asset ref index).
- The component contains heap-allocated data (e.g. `std::vector`, `std::string`).
- The component has fields that should be excluded from the file.

```cpp
ComponentRegistry::get().register_serializer<Mesh>(
    // Serialize: write to ByteWriter
    [](const Mesh& m, ByteWriter& w, AssetRefCollector& refs) {
        uint32_t idx = refs.add(m.assetHandle);   // get or create ref table entry
        w.write<uint32_t>(idx);
        w.write<uint8_t>(m.lodGroupIndex);
        w.write<uint8_t>(m.castShadows ? 1 : 0);
    },
    // Deserialize: read from ByteReader
    [](Mesh& m, ByteReader& r, const AssetRefTable& refs) {
        uint32_t idx = r.read<uint32_t>();
        m.assetHandle  = refs.getUnresolved(idx);  // resolved async later
        m.lodGroupIndex = r.read<uint8_t>();
        m.castShadows   = r.read<uint8_t>() != 0;
    },
    /*version=*/ 2
);
```

`ByteWriter` and `ByteReader` wrap a memory buffer; they do not perform I/O directly. The serialization system calls them in pass order, then writes the result to the file.

### TriggerCallback Components

`TriggerCallback` stores `std::function` objects, which are **not serializable**. The serializer writes the callback's **string identifier** (a registered name), and the deserializer looks up the function from a callback registry at load time.

```cpp
ComponentRegistry::get().register_serializer<TriggerCallback>(
    [](const TriggerCallback& cb, ByteWriter& w, AssetRefCollector&) {
        w.write_string(cb.onEnterCallbackId);   // e.g. "game::door_open"
        w.write_string(cb.onExitCallbackId);
    },
    [](TriggerCallback& cb, ByteReader& r, const AssetRefTable&) {
        cb.onEnterCallbackId = r.read_string();
        cb.onExitCallbackId  = r.read_string();
        // Resolve at post-load phase:
        cb.onEnter = TriggerCallbackRegistry::get().resolve(cb.onEnterCallbackId);
        cb.onExit  = TriggerCallbackRegistry::get().resolve(cb.onExitCallbackId);
    },
    /*version=*/ 1
);
```

Game code registers callbacks by name at startup:

```cpp
TriggerCallbackRegistry::get().add("game::door_open",
    [](EntityId enterer, Scene& scene) { /* ... */ });
```

---

## 5. Asset References

### Content-Hash Addressing

All assets referenced from a scene are identified by their **content hash** (SHA-256 of the raw asset bytes). This makes scene files resilient to file renames and asset relocations: as long as the content exists somewhere in the asset depot, it can be found.

The `logicalPath` in `AssetRef` is a human-readable hint for debugging and error messages, not used for actual resolution.

### Resolution at Load Time

```cpp
// After all component data is deserialized, AssetManager resolves refs:
void Scene::resolveAssetReferences(const AssetRefTable& refs) {
    for (uint32_t i = 0; i < refs.count(); ++i) {
        const AssetRef& ref = refs[i];
        AssetHandle handle = AssetManager::get().loadByHash(
            ref.contentHash, ref.assetTypeId
        );
        assetHandleMap[i] = handle;
    }
}
// Component data then calls:
m.assetHandle = scene.assetHandleMap[storedIndex];
```

Resolution is **asynchronous** by default. Components with unresolved assets are not rendered until their assets are ready. The `SceneReadyCallback` fires once all async loads complete.

### Missing Assets

If an asset cannot be found by content hash:

- In **debug builds**: an error log is emitted and a bright-pink placeholder mesh/material is used.
- In **release builds**: the entity is hidden (`Mesh.visible = false`) and a warning is logged.
- The scene still loads and is marked `Ready` — missing assets are non-fatal.

---

## 6. Entity Hierarchy Serialization

Parent–child relationships are stored in Section 4 as flat `HierarchyPair` arrays, completely separate from component data. This separation avoids forward-reference complexity during component deserialization.

### Write Path

```cpp
void SceneSerializer::write_hierarchy(const Scene& scene, BinaryWriter& w) {
    std::vector<HierarchyPair> pairs;

    scene.query<const Transform>([&](EntityId childId, const Transform& t) {
        if (t.parent != NULL_ENTITY) {
            uint32_t parentFile = scene.entityToFileIndex(t.parent);
            uint32_t childFile  = scene.entityToFileIndex(childId);
            pairs.push_back({parentFile, childFile});
        }
    });

    std::sort(pairs.begin(), pairs.end(),
        [](const HierarchyPair& a, const HierarchyPair& b) {
            return a.parentFileIndex < b.parentFileIndex;
        });

    for (auto& p : pairs) w.write(p);
}
```

### Read Path

After all entities and components are deserialized, the hierarchy pass runs:

```cpp
void SceneSerializer::apply_hierarchy(Scene& scene,
                                      const std::vector<HierarchyPair>& pairs,
                                      const std::vector<EntityId>& fileIndexToEntityId) {
    for (auto& pair : pairs) {
        EntityId parent = fileIndexToEntityId[pair.parentFileIndex];
        EntityId child  = fileIndexToEntityId[pair.childFileIndex];
        scene.setParent(child, parent);
    }
}
```

Socket entities (named child entities for weapon attachment) are preserved through this mechanism — they are regular entities with a `Name` component whose parent is set during hierarchy application.

---

## 7. Editor Workflow

```
┌───────────────────────────────────────────────────────────────┐
│  Editor                                                       │
│                                                               │
│  1. User places/edits entities in the 3D viewport            │
│  2. All changes go into an in-memory Scene object            │
│  3. Ctrl+S → SceneSerializer::save(scene, "levels/x.scene") │
│  4. File written atomically (to temp, then rename)           │
└───────────────────────────────────────────────────────────────┘
              │  .scene file
              ▼
┌───────────────────────────────────────────────────────────────┐
│  Runtime                                                      │
│                                                               │
│  SceneManager::load("levels/x.scene")                        │
│    → SceneSerializer::load(path) → Scene*                    │
└───────────────────────────────────────────────────────────────┘
```

### Editor-Specific Metadata

The editor appends an optional **editor metadata section** after the standard sections. This section stores UI state (selected entities, camera position, layer visibility toggles) and is silently ignored by the runtime loader.

```cpp
// Editor writes:
w.write_section(SectionId::EditorMeta, [&]() {
    w.write(editorCamera.position);
    w.write(editorCamera.rotation);
    w.write(selectedEntityCount);
    for (auto id : selectedEntities) w.write(scene.entityToFileIndex(id));
});
```

The section is skipped at runtime because the runtime's `SceneSerializer::load` only reads sections up to `SectionId::ComponentMetadata`.

### Incremental Saves

For large scenes (>10k entities), a full re-serialization takes a few hundred milliseconds. The editor performs an **incremental save** by tracking a dirty set of modified entities and rewriting only their component blocks, then updating the relevant section offsets and CRC.

Incremental save is an editor optimisation only; the runtime always reads the full file.

---

## 8. Runtime Loading Sequence

```
SceneSerializer::load(path)
│
├── 1. Open file, read 512-byte TOML text header
│       → parse format_version, section offsets
│       → validate magic bytes and CRC
│
├── 2. Read binary header
│       → validate entityCount, archetypeCount
│
├── 3. Read entity table (Section 0)
│       → build fileIndex → EntityId mapping
│       → allocate archetype storage for all archetypes
│
├── 4. Deserialize component data (Section 1)
│       → for each component type in ComponentSectionHeader:
│           → call registered Deserializer (or memcpy for trivial types)
│           → populate component arrays in archetype storage
│
├── 5. Read asset reference table (Section 2)
│       → build AssetRefTable in memory
│
├── 6. Dispatch async asset loads
│       → AssetManager::loadByHash() for each ref
│       → scene marked "assets pending"
│
├── 7. Deserialize scene globals (Section 3)
│       → copy into SceneGlobals
│       → register spawn points
│
├── 8. Apply entity hierarchy (Section 4)
│       → call scene.setParent() for each pair
│
├── 9. Read component metadata (Section 5)
│       → validate type name → typeId mapping
│       → log warnings for unknown types
│
├── 10. Initialise physics
│        → PhysicsWorld::addCollider() for all Collider components
│        → PhysicsWorld::bakeBVH() for all static entities
│
├── 11. Dispatch TriggerCallback resolution
│        → TriggerCallbackRegistry::resolve() for all trigger entities
│
└── 12. Wait for asset loads to complete (or proceed immediately in streaming mode)
         → scene.state = SceneState::Ready
         → fire onLoaded callback
```

### Load Time Budget (Target)

| Step | Expected Time |
|---|---|
| File open + header parse | < 0.5 ms |
| Entity table + component deserialize | < 5 ms per 10k entities |
| Asset dispatch (async) | < 1 ms (async, non-blocking) |
| BVH bake | < 20 ms per 100k static triangles |
| Total before first render | < 30 ms synchronous work |
| Asset streaming (textures, meshes) | 200–2000 ms async, hidden by load screen |

---

## 9. Partial Loads and Streaming

Full streaming is a Phase 8 deliverable. The serialization format is designed to be forward-compatible with it.

### Cell Format

A cell is a `.scene` file representing a spatial subset of a large map. It uses an identical format to a full scene file. The distinction is purely in how `SceneManager` manages them: cells are loaded additively and their entities share a single `PhysicsWorld` per zone.

### Cell Manifest

```toml
# levels/industrial/zone.manifest.toml
[zone]
name = "industrial_district"
physics_config = "config/industrial_physics.toml"

[[cell]]
id          = "cell_00_00"
file        = "levels/industrial/cell_00_00.scene"
bounds_min  = [-32.0, -10.0, -32.0]
bounds_max  = [ 32.0,  50.0,  32.0]
always_load = true   # this cell is always present (e.g. the spawn room)

[[cell]]
id         = "cell_01_00"
file       = "levels/industrial/cell_01_00.scene"
bounds_min = [ 32.0, -10.0, -32.0]
bounds_max = [ 96.0,  50.0,  32.0]
```

### Cross-Cell Entity References

Entities in different cells should not hold direct `EntityId` references to each other (those IDs are local to their scene). Cross-cell references (e.g. a trigger in cell A that opens a door in cell B) use **named entity references**:

```cpp
// Serialized as a string "cell_01_00::door_main"
// Resolved at activation time by SceneManager::findEntityByName()
```

---

## 10. Versioning and Migration

### Component Version Numbers

Every component type has a **version number** (uint32_t) registered at startup. The version is written into both the `ComponentSectionHeader` (Section 1) and the `ComponentMetaEntry` (Section 5).

```cpp
ComponentRegistry::get().register_component<WeaponState>("WeaponState",
    ComponentFlags::Replicated,
    /*version=*/ 3   // increment when serialized fields change
);
```

### Migration Table

When a file's component version differs from the current registered version, the loader looks up the migration table for an upgrade function:

```cpp
ComponentRegistry::get().register_migration<WeaponState>(
    /*from_version=*/ 2, /*to_version=*/ 3,
    [](ByteReader& oldData, ByteWriter& newData) {
        // Version 2 → 3: added 'fireMode' field (wasn't in v2)
        float ammoReserve = oldData.read<float>();
        float ammoInMag   = oldData.read<float>();
        float magCapacity = oldData.read<float>();
        // Write version 3 layout
        newData.write(ammoReserve);
        newData.write(ammoInMag);
        newData.write(magCapacity);
        newData.write<uint8_t>(0);  // fireMode = Semi (default)
        newData.write<uint8_t>(0);  // isReloading = false (default)
    }
);
```

Multiple migration steps chain automatically: a v1 file is upgraded v1→v2→v3 by composing registered steps.

### Unknown Component Types

If the file references a component type name that is not registered (e.g. a component added after the file was saved, but the file is from an older build), the loader:

1. Logs a warning: `"Unknown component type 'OldDebugComponent' in scene file — skipping"`.
2. Skips the byte range for that component type using the stride recorded in `ComponentSectionHeader`.
3. Continues loading. Entities that had the unknown component are loaded without it.

This policy ensures **forward-compatibility** (new engine builds can load old scene files) and graceful degradation.

### Format Version vs Component Version

| Field | What it gates |
|---|---|
| `format_version` (file header) | Breaking changes to the section layout itself (e.g. adding a new mandatory section) |
| `componentVersion` (per type) | Changes to the serialized layout of one specific component |

A change in `format_version` requires a manual migration pass of all `.scene` files in the project (tooled as a batch CLI operation). Component version changes are handled automatically at load time via the migration table.

---

## 11. C++ API

```cpp
namespace engine::core {

class SceneSerializer {
public:
    // Save a scene to disk. Returns false on error (error logged).
    static bool save(const Scene& scene, std::string_view path,
                     const SaveOptions& opts = {});

    // Load a scene from disk synchronously. Returns nullptr on error.
    static Scene* load(std::string_view path,
                       const LoadOptions& opts = {});

    // Load a scene asynchronously. Calls opts.onComplete when ready.
    static SceneHandle loadAsync(std::string_view path,
                                 const LoadOptions& opts);

    // Validate a .scene file without fully loading it.
    // Returns a list of warnings/errors.
    static ValidationResult validate(std::string_view path);
};

struct SaveOptions {
    bool    includeEditorMetadata = false;  // true in editor builds
    bool    compressComponentData = false;  // LZ4, Phase 8 feature stub
    bool    atomicWrite           = true;   // write to .tmp then rename
};

struct LoadOptions {
    bool     async          = false;
    bool     additive       = false;
    Scene*   targetScene    = nullptr;  // merge into existing scene if non-null
    std::function<void(Scene*)>                 onComplete;
    std::function<void(std::string_view error)> onError;
};

struct ValidationResult {
    bool                     valid;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    uint32_t                 entityCount;
    uint32_t                 formatVersion;
    std::string              sceneName;
};

} // namespace engine::core
```

### Usage Examples

```cpp
// Editor save
bool ok = SceneSerializer::save(*activeScene, "levels/arena_01.scene",
    SaveOptions{ .includeEditorMetadata = true, .atomicWrite = true }
);
if (!ok) show_error_dialog("Scene save failed. Check logs.");

// Runtime load (synchronous, blocking — use in loading screen context)
Scene* scene = SceneSerializer::load("levels/arena_01.scene");
if (!scene) { /* handle error */ }
SceneManager::get().activate(scene);

// Runtime load (asynchronous, non-blocking)
SceneSerializer::loadAsync("levels/arena_01.scene",
    LoadOptions{
        .async = true,
        .onComplete = [](Scene* scene) {
            SceneManager::get().activate(scene);
            HUD::hide_loading_screen();
        },
        .onError = [](std::string_view err) {
            LOG_ERROR("Scene load failed: {}", err);
            SceneManager::get().transition("menus/main_menu.scene");
        }
    }
);

// Validate before shipping
auto result = SceneSerializer::validate("levels/arena_01.scene");
for (auto& warn : result.warnings) LOG_WARN("Scene validation: {}", warn);
for (auto& err  : result.errors)   LOG_ERROR("Scene validation: {}", err);
if (!result.valid) abort_build("Scene {} failed validation");
```

### Integration with AssetManager Hashing

When saving, the serializer queries `AssetManager` for the content hash of each referenced asset:

```cpp
// Inside SceneSerializer::save() — building the asset ref table
AssetRefCollector refs;
scene.query<const Mesh>([&](EntityId, const Mesh& m) {
    refs.add(m.assetHandle);  // records contentHash + logicalPath + typeId
});
scene.query<const Material>([&](EntityId, const Material& m) {
    refs.add(m.assetHandle);
});
// ... etc for all asset-bearing component types
```

The `AssetRefCollector` deduplicates handles — if two entities reference the same asset, only one `AssetRef` entry is written to the file.
