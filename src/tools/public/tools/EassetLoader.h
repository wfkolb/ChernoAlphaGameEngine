#pragma once
#include <rendering/Mesh.h>
#include <filesystem>
#include <optional>
#include <vector>

namespace engine::tools {

struct CpuMesh {
    std::vector<rendering::VertexStatic> vertices;
    std::vector<uint32_t>               indices;
};

// Load a .easset file written by importGltf().
// Returns nullopt on any error: missing file, bad magic, version mismatch,
// unsupported asset type, truncated data.
std::optional<CpuMesh> loadEasset(const std::filesystem::path& path);

} // namespace engine::tools
