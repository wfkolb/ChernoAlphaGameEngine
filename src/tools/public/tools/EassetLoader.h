#pragma once
#include <rendering/Mesh.h>
#include <tools/AssetImporter.h>
#include <filesystem>
#include <optional>
#include <vector>
#include <array>

namespace engine::tools {

// Collision geometry extracted from a .easset version-2 file.
struct CpuCollision {
    CollisionType                          type;
    std::vector<std::array<float, 3>>      vertices; // position-only, no normal/UV
    std::vector<uint32_t>                  indices;  // empty for ConvexHull
};

struct CpuMesh {
    std::vector<rendering::VertexStatic> vertices;
    std::vector<uint32_t>               indices;
    std::optional<CpuCollision>         collision; // nullopt if no collision section
};

// Load a .easset file written by importGltf().
// Returns nullopt on any error: missing file, bad magic, version mismatch,
// unsupported asset type, truncated data.
// Version-1 files are accepted; collision will be std::nullopt.
std::optional<CpuMesh> loadEasset(const std::filesystem::path& path);

} // namespace engine::tools
