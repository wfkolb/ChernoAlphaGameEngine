#pragma once
#ifdef ENGINE_DEVREL

#include <filesystem>
#include <string>
#include <cstdint>

namespace engine::editor {

// Import settings persisted alongside a cooked .easset file.
struct AssetImportSettings {
    float   uniformScale      = 1.0f;
    bool    upAxisZ           = false; // false = Y-up (default), true = Z-up (Blender)
    bool    generateCollision = false;
    bool    mergeMeshes       = true;
    int     lodCount          = 1;     // Clamped to 1 for Phase 8; reserved for future LOD support
};

// Per-asset sidecar (.easset.meta). Cached source fingerprint lets
// SceneSerializer skip SHA-256 recomputation when the source hasn't changed.
struct AssetMeta {
    std::string         sourcePath;      // absolute path to the source .glb/.gltf
    uint64_t            sourceModTime;   // Windows FILETIME (0 = unknown)
    std::string         sourceSha256;    // hex string (empty = not yet computed)
    AssetImportSettings settings;
};

namespace MetaFileWriter {

// Path of the meta file for an .easset: appends ".meta"
std::filesystem::path metaPath(const std::filesystem::path& eassetPath);

void     write(const std::filesystem::path& eassetPath, const AssetMeta& meta);
AssetMeta read(const std::filesystem::path& eassetPath);

bool     exists(const std::filesystem::path& eassetPath);

// Returns true if the source file mtime differs from the stored value.
bool     isStale(const std::filesystem::path& eassetPath);

} // namespace MetaFileWriter

} // namespace engine::editor

#endif // ENGINE_DEVREL
